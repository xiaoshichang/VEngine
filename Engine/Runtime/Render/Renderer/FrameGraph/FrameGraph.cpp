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
#include <variant>
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

        enum class FrameGraphStage
        {
            Initial,
            SettingUp,
            SetupComplete,
            Compiled,
            Executed,
            Failed,
        };

        struct TextureAccessRecord
        {
            FrameGraphTextureHandle input;
            FrameGraphTextureHandle output;
            FrameGraphTextureAccess access = FrameGraphTextureAccess::ShaderRead;
            TextureAccessMode mode = TextureAccessMode::Read;
        };

        struct BufferAccessRecord
        {
            FrameGraphBufferHandle input;
            FrameGraphBufferHandle output;
            FrameGraphBufferAccess access = FrameGraphBufferAccess::ShaderRead;
            TextureAccessMode mode = TextureAccessMode::Read;
        };

        struct ColorAttachmentRecord
        {
            FrameGraphTextureHandle handle;
            rhi::RhiLoadAction loadAction = rhi::RhiLoadAction::Load;
            rhi::RhiStoreAction storeAction = rhi::RhiStoreAction::DontCare;
            rhi::RhiColor clearColor = {};
        };

        struct DepthAttachmentRecord
        {
            FrameGraphTextureHandle handle;
            rhi::RhiLoadAction loadAction = rhi::RhiLoadAction::Load;
            rhi::RhiStoreAction storeAction = rhi::RhiStoreAction::DontCare;
            Float32 clearDepth = 1.0f;
            bool readOnly = false;
        };

        [[nodiscard]] bool IsTextureDescValid(const FrameGraphTextureDesc& desc) noexcept
        {
            return desc.width != 0 && desc.height != 0 && desc.depth != 0 && desc.mipLevelCount != 0;
        }

    } // namespace

    struct FrameGraph::Impl
    {
        struct TransientTextureBacking
        {
            std::unique_ptr<rhi::RhiTexture> texture;
        };

        using TextureBacking = std::variant<TransientTextureBacking, ImportedFrameGraphTexture>;

        struct ResourceVersion
        {
            UInt32 producer = InvalidPassIndex;
            std::vector<UInt32> readers;
        };

        struct TextureResourceNode
        {
            std::string name;
            FrameGraphTextureDesc desc = {};
            TextureBacking backing = TransientTextureBacking{};
            std::vector<ResourceVersion> versions{ResourceVersion{}};
            UInt32 firstUse = InvalidPassIndex;
            UInt32 lastUse = InvalidPassIndex;

            [[nodiscard]] bool IsImported() const noexcept
            {
                return std::holds_alternative<ImportedFrameGraphTexture>(backing);
            }

            [[nodiscard]] const ImportedFrameGraphTexture* GetImportedBacking() const noexcept
            {
                return std::get_if<ImportedFrameGraphTexture>(&backing);
            }

            [[nodiscard]] TransientTextureBacking* GetTransientBacking() noexcept
            {
                return std::get_if<TransientTextureBacking>(&backing);
            }

            [[nodiscard]] const TransientTextureBacking* GetTransientBacking() const noexcept
            {
                return std::get_if<TransientTextureBacking>(&backing);
            }
        };

        struct BufferResourceNode
        {
            std::string name;
            ImportedFrameGraphBuffer backing = {};
            std::vector<ResourceVersion> versions{ResourceVersion{}};
        };

        struct PassNode
        {
            std::string name;
            std::vector<TextureAccessRecord> textureAccesses;
            std::vector<BufferAccessRecord> bufferAccesses;
            std::optional<ColorAttachmentRecord> colorAttachment;
            std::optional<DepthAttachmentRecord> depthAttachment;
            rhi::RhiRenderArea renderArea = {};
            rhi::RhiViewport viewport = {};
            rhi::RhiScissorRect scissorRect = {};
            ExecuteFunction executeFunction;
            bool raster = true;
            bool sideEffect = false;
            bool retained = false;
        };

        explicit Impl(FrameGraphExecuteContext inContext)
            : context(inContext)
        {
        }

        void ResetCompileResults() noexcept
        {
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

        [[nodiscard]] bool IsSettingUp() const noexcept
        {
            return stage == FrameGraphStage::SettingUp;
        }

        [[nodiscard]] bool IsHandleValid(FrameGraphTextureHandle handle) const noexcept
        {
            return handle.IsValid() && handle.index < textures.size() && handle.version < textures[handle.index].versions.size();
        }

        [[nodiscard]] bool IsHandleValid(FrameGraphBufferHandle handle) const noexcept
        {
            return handle.IsValid() && handle.index < buffers.size() && handle.version < buffers[handle.index].versions.size();
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

        [[nodiscard]] Error ValidateResourceDeclarations() const;
        [[nodiscard]] Error BuildPassDependencies();
        [[nodiscard]] Error ValidatePassDeclarations() const;
        void CullUnusedPasses();
        [[nodiscard]] Error BuildExecutionOrder();
        void InferAttachmentStoreActions();
        void AnalyzeTransientResourceLifetimes();

        [[nodiscard]] ResolvedFrameGraphTexture ResolveTexture(FrameGraphTextureHandle handle) const noexcept;
        [[nodiscard]] ErrorCode AcquirePassTextures(UInt32 orderIndex, FrameGraphTransientResourcePool& transientPool);
        [[nodiscard]] rhi::RhiRenderPassBeginInfo BuildRenderPassBeginInfo(const PassNode& pass) const;
        [[nodiscard]] RenderPassExecutionInfo BuildRenderPassExecutionInfo(const PassNode& pass) const;
        void ReleasePassTextures(UInt32 orderIndex, FrameGraphTransientResourcePool& transientPool);
        void ReleaseAllTextures(FrameGraphTransientResourcePool& transientPool);

        FrameGraphExecuteContext context;
        std::vector<TextureResourceNode> textures;
        std::vector<BufferResourceNode> buffers;
        std::vector<PassNode> passes;
        std::vector<FrameGraphTextureHandle> exportedTextures;
        std::vector<FrameGraphBufferHandle> exportedBuffers;
        std::vector<std::string> buildErrors;
        std::vector<std::unordered_set<UInt32>> dependencies;
        std::vector<std::unordered_set<UInt32>> reverseDependencies;
        std::vector<UInt32> compiledPassOrder;
        FrameGraphStage stage = FrameGraphStage::Initial;
    };

    FrameGraph::FrameGraph(FrameGraphExecuteContext context)
        : impl_(std::make_unique<Impl>(context))
    {
        VE_ASSERT_RENDER_THREAD();
    }

    FrameGraph::~FrameGraph() = default;

    ErrorCode FrameGraph::Setup(GraphSetupFunction setupFunction)
    {
        VE_ASSERT_RENDER_THREAD();

        // Step 1: verify that graph declaration starts exactly once and has a valid renderer callback.
        if (impl_->stage != FrameGraphStage::Initial)
        {
            return ErrorCode::InvalidState;
        }
        if (setupFunction == nullptr)
        {
            return ErrorCode::InvalidArgument;
        }

        // Step 2: open the declaration window. Resource and pass declarations are only legal inside this callback.
        impl_->stage = FrameGraphStage::SettingUp;
        const ErrorCode setupResult = setupFunction(*this);
        if (setupResult != ErrorCode::None)
        {
            impl_->stage = FrameGraphStage::Failed;
            return setupResult;
        }

        // Step 3: freeze the declared graph so Compile can validate and transform an immutable setup result.
        impl_->stage = FrameGraphStage::SetupComplete;
        return ErrorCode::None;
    }

    FrameGraphPassResources::FrameGraphPassResources(const FrameGraph& frameGraph, UInt32 passIndex) noexcept
        : frameGraph_(frameGraph)
        , passIndex_(passIndex)
    {
    }

    ResolvedFrameGraphTexture FrameGraphPassResources::GetTexture(FrameGraphTextureHandle handle) const noexcept
    {
        return frameGraph_.ResolvePassTexture(passIndex_, handle);
    }

    ResolvedFrameGraphBuffer FrameGraphPassResources::GetBuffer(FrameGraphBufferHandle handle) const noexcept
    {
        return frameGraph_.ResolvePassBuffer(passIndex_, handle);
    }

    FrameGraphTextureHandle FrameGraph::CreateTexture(std::string name, FrameGraphTextureDesc desc)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "FrameGraph::CreateTexture is only valid during Setup.");
        if (!impl_->IsSettingUp())
        {
            return {};
        }

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
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "FrameGraph::ImportTexture is only valid during Setup.");
        if (!impl_->IsSettingUp())
        {
            return {};
        }

        const UInt32 index = static_cast<UInt32>(impl_->textures.size());
        Impl::TextureResourceNode resource = {};
        resource.name = std::move(name);
        resource.desc = desc;
        resource.backing = importedTexture;
        impl_->textures.push_back(std::move(resource));
        return FrameGraphTextureHandle{index, 0};
    }

    FrameGraphBufferHandle FrameGraph::ImportBuffer(std::string name, ImportedFrameGraphBuffer importedBuffer)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "FrameGraph::ImportBuffer is only valid during Setup.");
        if (!impl_->IsSettingUp())
        {
            return {};
        }

        const UInt32 index = static_cast<UInt32>(impl_->buffers.size());
        Impl::BufferResourceNode resource = {};
        resource.name = std::move(name);
        resource.backing = importedBuffer;
        impl_->buffers.push_back(std::move(resource));
        return FrameGraphBufferHandle{index, 0};
    }

    void FrameGraph::Export(FrameGraphTextureHandle handle)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "FrameGraph::Export is only valid during Setup.");
        if (!impl_->IsSettingUp())
        {
            return;
        }
        impl_->exportedTextures.push_back(handle);
    }

    void FrameGraph::Export(FrameGraphBufferHandle handle)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "FrameGraph::Export is only valid during Setup.");
        if (!impl_->IsSettingUp())
        {
            return;
        }
        impl_->exportedBuffers.push_back(handle);
    }

    void FrameGraph::AddRasterPassInternal(std::string name, PassSetupFunction setupFunction, ExecuteFunction executeFunction)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(setupFunction != nullptr);
        VE_ASSERT(executeFunction != nullptr);
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "FrameGraph::AddRasterPass is only valid during Setup.");
        if (!impl_->IsSettingUp())
        {
            return;
        }

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

    void FrameGraph::AddComputePassInternal(std::string name, PassSetupFunction setupFunction, ExecuteFunction executeFunction)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(setupFunction != nullptr);
        VE_ASSERT(executeFunction != nullptr);
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "FrameGraph::AddComputePass is only valid during Setup.");
        if (!impl_->IsSettingUp())
        {
            return;
        }

        Impl::PassNode pass = {};
        pass.name = std::move(name);
        pass.executeFunction = std::move(executeFunction);
        pass.raster = false;

        const UInt32 passIndex = static_cast<UInt32>(impl_->passes.size());
        impl_->passes.push_back(std::move(pass));
        FrameGraphBuilder builder(*this, passIndex);
        setupFunction(builder);
    }

    ResolvedFrameGraphTexture FrameGraph::ResolveTexture(FrameGraphTextureHandle handle) const noexcept
    {
        return impl_->ResolveTexture(handle);
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

    ResolvedFrameGraphBuffer FrameGraph::ResolveBuffer(FrameGraphBufferHandle handle) const noexcept
    {
        if (!impl_->IsHandleValid(handle))
        {
            return {};
        }
        return ResolvedFrameGraphBuffer{impl_->buffers[handle.index].backing.buffer};
    }

    ResolvedFrameGraphBuffer FrameGraph::ResolvePassBuffer(UInt32 passIndex, FrameGraphBufferHandle handle) const noexcept
    {
        if (passIndex >= impl_->passes.size())
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "Frame graph resource resolution requires a valid pass index.");
            return {};
        }

        const Impl::PassNode& pass = impl_->passes[passIndex];
        const bool declared = std::any_of(pass.bufferAccesses.begin(),
                                          pass.bufferAccesses.end(),
                                          [handle](const BufferAccessRecord& access) { return access.input == handle || access.output == handle; });
        if (!declared)
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "Frame graph pass attempted to resolve an undeclared buffer handle.");
            return {};
        }
        return ResolveBuffer(handle);
    }

    FrameGraphTextureHandle FrameGraph::ReadTexture(UInt32 passIndex, FrameGraphTextureHandle handle, FrameGraphTextureAccess access)
    {
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "Frame graph resource reads are only valid during Setup.");
        if (!impl_->IsSettingUp())
        {
            return {};
        }
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
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "Frame graph resource writes are only valid during Setup.");
        if (!impl_->IsSettingUp())
        {
            return {};
        }
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

    FrameGraphBufferHandle FrameGraph::ReadBuffer(UInt32 passIndex, FrameGraphBufferHandle handle, FrameGraphBufferAccess access)
    {
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "Frame graph resource reads are only valid during Setup.");
        if (!impl_->IsSettingUp())
        {
            return {};
        }
        if (passIndex >= impl_->passes.size() || !impl_->IsHandleValid(handle))
        {
            impl_->AddBuildError(passIndex, "read uses an invalid buffer handle.");
            return {};
        }

        BufferAccessRecord record = {};
        record.input = handle;
        record.output = handle;
        record.access = access;
        record.mode = TextureAccessMode::Read;
        impl_->passes[passIndex].bufferAccesses.push_back(record);
        impl_->buffers[handle.index].versions[handle.version].readers.push_back(passIndex);
        return handle;
    }

    FrameGraphBufferHandle FrameGraph::WriteBuffer(UInt32 passIndex, FrameGraphBufferHandle handle, FrameGraphBufferAccess access)
    {
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "Frame graph resource writes are only valid during Setup.");
        if (!impl_->IsSettingUp())
        {
            return {};
        }
        if (passIndex >= impl_->passes.size() || !impl_->IsHandleValid(handle))
        {
            impl_->AddBuildError(passIndex, "write uses an invalid buffer handle.");
            return {};
        }

        Impl::BufferResourceNode& resource = impl_->buffers[handle.index];
        if (handle.version + 1u != resource.versions.size())
        {
            impl_->AddBuildError(passIndex, "write must consume the latest buffer version.");
            return {};
        }

        const UInt32 outputVersion = static_cast<UInt32>(resource.versions.size());
        Impl::ResourceVersion version = {};
        version.producer = passIndex;
        resource.versions.push_back(std::move(version));

        const FrameGraphBufferHandle output{handle.index, outputVersion};
        BufferAccessRecord record = {};
        record.input = handle;
        record.output = output;
        record.access = access;
        record.mode = TextureAccessMode::Write;
        impl_->passes[passIndex].bufferAccesses.push_back(record);
        return output;
    }

    FrameGraphTextureHandle
    FrameGraph::WriteColorAttachment(UInt32 passIndex, FrameGraphTextureHandle handle, rhi::RhiLoadAction loadAction, rhi::RhiColor clearColor)
    {
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "Frame graph attachments are only valid during Setup.");
        if (!impl_->IsSettingUp())
        {
            return {};
        }
        if (passIndex >= impl_->passes.size())
        {
            impl_->AddBuildError(passIndex, "color attachment targets an invalid pass.");
            return {};
        }

        Impl::PassNode& pass = impl_->passes[passIndex];
        if (pass.colorAttachment.has_value())
        {
            impl_->AddBuildError(passIndex, "declares more than one color attachment.");
            return {};
        }

        const FrameGraphTextureHandle output = WriteTexture(passIndex, handle, FrameGraphTextureAccess::ColorAttachment);
        if (!output.IsValid())
        {
            return {};
        }

        pass.colorAttachment = ColorAttachmentRecord{output, loadAction, rhi::RhiStoreAction::DontCare, clearColor};
        if (impl_->IsHandleValid(output))
        {
            const FrameGraphTextureDesc& desc = impl_->textures[output.index].desc;
            pass.renderArea = rhi::RhiRenderArea{0, 0, desc.width, desc.height};
            pass.viewport = rhi::RhiViewport{0.0f, 0.0f, static_cast<Float32>(desc.width), static_cast<Float32>(desc.height), 0.0f, 1.0f};
            pass.scissorRect = rhi::RhiScissorRect{0, 0, desc.width, desc.height};
        }
        return output;
    }

    FrameGraphTextureHandle
    FrameGraph::WriteDepthAttachment(UInt32 passIndex, FrameGraphTextureHandle handle, rhi::RhiLoadAction loadAction, Float32 clearDepth)
    {
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "Frame graph attachments are only valid during Setup.");
        if (!impl_->IsSettingUp())
        {
            return {};
        }
        if (passIndex >= impl_->passes.size())
        {
            impl_->AddBuildError(passIndex, "depth attachment targets an invalid pass.");
            return {};
        }

        Impl::PassNode& pass = impl_->passes[passIndex];
        if (pass.depthAttachment.has_value())
        {
            impl_->AddBuildError(passIndex, "declares more than one depth attachment.");
            return {};
        }

        const FrameGraphTextureHandle output = WriteTexture(passIndex, handle, FrameGraphTextureAccess::DepthAttachment);
        if (!output.IsValid())
        {
            return {};
        }
        pass.depthAttachment = DepthAttachmentRecord{output, loadAction, rhi::RhiStoreAction::DontCare, clearDepth, false};
        if (!pass.colorAttachment.has_value())
        {
            const FrameGraphTextureDesc& desc = impl_->textures[output.index].desc;
            pass.renderArea = rhi::RhiRenderArea{0, 0, desc.width, desc.height};
            pass.viewport = rhi::RhiViewport{0.0f, 0.0f, static_cast<Float32>(desc.width), static_cast<Float32>(desc.height), 0.0f, 1.0f};
            pass.scissorRect = rhi::RhiScissorRect{0, 0, desc.width, desc.height};
        }
        return output;
    }

    FrameGraphTextureHandle FrameGraph::ReadDepthAttachment(UInt32 passIndex, FrameGraphTextureHandle handle)
    {
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "Frame graph attachments are only valid during Setup.");
        if (!impl_->IsSettingUp())
        {
            return {};
        }
        if (passIndex >= impl_->passes.size())
        {
            impl_->AddBuildError(passIndex, "depth attachment targets an invalid pass.");
            return {};
        }

        Impl::PassNode& pass = impl_->passes[passIndex];
        if (pass.depthAttachment.has_value())
        {
            impl_->AddBuildError(passIndex, "declares more than one depth attachment.");
            return {};
        }

        const FrameGraphTextureHandle input = ReadTexture(passIndex, handle, FrameGraphTextureAccess::DepthAttachment);
        if (!input.IsValid())
        {
            return {};
        }
        pass.depthAttachment = DepthAttachmentRecord{input, rhi::RhiLoadAction::Load, rhi::RhiStoreAction::DontCare, 1.0f, true};
        if (!pass.colorAttachment.has_value())
        {
            const FrameGraphTextureDesc& desc = impl_->textures[input.index].desc;
            pass.renderArea = rhi::RhiRenderArea{0, 0, desc.width, desc.height};
            pass.viewport = rhi::RhiViewport{0.0f, 0.0f, static_cast<Float32>(desc.width), static_cast<Float32>(desc.height), 0.0f, 1.0f};
            pass.scissorRect = rhi::RhiScissorRect{0, 0, desc.width, desc.height};
        }
        return input;
    }

    void FrameGraph::SetRenderArea(UInt32 passIndex, const rhi::RhiRenderArea& renderArea) noexcept
    {
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "Frame graph pass state is only configurable during Setup.");
        if (impl_->IsSettingUp() && passIndex < impl_->passes.size())
        {
            impl_->passes[passIndex].renderArea = renderArea;
        }
    }

    void FrameGraph::SetViewport(UInt32 passIndex, const rhi::RhiViewport& viewport) noexcept
    {
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "Frame graph pass state is only configurable during Setup.");
        if (impl_->IsSettingUp() && passIndex < impl_->passes.size())
        {
            impl_->passes[passIndex].viewport = viewport;
        }
    }

    void FrameGraph::SetScissor(UInt32 passIndex, const rhi::RhiScissorRect& scissorRect) noexcept
    {
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "Frame graph pass state is only configurable during Setup.");
        if (impl_->IsSettingUp() && passIndex < impl_->passes.size())
        {
            impl_->passes[passIndex].scissorRect = scissorRect;
        }
    }

    void FrameGraph::SetSideEffect(UInt32 passIndex) noexcept
    {
        VE_ASSERT_MESSAGE(impl_->IsSettingUp(), "Frame graph pass state is only configurable during Setup.");
        if (impl_->IsSettingUp() && passIndex < impl_->passes.size())
        {
            impl_->passes[passIndex].sideEffect = true;
        }
    }

    const RendererData& FrameGraph::GetRendererData() const noexcept
    {
        return impl_->context.rendererData;
    }

    Error FrameGraph::Impl::ValidateResourceDeclarations() const
    {
        if (!buildErrors.empty())
        {
            return Error(ErrorCode::InvalidArgument, buildErrors.front());
        }

        if (context.frameData.mainSwapchain == nullptr)
        {
            return Error(ErrorCode::InvalidState, "Frame graph requires the frame pipeline main swapchain.");
        }

        for (const TextureResourceNode& resource : textures)
        {
            if (!IsTextureDescValid(resource.desc))
            {
                return Error(ErrorCode::InvalidArgument, "Frame graph texture '" + resource.name + "' has an invalid physical descriptor.");
            }

            const ImportedFrameGraphTexture* importedBacking = resource.GetImportedBacking();
            if (importedBacking == nullptr)
            {
                continue;
            }
            if (!importedBacking->isSwapchain && importedBacking->texture == nullptr)
            {
                return Error(ErrorCode::InvalidArgument, "Imported frame graph texture '" + resource.name + "' has no native texture.");
            }
            if (importedBacking->isSwapchain && importedBacking->texture != nullptr)
            {
                return Error(ErrorCode::InvalidArgument, "Swapchain frame graph texture '" + resource.name + "' must not carry an RhiTexture pointer.");
            }
            if (importedBacking->texture != nullptr &&
                (importedBacking->texture->GetDimension() != resource.desc.dimension || importedBacking->texture->GetWidth() != resource.desc.width ||
                 importedBacking->texture->GetHeight() != resource.desc.height || importedBacking->texture->GetFormat() != resource.desc.format))
            {
                return Error(ErrorCode::InvalidArgument, "Imported frame graph texture '" + resource.name + "' does not match its descriptor.");
            }
        }

        for (FrameGraphTextureHandle exported : exportedTextures)
        {
            if (!IsHandleValid(exported))
            {
                return Error(ErrorCode::InvalidArgument, "Frame graph exports an invalid texture handle.");
            }
        }

        for (const BufferResourceNode& resource : buffers)
        {
            if (resource.backing.buffer == nullptr)
            {
                return Error(ErrorCode::InvalidArgument, "Imported frame graph buffer '" + resource.name + "' has no native buffer.");
            }
        }

        for (FrameGraphBufferHandle exported : exportedBuffers)
        {
            if (!IsHandleValid(exported))
            {
                return Error(ErrorCode::InvalidArgument, "Frame graph exports an invalid buffer handle.");
            }
        }

        return Error();
    }

    Error FrameGraph::Impl::BuildPassDependencies()
    {
        const SizeT passCount = passes.size();
        dependencies.assign(passCount, {});
        reverseDependencies.assign(passCount, {});

        const auto addDependency = [this](UInt32 before, UInt32 after)
        {
            if (before == InvalidPassIndex || before == after)
            {
                return;
            }
            dependencies[before].insert(after);
            reverseDependencies[after].insert(before);
        };

        for (UInt32 passIndex = 0; passIndex < passes.size(); ++passIndex)
        {
            const PassNode& pass = passes[passIndex];
            for (const TextureAccessRecord& access : pass.textureAccesses)
            {
                if (!IsHandleValid(access.input) || !IsHandleValid(access.output))
                {
                    return Error(ErrorCode::InvalidArgument, "Frame graph pass '" + pass.name + "' has an invalid texture access.");
                }

                const TextureResourceNode& resource = textures[access.input.index];
                const ResourceVersion& inputVersion = resource.versions[access.input.version];
                if (access.mode == TextureAccessMode::Read)
                {
                    if (!resource.IsImported() && inputVersion.producer == InvalidPassIndex)
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

            for (const BufferAccessRecord& access : pass.bufferAccesses)
            {
                if (!IsHandleValid(access.input) || !IsHandleValid(access.output))
                {
                    return Error(ErrorCode::InvalidArgument, "Frame graph pass '" + pass.name + "' has an invalid buffer access.");
                }

                const BufferResourceNode& resource = buffers[access.input.index];
                const ResourceVersion& inputVersion = resource.versions[access.input.version];
                if (access.mode == TextureAccessMode::Read)
                {
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
        }

        return Error();
    }

    Error FrameGraph::Impl::ValidatePassDeclarations() const
    {
        for (const PassNode& pass : passes)
        {
            if (!pass.raster)
            {
                if (pass.colorAttachment.has_value() || pass.depthAttachment.has_value())
                {
                    return Error(ErrorCode::InvalidArgument, "Frame graph compute pass '" + pass.name + "' cannot declare raster attachments.");
                }
                continue;
            }

            if (!pass.colorAttachment.has_value() && !pass.depthAttachment.has_value())
            {
                return Error(ErrorCode::InvalidArgument, "Frame graph raster pass '" + pass.name + "' requires at least one attachment.");
            }

            const TextureResourceNode* colorResource = nullptr;
            if (pass.colorAttachment.has_value())
            {
                const ColorAttachmentRecord& colorAttachment = *pass.colorAttachment;
                colorResource = &textures[colorAttachment.handle.index];
                if (colorResource->desc.format == rhi::RhiFormat::Depth32Float)
                {
                    return Error(ErrorCode::InvalidArgument, "Frame graph pass '" + pass.name + "' uses a depth format as a color attachment.");
                }

                const auto colorAccessIt = std::find_if(pass.textureAccesses.begin(),
                                                        pass.textureAccesses.end(),
                                                        [&colorAttachment](const TextureAccessRecord& access)
                                                        {
                                                            return access.mode == TextureAccessMode::Write &&
                                                                   access.access == FrameGraphTextureAccess::ColorAttachment &&
                                                                   access.output == colorAttachment.handle;
                                                        });
                if (colorAccessIt == pass.textureAccesses.end())
                {
                    return Error(ErrorCode::InvalidState, "Frame graph pass '" + pass.name + "' has an inconsistent color attachment declaration.");
                }
                if (!colorResource->IsImported() && colorResource->versions[colorAccessIt->input.version].producer == InvalidPassIndex &&
                    colorAttachment.loadAction == rhi::RhiLoadAction::Load)
                {
                    return Error(ErrorCode::InvalidState,
                                 "Frame graph pass '" + pass.name + "' loads uninitialized transient color texture '" + colorResource->name + "'.");
                }
            }

            if (pass.depthAttachment.has_value())
            {
                const DepthAttachmentRecord& depthAttachment = *pass.depthAttachment;
                const TextureResourceNode& depthResource = textures[depthAttachment.handle.index];
                const ImportedFrameGraphTexture* depthImportedBacking = depthResource.GetImportedBacking();
                if ((depthImportedBacking != nullptr && depthImportedBacking->isSwapchain) || depthResource.desc.format != rhi::RhiFormat::Depth32Float)
                {
                    return Error(ErrorCode::InvalidArgument, "Frame graph pass '" + pass.name + "' has an invalid depth attachment format.");
                }
                if (colorResource != nullptr &&
                    (depthResource.desc.width != colorResource->desc.width || depthResource.desc.height != colorResource->desc.height))
                {
                    return Error(ErrorCode::InvalidArgument, "Frame graph pass '" + pass.name + "' color and depth attachment extents do not match.");
                }

                const TextureAccessMode depthAccessMode = depthAttachment.readOnly ? TextureAccessMode::Read : TextureAccessMode::Write;
                const auto depthAccessIt =
                    std::find_if(pass.textureAccesses.begin(),
                                 pass.textureAccesses.end(),
                                 [&depthAttachment, depthAccessMode](const TextureAccessRecord& access)
                                 {
                                     const FrameGraphTextureHandle declaredHandle = depthAccessMode == TextureAccessMode::Read ? access.input : access.output;
                                     return access.mode == depthAccessMode && access.access == FrameGraphTextureAccess::DepthAttachment &&
                                            declaredHandle == depthAttachment.handle;
                                 });
                if (depthAccessIt == pass.textureAccesses.end())
                {
                    return Error(ErrorCode::InvalidState, "Frame graph pass '" + pass.name + "' has an inconsistent depth attachment declaration.");
                }

                if (!depthAttachment.readOnly && !depthResource.IsImported() && depthAttachment.loadAction == rhi::RhiLoadAction::Load)
                {
                    if (depthResource.versions[depthAccessIt->input.version].producer == InvalidPassIndex)
                    {
                        return Error(ErrorCode::InvalidState,
                                     "Frame graph pass '" + pass.name + "' loads uninitialized transient depth texture '" + depthResource.name + "'.");
                    }
                }
            }
        }

        return Error();
    }

    void FrameGraph::Impl::CullUnusedPasses()
    {
        std::vector<UInt32> roots;
        for (UInt32 passIndex = 0; passIndex < passes.size(); ++passIndex)
        {
            if (passes[passIndex].sideEffect)
            {
                roots.push_back(passIndex);
            }
        }
        for (FrameGraphTextureHandle exported : exportedTextures)
        {
            const UInt32 producer = textures[exported.index].versions[exported.version].producer;
            if (producer != InvalidPassIndex)
            {
                roots.push_back(producer);
            }
        }
        for (FrameGraphBufferHandle exported : exportedBuffers)
        {
            const UInt32 producer = buffers[exported.index].versions[exported.version].producer;
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

            PassNode& pass = passes[passIndex];
            if (pass.retained)
            {
                continue;
            }

            pass.retained = true;
            for (UInt32 dependency : reverseDependencies[passIndex])
            {
                retainStack.push_back(dependency);
            }
        }
    }

    Error FrameGraph::Impl::BuildExecutionOrder()
    {
        const SizeT passCount = passes.size();
        std::vector<UInt32> indegrees(passCount, 0);
        std::priority_queue<UInt32, std::vector<UInt32>, std::greater<>> ready;
        SizeT retainedCount = 0;

        for (UInt32 passIndex = 0; passIndex < passes.size(); ++passIndex)
        {
            if (!passes[passIndex].retained)
            {
                continue;
            }

            ++retainedCount;
            for (UInt32 dependency : reverseDependencies[passIndex])
            {
                if (passes[dependency].retained)
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
            compiledPassOrder.push_back(passIndex);

            for (UInt32 dependent : dependencies[passIndex])
            {
                if (!passes[dependent].retained)
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

        if (compiledPassOrder.size() == retainedCount)
        {
            return Error();
        }

        std::ostringstream message;
        message << "Frame graph dependency cycle involves:";
        for (UInt32 passIndex = 0; passIndex < passes.size(); ++passIndex)
        {
            if (passes[passIndex].retained && indegrees[passIndex] != 0)
            {
                message << " '" << passes[passIndex].name << "'";
            }
        }
        return Error(ErrorCode::InvalidState, message.str());
    }

    void FrameGraph::Impl::InferAttachmentStoreActions()
    {
        const auto isNeededAfterPass = [this](FrameGraphTextureHandle handle, UInt32 orderIndex)
        {
            if (std::find(exportedTextures.begin(), exportedTextures.end(), handle) != exportedTextures.end())
            {
                return true;
            }

            for (UInt32 laterOrderIndex = orderIndex + 1; laterOrderIndex < compiledPassOrder.size(); ++laterOrderIndex)
            {
                const PassNode& laterPass = passes[compiledPassOrder[laterOrderIndex]];
                const bool consumed = std::any_of(laterPass.textureAccesses.begin(),
                                                  laterPass.textureAccesses.end(),
                                                  [handle](const TextureAccessRecord& access) { return access.input == handle; });
                if (consumed)
                {
                    return true;
                }
            }
            return false;
        };

        for (UInt32 orderIndex = 0; orderIndex < compiledPassOrder.size(); ++orderIndex)
        {
            PassNode& pass = passes[compiledPassOrder[orderIndex]];
            if (pass.colorAttachment.has_value())
            {
                pass.colorAttachment->storeAction =
                    isNeededAfterPass(pass.colorAttachment->handle, orderIndex) ? rhi::RhiStoreAction::Store : rhi::RhiStoreAction::DontCare;
            }
            if (pass.depthAttachment.has_value())
            {
                pass.depthAttachment->storeAction =
                    isNeededAfterPass(pass.depthAttachment->handle, orderIndex) ? rhi::RhiStoreAction::Store : rhi::RhiStoreAction::DontCare;
            }
        }
    }

    void FrameGraph::Impl::AnalyzeTransientResourceLifetimes()
    {
        for (UInt32 orderIndex = 0; orderIndex < compiledPassOrder.size(); ++orderIndex)
        {
            const PassNode& pass = passes[compiledPassOrder[orderIndex]];
            for (const TextureAccessRecord& access : pass.textureAccesses)
            {
                TextureResourceNode& resource = textures[access.input.index];
                if (resource.IsImported())
                {
                    continue;
                }

                resource.firstUse = resource.firstUse == InvalidPassIndex ? orderIndex : std::min(resource.firstUse, orderIndex);
                resource.lastUse = resource.lastUse == InvalidPassIndex ? orderIndex : std::max(resource.lastUse, orderIndex);
            }
        }
    }

    ResolvedFrameGraphTexture FrameGraph::Impl::ResolveTexture(FrameGraphTextureHandle handle) const noexcept
    {
        if (!IsHandleValid(handle))
        {
            return {};
        }

        const TextureResourceNode& resource = textures[handle.index];
        if (const ImportedFrameGraphTexture* importedBacking = resource.GetImportedBacking(); importedBacking != nullptr)
        {
            return ResolvedFrameGraphTexture{importedBacking->texture, importedBacking->isSwapchain};
        }

        const TransientTextureBacking* transientBacking = resource.GetTransientBacking();
        VE_ASSERT(transientBacking != nullptr);
        return ResolvedFrameGraphTexture{transientBacking->texture.get(), false};
    }

    ErrorCode FrameGraph::Impl::AcquirePassTextures(UInt32 orderIndex, FrameGraphTransientResourcePool& transientPool)
    {
        for (TextureResourceNode& resource : textures)
        {
            TransientTextureBacking* transientBacking = resource.GetTransientBacking();
            if (transientBacking == nullptr || resource.firstUse != orderIndex)
            {
                continue;
            }

            transientBacking->texture = transientPool.AcquireTexture(resource.desc, resource.name.c_str());
            if (transientBacking->texture == nullptr)
            {
                return ErrorCode::OutOfMemory;
            }
        }
        return ErrorCode::None;
    }

    rhi::RhiRenderPassBeginInfo FrameGraph::Impl::BuildRenderPassBeginInfo(const PassNode& pass) const
    {
        rhi::RhiRenderPassBeginInfo beginInfo = {};
        beginInfo.debugName = pass.name.c_str();
        beginInfo.hasColorAttachment = pass.colorAttachment.has_value();
        beginInfo.colorAttachmentIsSwapchain = false;
        if (pass.colorAttachment.has_value())
        {
            const ColorAttachmentRecord& colorAttachment = *pass.colorAttachment;
            const ResolvedFrameGraphTexture resolvedColor = ResolveTexture(colorAttachment.handle);
            beginInfo.colorAttachment.texture = resolvedColor.texture;
            beginInfo.colorAttachment.loadAction = colorAttachment.loadAction;
            beginInfo.colorAttachment.storeAction = colorAttachment.storeAction;
            beginInfo.colorAttachment.clearColor = colorAttachment.clearColor;
            beginInfo.colorAttachmentIsSwapchain = resolvedColor.isSwapchain;
        }

        beginInfo.hasDepthAttachment = pass.depthAttachment.has_value();
        if (pass.depthAttachment.has_value())
        {
            const DepthAttachmentRecord& attachment = *pass.depthAttachment;
            beginInfo.depthAttachment.texture = ResolveTexture(attachment.handle).texture;
            beginInfo.depthAttachment.loadAction = attachment.loadAction;
            beginInfo.depthAttachment.storeAction = attachment.storeAction;
            beginInfo.depthAttachment.clearDepth = attachment.clearDepth;
            beginInfo.depthAttachment.readOnly = attachment.readOnly;
        }

        return beginInfo;
    }

    RenderPassExecutionInfo FrameGraph::Impl::BuildRenderPassExecutionInfo(const PassNode& pass) const
    {
        RenderPassExecutionInfo executionInfo = {};
        executionInfo.renderArea = pass.renderArea;
        if (pass.colorAttachment.has_value())
        {
            executionInfo.colorFormat = textures[pass.colorAttachment->handle.index].desc.format;
            executionInfo.colorAttachmentCount = 1;
        }
        executionInfo.depthEnabled = pass.depthAttachment.has_value();
        executionInfo.depthReadOnly = pass.depthAttachment.has_value() && pass.depthAttachment->readOnly;
        return executionInfo;
    }

    void FrameGraph::Impl::ReleasePassTextures(UInt32 orderIndex, FrameGraphTransientResourcePool& transientPool)
    {
        for (TextureResourceNode& resource : textures)
        {
            TransientTextureBacking* transientBacking = resource.GetTransientBacking();
            if (transientBacking != nullptr && resource.lastUse == orderIndex && transientBacking->texture != nullptr)
            {
                transientPool.ReleaseTexture(resource.desc, std::move(transientBacking->texture));
            }
        }
    }

    void FrameGraph::Impl::ReleaseAllTextures(FrameGraphTransientResourcePool& transientPool)
    {
        for (TextureResourceNode& resource : textures)
        {
            TransientTextureBacking* transientBacking = resource.GetTransientBacking();
            if (transientBacking != nullptr && transientBacking->texture != nullptr)
            {
                transientPool.ReleaseTexture(resource.desc, std::move(transientBacking->texture));
            }
        }
    }

    Error FrameGraph::Compile()
    {
        VE_ASSERT_RENDER_THREAD();
        if (impl_->stage != FrameGraphStage::SetupComplete)
        {
            return Error(ErrorCode::InvalidState, "FrameGraph::Compile requires a completed Setup phase.");
        }

        impl_->ResetCompileResults();

        // Step 1: validate the frozen Setup output before deriving any execution state from it.
        Error stepResult = impl_->ValidateResourceDeclarations();
        if (!stepResult.IsOk())
        {
            impl_->stage = FrameGraphStage::Failed;
            return stepResult;
        }

        // Step 2: convert versioned reads and writes into pass ordering dependencies.
        stepResult = impl_->BuildPassDependencies();
        if (!stepResult.IsOk())
        {
            impl_->stage = FrameGraphStage::Failed;
            return stepResult;
        }

        // Step 3: validate raster attachment formats and initialization rules.
        stepResult = impl_->ValidatePassDeclarations();
        if (!stepResult.IsOk())
        {
            impl_->stage = FrameGraphStage::Failed;
            return stepResult;
        }

        // Step 4: retain only passes that contribute to exported resources or explicitly declared side effects.
        impl_->CullUnusedPasses();

        // Step 5: produce a stable topological order and reject dependency cycles.
        stepResult = impl_->BuildExecutionOrder();
        if (!stepResult.IsOk())
        {
            impl_->stage = FrameGraphStage::Failed;
            return stepResult;
        }

        // Step 6: derive native store actions from later logical consumers and exported results.
        impl_->InferAttachmentStoreActions();

        // Step 7: calculate transient first/last use intervals for Execute-time allocation and release.
        impl_->AnalyzeTransientResourceLifetimes();

        impl_->stage = FrameGraphStage::Compiled;
        return Error();
    }
    ErrorCode FrameGraph::Execute()
    {
        VE_ASSERT_RENDER_THREAD();
        if (impl_->stage != FrameGraphStage::Compiled)
        {
            return ErrorCode::InvalidState;
        }

        FrameGraphTransientResourcePool& transientPool = impl_->context.frameData.GetFrameGraphTransientResourcePool();
        rhi::RhiCommandList& commandList = impl_->context.frameData.GetCommandList();

        for (UInt32 orderIndex = 0; orderIndex < impl_->compiledPassOrder.size(); ++orderIndex)
        {
            // Step 1: materialize graph-owned textures at the first pass that needs them.
            const ErrorCode acquireResult = impl_->AcquirePassTextures(orderIndex, transientPool);
            if (acquireResult != ErrorCode::None)
            {
                impl_->ReleaseAllTextures(transientPool);
                impl_->stage = FrameGraphStage::Failed;
                return acquireResult;
            }

            // Step 2: resolve logical attachments into the native begin packet and draw-time logical state.
            const UInt32 passIndex = impl_->compiledPassOrder[orderIndex];
            Impl::PassNode& pass = impl_->passes[passIndex];
            const FrameGraphPassResources passResources(*this, passIndex);
            const rhi::RhiRenderPassBeginInfo beginInfo = impl_->BuildRenderPassBeginInfo(pass);
            const RenderPassExecutionInfo executionInfo = impl_->BuildRenderPassExecutionInfo(pass);

            // Step 3: raster passes open native attachments; compute passes record directly on the command list.
            if (pass.raster && !commandList.BeginRenderPass(*impl_->context.frameData.mainSwapchain, beginInfo))
            {
                impl_->ReleaseAllTextures(transientPool);
                impl_->stage = FrameGraphStage::Failed;
                return ErrorCode::PlatformError;
            }
            if (pass.raster)
            {
                commandList.SetViewport(pass.viewport);
                commandList.SetScissor(pass.scissorRect);
            }

            // Step 4: execute renderer commands with access limited to the resources declared by this pass.
            RenderPassContext passContext(RenderPassContextInitParam{impl_->context.frameData, impl_->context.rendererData, executionInfo});
            const ErrorCode passResult = pass.executeFunction(passResources, passContext);
            if (pass.raster)
            {
                commandList.EndRenderPass();
            }
            if (passResult != ErrorCode::None)
            {
                impl_->ReleaseAllTextures(transientPool);
                impl_->stage = FrameGraphStage::Failed;
                return passResult;
            }

            // Step 5: return graph-owned textures immediately after their last compiled use.
            impl_->ReleasePassTextures(orderIndex, transientPool);
        }

        impl_->stage = FrameGraphStage::Executed;
        return ErrorCode::None;
    }
} // namespace ve
