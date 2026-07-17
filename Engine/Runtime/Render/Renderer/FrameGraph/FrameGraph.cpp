#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphTransientResourcePool.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <queue>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace ve
{
    namespace
    {
        inline constexpr UInt32 InvalidPassIndex = std::numeric_limits<UInt32>::max();

        enum class TextureAccessMode
        {
            Read,
            Write,
        };

        struct TextureAccessRecord
        {
            FrameGraphTextureHandle input;
            FrameGraphTextureHandle output;
            FrameGraphTextureAccess access = FrameGraphTextureAccess::ShaderRead;
            TextureAccessMode mode = TextureAccessMode::Read;
        };

        struct ColorAttachmentRecord
        {
            FrameGraphTextureHandle handle;
            rhi::RhiLoadAction loadAction = rhi::RhiLoadAction::Load;
            rhi::RhiStoreAction storeAction = rhi::RhiStoreAction::Store;
            rhi::RhiColor clearColor = {};
        };

        struct DepthAttachmentRecord
        {
            FrameGraphTextureHandle handle;
            rhi::RhiLoadAction loadAction = rhi::RhiLoadAction::Load;
            rhi::RhiStoreAction storeAction = rhi::RhiStoreAction::Store;
            rhi::RhiDepthStencilClearValue clearValue = {};
            bool readOnly = false;
        };

        [[nodiscard]] bool IsTextureDescValid(const FrameGraphTextureDesc& desc) noexcept
        {
            return desc.width != 0 && desc.height != 0 && desc.depth != 0 && desc.mipLevelCount != 0;
        }

        [[nodiscard]] bool ContainsPass(const std::vector<UInt32>& passes, UInt32 passIndex) noexcept
        {
            return std::find(passes.begin(), passes.end(), passIndex) != passes.end();
        }
    } // namespace

    struct FrameGraph::Impl
    {
        struct ResourceVersion
        {
            UInt32 producer = InvalidPassIndex;
            std::vector<UInt32> readers;
        };

        struct TextureResourceNode
        {
            std::string name;
            FrameGraphTextureDesc desc = {};
            ImportedFrameGraphTexture importedTexture = {};
            bool imported = false;
            std::vector<ResourceVersion> versions{ResourceVersion{}};
            UInt32 firstUse = InvalidPassIndex;
            UInt32 lastUse = InvalidPassIndex;
            std::unique_ptr<rhi::RhiTexture> physicalTexture;
        };

        struct PassNode
        {
            std::string name;
            std::vector<TextureAccessRecord> textureAccesses;
            std::vector<ColorAttachmentRecord> colorAttachments;
            std::optional<DepthAttachmentRecord> depthAttachment;
            rhi::RhiRenderArea renderArea = {};
            rhi::RhiViewport viewport = {};
            rhi::RhiScissorRect scissorRect = {};
            ExecuteFunction executeFunction;
            bool sideEffect = false;
            bool retained = false;
        };

        explicit Impl(FrameGraphExecuteContext inContext)
            : context(inContext)
        {
        }

        void InvalidateCompile() noexcept
        {
            compiled = false;
            compiledPassOrder.clear();
            dependencies.clear();
            reverseDependencies.clear();
            for (PassNode& pass : passes)
            {
                pass.retained = false;
            }
            for (TextureResourceNode& resource : textures)
            {
                resource.firstUse = InvalidPassIndex;
                resource.lastUse = InvalidPassIndex;
            }
        }

        [[nodiscard]] bool IsHandleValid(FrameGraphTextureHandle handle) const noexcept
        {
            return handle.IsValid() && handle.index < textures.size() && handle.version < textures[handle.index].versions.size();
        }

        void AddBuildError(UInt32 passIndex, std::string message)
        {
            std::string prefix = "Frame graph";
            if (passIndex < passes.size())
            {
                prefix += " pass '" + passes[passIndex].name + "'";
            }
            buildErrors.push_back(prefix + ": " + std::move(message));
        }

        FrameGraphExecuteContext context;
        std::vector<TextureResourceNode> textures;
        std::vector<PassNode> passes;
        std::vector<FrameGraphTextureHandle> exportedTextures;
        std::vector<std::string> buildErrors;
        std::vector<std::unordered_set<UInt32>> dependencies;
        std::vector<std::unordered_set<UInt32>> reverseDependencies;
        std::vector<UInt32> compiledPassOrder;
        bool compiled = false;
    };

    FrameGraph::FrameGraph(FrameGraphExecuteContext context)
        : impl_(std::make_unique<Impl>(context))
    {
        VE_ASSERT_RENDER_THREAD();
    }

    FrameGraph::~FrameGraph() = default;

    FrameGraphPassResources::FrameGraphPassResources(const FrameGraph& frameGraph, UInt32 passIndex) noexcept
        : frameGraph_(frameGraph)
        , passIndex_(passIndex)
    {
    }

    ResolvedFrameGraphTexture FrameGraphPassResources::GetTexture(FrameGraphTextureHandle handle) const noexcept
    {
        return frameGraph_.ResolvePassTexture(passIndex_, handle);
    }

    FrameGraphTextureHandle FrameGraph::CreateTexture(std::string name, FrameGraphTextureDesc desc)
    {
        VE_ASSERT_RENDER_THREAD();
        impl_->InvalidateCompile();

        const UInt32 index = static_cast<UInt32>(impl_->textures.size());
        Impl::TextureResourceNode resource = {};
        resource.name = std::move(name);
        resource.desc = desc;
        impl_->textures.push_back(std::move(resource));
        return FrameGraphTextureHandle{index, 0};
    }

    FrameGraphTextureHandle FrameGraph::ImportTexture(std::string name, FrameGraphTextureDesc desc, ImportedFrameGraphTexture importedTexture)
    {
        VE_ASSERT_RENDER_THREAD();
        impl_->InvalidateCompile();

        const UInt32 index = static_cast<UInt32>(impl_->textures.size());
        Impl::TextureResourceNode resource = {};
        resource.name = std::move(name);
        resource.desc = desc;
        resource.importedTexture = importedTexture;
        resource.imported = true;
        impl_->textures.push_back(std::move(resource));
        return FrameGraphTextureHandle{index, 0};
    }

    void FrameGraph::Export(FrameGraphTextureHandle handle)
    {
        VE_ASSERT_RENDER_THREAD();
        impl_->InvalidateCompile();
        impl_->exportedTextures.push_back(handle);
    }

    void FrameGraph::AddRasterPassInternal(std::string name, SetupFunction setupFunction, ExecuteFunction executeFunction)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(setupFunction != nullptr);
        VE_ASSERT(executeFunction != nullptr);
        impl_->InvalidateCompile();

        const rhi::RhiExtent2D extent =
            impl_->context.frameData.mainSwapchain != nullptr ? impl_->context.frameData.mainSwapchain->GetExtent() : rhi::RhiExtent2D{};
        Impl::PassNode pass = {};
        pass.name = std::move(name);
        pass.renderArea = rhi::RhiRenderArea{0, 0, extent.width, extent.height};
        pass.viewport = rhi::RhiViewport{0.0f, 0.0f, static_cast<Float32>(extent.width), static_cast<Float32>(extent.height), 0.0f, 1.0f};
        pass.scissorRect = rhi::RhiScissorRect{0, 0, extent.width, extent.height};
        pass.executeFunction = std::move(executeFunction);

        const UInt32 passIndex = static_cast<UInt32>(impl_->passes.size());
        impl_->passes.push_back(std::move(pass));
        FrameGraphBuilder builder(*this, passIndex);
        setupFunction(builder);
    }

    ResolvedFrameGraphTexture FrameGraph::ResolveTexture(FrameGraphTextureHandle handle) const noexcept
    {
        if (!impl_->IsHandleValid(handle))
        {
            return {};
        }

        const Impl::TextureResourceNode& resource = impl_->textures[handle.index];
        if (resource.imported)
        {
            return ResolvedFrameGraphTexture{resource.importedTexture.texture, resource.importedTexture.isSwapchain};
        }
        return ResolvedFrameGraphTexture{resource.physicalTexture.get(), false};
    }

    ResolvedFrameGraphTexture FrameGraph::ResolvePassTexture(UInt32 passIndex, FrameGraphTextureHandle handle) const noexcept
    {
        if (passIndex >= impl_->passes.size())
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "Frame graph resource resolution requires a valid pass index.");
            return {};
        }

        const Impl::PassNode& pass = impl_->passes[passIndex];
        const bool declared = std::any_of(pass.textureAccesses.begin(),
                                          pass.textureAccesses.end(),
                                          [handle](const TextureAccessRecord& access) { return access.input == handle || access.output == handle; });
        if (!declared)
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "Frame graph pass attempted to resolve an undeclared texture handle.");
            return {};
        }
        return ResolveTexture(handle);
    }

    FrameGraphTextureHandle FrameGraph::ReadTexture(UInt32 passIndex, FrameGraphTextureHandle handle, FrameGraphTextureAccess access)
    {
        impl_->InvalidateCompile();
        if (passIndex >= impl_->passes.size() || !impl_->IsHandleValid(handle))
        {
            impl_->AddBuildError(passIndex, "read uses an invalid texture handle.");
            return {};
        }

        TextureAccessRecord record = {};
        record.input = handle;
        record.output = handle;
        record.access = access;
        record.mode = TextureAccessMode::Read;
        impl_->passes[passIndex].textureAccesses.push_back(record);
        impl_->textures[handle.index].versions[handle.version].readers.push_back(passIndex);
        return handle;
    }

    FrameGraphTextureHandle FrameGraph::WriteTexture(UInt32 passIndex, FrameGraphTextureHandle handle, FrameGraphTextureAccess access)
    {
        impl_->InvalidateCompile();
        if (passIndex >= impl_->passes.size() || !impl_->IsHandleValid(handle))
        {
            impl_->AddBuildError(passIndex, "write uses an invalid texture handle.");
            return {};
        }

        Impl::TextureResourceNode& resource = impl_->textures[handle.index];
        if (handle.version + 1u != resource.versions.size())
        {
            impl_->AddBuildError(passIndex, "write must consume the latest texture version.");
            return {};
        }

        const UInt32 outputVersion = static_cast<UInt32>(resource.versions.size());
        Impl::ResourceVersion version = {};
        version.producer = passIndex;
        resource.versions.push_back(std::move(version));

        const FrameGraphTextureHandle output{handle.index, outputVersion};
        TextureAccessRecord record = {};
        record.input = handle;
        record.output = output;
        record.access = access;
        record.mode = TextureAccessMode::Write;
        impl_->passes[passIndex].textureAccesses.push_back(record);
        return output;
    }

    void FrameGraph::SetColorAttachment(
        UInt32 passIndex, FrameGraphTextureHandle handle, rhi::RhiLoadAction loadAction, rhi::RhiStoreAction storeAction, rhi::RhiColor clearColor)
    {
        impl_->InvalidateCompile();
        if (passIndex >= impl_->passes.size())
        {
            impl_->AddBuildError(passIndex, "color attachment targets an invalid pass.");
            return;
        }

        Impl::PassNode& pass = impl_->passes[passIndex];
        if (pass.colorAttachments.size() >= rhi::RhiMaxColorAttachments)
        {
            impl_->AddBuildError(passIndex, "color attachment count exceeds RhiMaxColorAttachments.");
            return;
        }

        pass.colorAttachments.push_back(ColorAttachmentRecord{handle, loadAction, storeAction, clearColor});
        if (impl_->IsHandleValid(handle))
        {
            const FrameGraphTextureDesc& desc = impl_->textures[handle.index].desc;
            pass.renderArea = rhi::RhiRenderArea{0, 0, desc.width, desc.height};
            pass.viewport = rhi::RhiViewport{0.0f, 0.0f, static_cast<Float32>(desc.width), static_cast<Float32>(desc.height), 0.0f, 1.0f};
            pass.scissorRect = rhi::RhiScissorRect{0, 0, desc.width, desc.height};
        }
    }

    void FrameGraph::SetDepthAttachment(UInt32 passIndex,
                                        FrameGraphTextureHandle handle,
                                        rhi::RhiLoadAction loadAction,
                                        rhi::RhiStoreAction storeAction,
                                        rhi::RhiDepthStencilClearValue clearValue,
                                        bool readOnly)
    {
        impl_->InvalidateCompile();
        if (passIndex >= impl_->passes.size())
        {
            impl_->AddBuildError(passIndex, "depth attachment targets an invalid pass.");
            return;
        }

        Impl::PassNode& pass = impl_->passes[passIndex];
        if (pass.depthAttachment.has_value())
        {
            impl_->AddBuildError(passIndex, "declares more than one depth attachment.");
            return;
        }
        pass.depthAttachment = DepthAttachmentRecord{handle, loadAction, storeAction, clearValue, readOnly};
    }

    void FrameGraph::SetRenderArea(UInt32 passIndex, const rhi::RhiRenderArea& renderArea) noexcept
    {
        impl_->InvalidateCompile();
        if (passIndex < impl_->passes.size())
        {
            impl_->passes[passIndex].renderArea = renderArea;
        }
    }

    void FrameGraph::SetViewport(UInt32 passIndex, const rhi::RhiViewport& viewport) noexcept
    {
        impl_->InvalidateCompile();
        if (passIndex < impl_->passes.size())
        {
            impl_->passes[passIndex].viewport = viewport;
        }
    }

    void FrameGraph::SetScissor(UInt32 passIndex, const rhi::RhiScissorRect& scissorRect) noexcept
    {
        impl_->InvalidateCompile();
        if (passIndex < impl_->passes.size())
        {
            impl_->passes[passIndex].scissorRect = scissorRect;
        }
    }

    void FrameGraph::SetSideEffect(UInt32 passIndex) noexcept
    {
        impl_->InvalidateCompile();
        if (passIndex < impl_->passes.size())
        {
            impl_->passes[passIndex].sideEffect = true;
        }
    }

    const RendererData& FrameGraph::GetRendererData() const noexcept
    {
        return impl_->context.rendererData;
    }

    Error FrameGraph::Compile()
    {
        VE_ASSERT_RENDER_THREAD();
        impl_->InvalidateCompile();

        if (!impl_->buildErrors.empty())
        {
            return Error(ErrorCode::InvalidArgument, impl_->buildErrors.front());
        }

        if (impl_->context.frameData.mainSwapchain == nullptr)
        {
            return Error(ErrorCode::InvalidState, "Frame graph requires the frame pipeline main swapchain.");
        }

        for (const Impl::TextureResourceNode& resource : impl_->textures)
        {
            if (!IsTextureDescValid(resource.desc))
            {
                return Error(ErrorCode::InvalidArgument, "Frame graph texture '" + resource.name + "' has an invalid physical descriptor.");
            }
            if (resource.imported && !resource.importedTexture.isSwapchain && resource.importedTexture.texture == nullptr)
            {
                return Error(ErrorCode::InvalidArgument, "Imported frame graph texture '" + resource.name + "' has no native texture.");
            }
            if (resource.importedTexture.isSwapchain && resource.importedTexture.texture != nullptr)
            {
                return Error(ErrorCode::InvalidArgument, "Swapchain frame graph texture '" + resource.name + "' must not carry an RhiTexture pointer.");
            }
            if (resource.imported && resource.importedTexture.texture != nullptr &&
                (resource.importedTexture.texture->GetDimension() != resource.desc.dimension ||
                 resource.importedTexture.texture->GetWidth() != resource.desc.width || resource.importedTexture.texture->GetHeight() != resource.desc.height ||
                 resource.importedTexture.texture->GetFormat() != resource.desc.format))
            {
                return Error(ErrorCode::InvalidArgument, "Imported frame graph texture '" + resource.name + "' does not match its descriptor.");
            }
        }

        for (FrameGraphTextureHandle exported : impl_->exportedTextures)
        {
            if (!impl_->IsHandleValid(exported))
            {
                return Error(ErrorCode::InvalidArgument, "Frame graph exports an invalid texture handle.");
            }
        }

        const SizeT passCount = impl_->passes.size();
        impl_->dependencies.assign(passCount, {});
        impl_->reverseDependencies.assign(passCount, {});

        const auto addDependency = [this](UInt32 before, UInt32 after)
        {
            if (before == InvalidPassIndex || before == after)
            {
                return;
            }
            impl_->dependencies[before].insert(after);
            impl_->reverseDependencies[after].insert(before);
        };

        for (UInt32 passIndex = 0; passIndex < impl_->passes.size(); ++passIndex)
        {
            const Impl::PassNode& pass = impl_->passes[passIndex];
            for (const TextureAccessRecord& access : pass.textureAccesses)
            {
                if (!impl_->IsHandleValid(access.input) || !impl_->IsHandleValid(access.output))
                {
                    return Error(ErrorCode::InvalidArgument, "Frame graph pass '" + pass.name + "' has an invalid texture access.");
                }

                const Impl::TextureResourceNode& resource = impl_->textures[access.input.index];
                const Impl::ResourceVersion& inputVersion = resource.versions[access.input.version];
                if (access.mode == TextureAccessMode::Read)
                {
                    if (!resource.imported && inputVersion.producer == InvalidPassIndex)
                    {
                        return Error(ErrorCode::InvalidState,
                                     "Frame graph pass '" + pass.name + "' reads uninitialized transient texture '" + resource.name + "'.");
                    }
                    addDependency(inputVersion.producer, passIndex);
                }
                else
                {
                    addDependency(inputVersion.producer, passIndex);
                    for (UInt32 reader : inputVersion.readers)
                    {
                        addDependency(reader, passIndex);
                    }
                }
            }

            for (const ColorAttachmentRecord& attachment : pass.colorAttachments)
            {
                const auto accessIt = std::find_if(pass.textureAccesses.begin(),
                                                   pass.textureAccesses.end(),
                                                   [&attachment](const TextureAccessRecord& access)
                                                   {
                                                       return access.mode == TextureAccessMode::Write &&
                                                              access.access == FrameGraphTextureAccess::ColorAttachment && access.output == attachment.handle;
                                                   });
                if (accessIt == pass.textureAccesses.end())
                {
                    return Error(ErrorCode::InvalidArgument, "Frame graph pass '" + pass.name + "' color attachment was not declared as a color write.");
                }
                const Impl::TextureResourceNode& resource = impl_->textures[attachment.handle.index];
                if (resource.desc.format == rhi::RhiFormat::Depth32Float)
                {
                    return Error(ErrorCode::InvalidArgument, "Frame graph pass '" + pass.name + "' uses a depth format as a color attachment.");
                }
                if (!resource.imported && resource.versions[accessIt->input.version].producer == InvalidPassIndex &&
                    attachment.loadAction == rhi::RhiLoadAction::Load)
                {
                    return Error(ErrorCode::InvalidState,
                                 "Frame graph pass '" + pass.name + "' loads uninitialized transient color texture '" + resource.name + "'.");
                }
            }

            if (pass.depthAttachment.has_value())
            {
                const DepthAttachmentRecord& attachment = *pass.depthAttachment;
                const TextureAccessMode requiredMode = attachment.readOnly ? TextureAccessMode::Read : TextureAccessMode::Write;
                const bool declared = std::any_of(
                    pass.textureAccesses.begin(),
                    pass.textureAccesses.end(),
                    [&attachment, requiredMode](const TextureAccessRecord& access)
                    {
                        const FrameGraphTextureHandle declaredHandle = requiredMode == TextureAccessMode::Read ? access.input : access.output;
                        return access.mode == requiredMode && access.access == FrameGraphTextureAccess::DepthAttachment && declaredHandle == attachment.handle;
                    });
                if (!declared)
                {
                    return Error(ErrorCode::InvalidArgument,
                                 "Frame graph pass '" + pass.name + "' depth attachment access does not match its read-only declaration.");
                }
                const Impl::TextureResourceNode& resource = impl_->textures[attachment.handle.index];
                if (resource.importedTexture.isSwapchain || resource.desc.format != rhi::RhiFormat::Depth32Float)
                {
                    return Error(ErrorCode::InvalidArgument, "Frame graph pass '" + pass.name + "' has an invalid depth attachment format.");
                }
                if (!attachment.readOnly && !resource.imported && attachment.loadAction == rhi::RhiLoadAction::Load)
                {
                    const auto accessIt = std::find_if(pass.textureAccesses.begin(),
                                                       pass.textureAccesses.end(),
                                                       [&attachment](const TextureAccessRecord& access)
                                                       { return access.mode == TextureAccessMode::Write && access.output == attachment.handle; });
                    VE_ASSERT(accessIt != pass.textureAccesses.end());
                    if (resource.versions[accessIt->input.version].producer == InvalidPassIndex)
                    {
                        return Error(ErrorCode::InvalidState,
                                     "Frame graph pass '" + pass.name + "' loads uninitialized transient depth texture '" + resource.name + "'.");
                    }
                }
            }
        }

        std::vector<UInt32> roots;
        for (UInt32 passIndex = 0; passIndex < impl_->passes.size(); ++passIndex)
        {
            if (impl_->passes[passIndex].sideEffect)
            {
                roots.push_back(passIndex);
            }
        }
        for (FrameGraphTextureHandle exported : impl_->exportedTextures)
        {
            const UInt32 producer = impl_->textures[exported.index].versions[exported.version].producer;
            if (producer != InvalidPassIndex)
            {
                roots.push_back(producer);
            }
        }

        std::vector<UInt32> retainStack = roots;
        while (!retainStack.empty())
        {
            const UInt32 passIndex = retainStack.back();
            retainStack.pop_back();
            Impl::PassNode& pass = impl_->passes[passIndex];
            if (pass.retained)
            {
                continue;
            }
            pass.retained = true;
            for (UInt32 dependency : impl_->reverseDependencies[passIndex])
            {
                retainStack.push_back(dependency);
            }
        }

        std::vector<UInt32> indegrees(passCount, 0);
        std::priority_queue<UInt32, std::vector<UInt32>, std::greater<>> ready;
        SizeT retainedCount = 0;
        for (UInt32 passIndex = 0; passIndex < impl_->passes.size(); ++passIndex)
        {
            if (!impl_->passes[passIndex].retained)
            {
                continue;
            }
            ++retainedCount;
            for (UInt32 dependency : impl_->reverseDependencies[passIndex])
            {
                if (impl_->passes[dependency].retained)
                {
                    ++indegrees[passIndex];
                }
            }
            if (indegrees[passIndex] == 0)
            {
                ready.push(passIndex);
            }
        }

        while (!ready.empty())
        {
            const UInt32 passIndex = ready.top();
            ready.pop();
            impl_->compiledPassOrder.push_back(passIndex);
            for (UInt32 dependent : impl_->dependencies[passIndex])
            {
                if (!impl_->passes[dependent].retained)
                {
                    continue;
                }
                VE_ASSERT(indegrees[dependent] != 0);
                --indegrees[dependent];
                if (indegrees[dependent] == 0)
                {
                    ready.push(dependent);
                }
            }
        }

        if (impl_->compiledPassOrder.size() != retainedCount)
        {
            std::ostringstream message;
            message << "Frame graph dependency cycle involves:";
            for (UInt32 passIndex = 0; passIndex < impl_->passes.size(); ++passIndex)
            {
                if (impl_->passes[passIndex].retained && indegrees[passIndex] != 0)
                {
                    message << " '" << impl_->passes[passIndex].name << "'";
                }
            }
            return Error(ErrorCode::InvalidState, message.str());
        }

        for (UInt32 orderIndex = 0; orderIndex < impl_->compiledPassOrder.size(); ++orderIndex)
        {
            const Impl::PassNode& pass = impl_->passes[impl_->compiledPassOrder[orderIndex]];
            for (const TextureAccessRecord& access : pass.textureAccesses)
            {
                Impl::TextureResourceNode& resource = impl_->textures[access.input.index];
                if (resource.imported)
                {
                    continue;
                }
                resource.firstUse = resource.firstUse == InvalidPassIndex ? orderIndex : std::min(resource.firstUse, orderIndex);
                resource.lastUse = resource.lastUse == InvalidPassIndex ? orderIndex : std::max(resource.lastUse, orderIndex);
            }
        }

        impl_->compiled = true;
        return Error();
    }

    ErrorCode FrameGraph::Execute()
    {
        VE_ASSERT_RENDER_THREAD();
        if (!impl_->compiled)
        {
            return ErrorCode::InvalidState;
        }

        FrameGraphTransientResourcePool& transientPool = impl_->context.frameData.GetFrameGraphTransientResourcePool();
        const auto releaseAllTextures = [this, &transientPool]()
        {
            for (Impl::TextureResourceNode& resource : impl_->textures)
            {
                if (!resource.imported && resource.physicalTexture != nullptr)
                {
                    transientPool.ReleaseTexture(resource.desc, std::move(resource.physicalTexture));
                }
            }
        };

        rhi::RhiCommandList& commandList = impl_->context.frameData.GetCommandList();
        for (UInt32 orderIndex = 0; orderIndex < impl_->compiledPassOrder.size(); ++orderIndex)
        {
            for (Impl::TextureResourceNode& resource : impl_->textures)
            {
                if (!resource.imported && resource.firstUse == orderIndex)
                {
                    resource.physicalTexture = transientPool.AcquireTexture(resource.desc, resource.name.c_str());
                    if (resource.physicalTexture == nullptr)
                    {
                        releaseAllTextures();
                        return ErrorCode::OutOfMemory;
                    }
                }
            }

            const UInt32 passIndex = impl_->compiledPassOrder[orderIndex];
            Impl::PassNode& pass = impl_->passes[passIndex];
            const FrameGraphPassResources passResources(*this, passIndex);
            RenderPassData passData = {};
            passData.renderPassDesc.debugName = pass.name.c_str();
            passData.renderPassDesc.renderArea = pass.renderArea;
            passData.viewport = pass.viewport;
            passData.scissorRect = pass.scissorRect;

            for (const ColorAttachmentRecord& attachment : pass.colorAttachments)
            {
                rhi::RhiRenderPassColorAttachmentDesc& rhiAttachment = passData.renderPassDesc.colorAttachments[passData.renderPassDesc.colorAttachmentCount];
                rhiAttachment.texture = ResolveTexture(attachment.handle).texture;
                rhiAttachment.loadAction = attachment.loadAction;
                rhiAttachment.storeAction = attachment.storeAction;
                rhiAttachment.clearColor = attachment.clearColor;
                ++passData.renderPassDesc.colorAttachmentCount;
            }

            if (pass.depthAttachment.has_value())
            {
                const DepthAttachmentRecord& attachment = *pass.depthAttachment;
                rhi::RhiRenderPassDepthStencilAttachmentDesc& rhiAttachment = passData.renderPassDesc.depthStencilAttachment;
                rhiAttachment.texture = ResolveTexture(attachment.handle).texture;
                rhiAttachment.depthLoadAction = attachment.loadAction;
                rhiAttachment.depthStoreAction = attachment.storeAction;
                rhiAttachment.clearValue = attachment.clearValue;
                passData.renderPassDesc.hasDepthStencilAttachment = true;
            }

            if (!commandList.BeginRenderPass(*impl_->context.frameData.mainSwapchain, passData.renderPassDesc))
            {
                releaseAllTextures();
                return ErrorCode::PlatformError;
            }

            commandList.SetViewport(passData.viewport);
            commandList.SetScissor(passData.scissorRect);
            RenderPassContext passContext(RenderPassContextInitParam{impl_->context.frameData, impl_->context.rendererData, passData});
            const ErrorCode passResult = pass.executeFunction(passResources, passContext);
            commandList.EndRenderPass();
            if (passResult != ErrorCode::None)
            {
                releaseAllTextures();
                return passResult;
            }

            for (Impl::TextureResourceNode& resource : impl_->textures)
            {
                if (!resource.imported && resource.lastUse == orderIndex && resource.physicalTexture != nullptr)
                {
                    transientPool.ReleaseTexture(resource.desc, std::move(resource.physicalTexture));
                }
            }
        }

        return ErrorCode::None;
    }
} // namespace ve
