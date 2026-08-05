#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] bool IsPassReferenceValid(UInt32 passIndex, SizeT passCount)
        {
            if (passIndex == InvalidFrameGraphDebugPassIndex)
            {
                return false;
            }

            const bool valid = passIndex < passCount;
            VE_ASSERT_MESSAGE(valid, "Frame graph debug source contains an out-of-range pass index.");
            return valid;
        }

        [[nodiscard]] FrameGraphDebugResourceVersion BuildResourceVersion(const FrameGraphDebugSourceVersion& sourceVersion,
                                                                          const std::vector<std::optional<UInt32>>& compiledIndices)
        {
            FrameGraphDebugResourceVersion version;
            if (IsPassReferenceValid(sourceVersion.producer, compiledIndices.size()))
            {
                version.producer = sourceVersion.producer;
            }

            for (const UInt32 reader : sourceVersion.readers)
            {
                if (IsPassReferenceValid(reader, compiledIndices.size()))
                {
                    version.readers.push_back(reader);
                }
            }
            version.exported = sourceVersion.exported;

            const auto includeCompiledUse = [&version, &compiledIndices](UInt32 passIndex)
            {
                if (!IsPassReferenceValid(passIndex, compiledIndices.size()) || !compiledIndices[passIndex])
                {
                    return;
                }

                const UInt32 compiledIndex = *compiledIndices[passIndex];
                version.firstCompiledUse = version.firstCompiledUse ? (std::min)(*version.firstCompiledUse, compiledIndex) : compiledIndex;
                version.lastCompiledUse = version.lastCompiledUse ? (std::max)(*version.lastCompiledUse, compiledIndex) : compiledIndex;
            };

            includeCompiledUse(sourceVersion.producer);
            for (const UInt32 reader : sourceVersion.readers)
            {
                includeCompiledUse(reader);
            }
            includeCompiledUse(sourceVersion.nextWriter);
            return version;
        }

        void AddDependency(std::vector<FrameGraphDebugDependency>& dependencies,
                           UInt32 beforePass,
                           UInt32 afterPass,
                           FrameGraphDebugResourceKind resourceKind,
                           UInt32 resourceIndex,
                           UInt32 version,
                           FrameGraphDebugDependencyHazard hazard,
                           SizeT passCount)
        {
            if (!IsPassReferenceValid(beforePass, passCount) || !IsPassReferenceValid(afterPass, passCount) || beforePass == afterPass)
            {
                return;
            }

            dependencies.push_back({beforePass, afterPass, resourceKind, resourceIndex, version, hazard});
        }

        void AddVersionDependencies(std::vector<FrameGraphDebugDependency>& dependencies,
                                    const FrameGraphDebugSourceVersion& sourceVersion,
                                    FrameGraphDebugResourceKind resourceKind,
                                    UInt32 resourceIndex,
                                    UInt32 version,
                                    SizeT passCount)
        {
            for (const UInt32 reader : sourceVersion.readers)
            {
                AddDependency(
                    dependencies, sourceVersion.producer, reader, resourceKind, resourceIndex, version, FrameGraphDebugDependencyHazard::Raw, passCount);
                AddDependency(
                    dependencies, reader, sourceVersion.nextWriter, resourceKind, resourceIndex, version, FrameGraphDebugDependencyHazard::War, passCount);
            }
            AddDependency(dependencies,
                          sourceVersion.producer,
                          sourceVersion.nextWriter,
                          resourceKind,
                          resourceIndex,
                          version,
                          FrameGraphDebugDependencyHazard::Waw,
                          passCount);
        }

        [[nodiscard]] bool DependencyLess(const FrameGraphDebugDependency& left, const FrameGraphDebugDependency& right)
        {
            // Resource-first ordering keeps output stable independently of source reader insertion order.
            return std::tuple(static_cast<UInt32>(left.resourceKind),
                              left.resourceIndex,
                              left.version,
                              static_cast<UInt32>(left.hazard),
                              left.beforePass,
                              left.afterPass) < std::tuple(static_cast<UInt32>(right.resourceKind),
                                                           right.resourceIndex,
                                                           right.version,
                                                           static_cast<UInt32>(right.hazard),
                                                           right.beforePass,
                                                           right.afterPass);
        }

        [[nodiscard]] bool DependenciesEqual(const FrameGraphDebugDependency& left, const FrameGraphDebugDependency& right)
        {
            return left.resourceKind == right.resourceKind && left.resourceIndex == right.resourceIndex && left.version == right.version &&
                   left.hazard == right.hazard && left.beforePass == right.beforePass && left.afterPass == right.afterPass;
        }

    } // namespace

    bool IsFrameGraphDebugPreviewScaleValid(Float32 scale) noexcept
    {
        return std::isfinite(scale) && scale >= 0.1F && scale <= 1.0F;
    }

    rhi::RhiExtent2D CalculateFrameGraphDebugPreviewExtent(UInt32 width, UInt32 height, Float32 scale) noexcept
    {
        VE_ASSERT(width != 0);
        VE_ASSERT(height != 0);
        VE_ASSERT(IsFrameGraphDebugPreviewScaleValid(scale));

        const auto scaled = [scale](UInt32 value)
        {
            const auto rounded = static_cast<UInt32>(std::llround(static_cast<double>(value) * static_cast<double>(scale)));
            return (std::max)(1U, rounded);
        };
        return {scaled(width), scaled(height)};
    }

    bool ShouldPublishFrameGraphDebugCapture(ErrorCode submitResult, const std::shared_ptr<FrameGraphDebugData>& data, std::string_view failureMessage) noexcept
    {
        return submitResult == ErrorCode::None && data != nullptr && failureMessage.empty();
    }

    FrameGraphDebugCapturePublishResult CompleteFrameGraphDebugCapture(FrameGraphDebugCaptureExchange& exchange,
                                                                       ErrorCode submitResult,
                                                                       FrameGraphDebugCaptureRequest request,
                                                                       std::shared_ptr<FrameGraphDebugData> data,
                                                                       std::string failureMessage)
    {
        if (!ShouldPublishFrameGraphDebugCapture(submitResult, data, failureMessage))
        {
            if (failureMessage.empty())
            {
                failureMessage = data == nullptr ? "Frame graph debug capture produced no data."
                                                 : "Frame graph debug capture frame submission failed: " + std::string(ToString(submitResult)) + ".";
            }
            exchange.Fail(request.requestId, std::move(failureMessage));
            return {false, std::move(data)};
        }

        if (data->requestId != request.requestId)
        {
            failureMessage = "Frame graph debug capture request ID mismatch: expected " + std::to_string(request.requestId) + ", actual " +
                             std::to_string(data->requestId) + ".";
            exchange.Fail(request.requestId, std::move(failureMessage));
            return {false, std::move(data)};
        }

        FrameGraphDebugCapturePublishResult result = exchange.Publish(std::move(data));
        if (!result.published)
        {
            exchange.Fail(request.requestId, "Frame graph debug capture exchange rejected the completed request.");
        }
        return result;
    }

    std::vector<std::shared_ptr<FrameGraphDebugPreviewTexture>> CollectFrameGraphDebugPreviewTextures(const FrameGraphDebugData& data)
    {
        std::vector<std::shared_ptr<FrameGraphDebugPreviewTexture>> result;
        std::unordered_set<FrameGraphDebugPreviewTexture*> collected;
        for (const FrameGraphDebugTexture& texture : data.textures)
        {
            for (const FrameGraphDebugResourceVersion& version : texture.versions)
            {
                const std::shared_ptr<FrameGraphDebugPreviewTexture>& previewTexture = version.preview.texture;
                if (previewTexture != nullptr && collected.insert(previewTexture.get()).second)
                {
                    result.push_back(previewTexture);
                }
            }
        }
        return result;
    }

    FrameGraphDebugBuildResult BuildFrameGraphDebugData(const FrameGraphDebugSourceGraph& source, FrameGraphDebugCaptureRequest request)
    {
        FrameGraphDebugBuildResult result;
        result.data = std::make_shared<FrameGraphDebugData>();
        result.data->requestId = request.requestId;
        result.data->frameIndex = source.frameIndex;
        result.data->previewScale = request.previewScale;

        const bool previewScaleValid = IsFrameGraphDebugPreviewScaleValid(request.previewScale);
        VE_ASSERT_MESSAGE(previewScaleValid, "Frame graph debug capture request contains an invalid preview scale.");

        std::vector<std::optional<UInt32>> compiledIndices(source.passes.size());
        for (SizeT passIndex = 0; passIndex < source.passes.size(); ++passIndex)
        {
            VE_ASSERT_MESSAGE(source.passes[passIndex].registrationIndex == passIndex,
                              "Frame graph debug source pass registration index does not match its source index.");
        }

        for (const UInt32 passIndex : source.compiledPassOrder)
        {
            if (!IsPassReferenceValid(passIndex, source.passes.size()))
            {
                continue;
            }

            if (source.passes[passIndex].registrationIndex != passIndex)
            {
                continue;
            }

            VE_ASSERT_MESSAGE(source.passes[passIndex].retained, "Frame graph debug compiled pass order contains an unretained pass.");
            VE_ASSERT_MESSAGE(!compiledIndices[passIndex], "Frame graph debug compiled pass order contains a duplicate pass.");
            if (!source.passes[passIndex].retained || compiledIndices[passIndex])
            {
                continue;
            }

            compiledIndices[passIndex] = static_cast<UInt32>(result.data->executionPassIndices.size());
            result.data->executionPassIndices.push_back(passIndex);
        }

        result.data->passes.reserve(source.passes.size());
        for (SizeT passIndex = 0; passIndex < source.passes.size(); ++passIndex)
        {
            const FrameGraphDebugSourcePass& sourcePass = source.passes[passIndex];
            FrameGraphDebugPass pass;
            pass.name = sourcePass.name;
            pass.type = sourcePass.type;
            pass.registrationIndex = sourcePass.registrationIndex;
            pass.compiledIndex = sourcePass.retained ? compiledIndices[passIndex] : std::nullopt;
            pass.retained = sourcePass.retained;
            pass.culled = !pass.compiledIndex.has_value();
            pass.sideEffect = sourcePass.sideEffect;
            pass.accesses = sourcePass.accesses;
            pass.attachments = sourcePass.attachments;
            pass.textureUavBarriers = sourcePass.textureUavBarriers;
            pass.bufferUavBarriers = sourcePass.bufferUavBarriers;
            result.data->passes.push_back(std::move(pass));
        }

        result.data->textures.reserve(source.textures.size());
        for (UInt32 textureIndex = 0; textureIndex < source.textures.size(); ++textureIndex)
        {
            const FrameGraphDebugSourceTexture& sourceTexture = source.textures[textureIndex];
            FrameGraphDebugTexture texture;
            texture.name = sourceTexture.name;
            texture.desc = sourceTexture.desc;
            texture.imported = sourceTexture.imported;
            texture.swapchain = sourceTexture.swapchain;
            texture.versions.reserve(sourceTexture.versions.size());

            for (UInt32 versionIndex = 0; versionIndex < sourceTexture.versions.size(); ++versionIndex)
            {
                const FrameGraphDebugSourceVersion& sourceVersion = sourceTexture.versions[versionIndex];
                FrameGraphDebugResourceVersion version = BuildResourceVersion(sourceVersion, compiledIndices);
                version.preview.sourceExtent = {sourceTexture.desc.width, sourceTexture.desc.height};
                version.preview.sourceFormat = sourceTexture.desc.format;

                bool capturePlanned = false;
                if (versionIndex == 0)
                {
                    if (sourceTexture.imported && !sourceTexture.swapchain)
                    {
                        capturePlanned = true;
                    }
                    else if (sourceTexture.swapchain)
                    {
                        version.preview.message = "swapchain contents undefined";
                    }
                    else
                    {
                        version.preview.message = "uninitialized";
                    }
                }
                else if (!IsPassReferenceValid(sourceVersion.producer, source.passes.size()))
                {
                    version.preview.message = "uninitialized";
                }
                else if (sourceVersion.producer == sourceVersion.nextWriter)
                {
                    version.preview.message = "no pass boundary before next write";
                }
                else if (!compiledIndices[sourceVersion.producer])
                {
                    version.preview.message = "producer culled";
                }
                else
                {
                    capturePlanned = true;
                }

                if (capturePlanned && previewScaleValid && sourceTexture.desc.width != 0 && sourceTexture.desc.height != 0)
                {
                    version.preview.previewExtent =
                        CalculateFrameGraphDebugPreviewExtent(sourceTexture.desc.width, sourceTexture.desc.height, request.previewScale);
                    version.preview.message = "capture pending";

                    UInt32 captureBeforePass = InvalidFrameGraphDebugPassIndex;
                    if (IsPassReferenceValid(sourceVersion.nextWriter, source.passes.size()))
                    {
                        captureBeforePass = sourceVersion.nextWriter;
                    }
                    result.capturePlan.push_back({textureIndex,
                                                  versionIndex,
                                                  version.preview.previewExtent,
                                                  sourceTexture.swapchain,
                                                  NeedsFrameGraphDebugStaging(sourceTexture.desc.usage),
                                                  captureBeforePass});
                }
                else if (capturePlanned)
                {
                    VE_ASSERT_MESSAGE(sourceTexture.desc.width != 0 && sourceTexture.desc.height != 0,
                                      "Frame graph debug capture source texture has an invalid extent.");
                    version.preview.message = previewScaleValid ? "invalid source extent" : "invalid preview scale";
                }

                AddVersionDependencies(
                    result.data->dependencies, sourceVersion, FrameGraphDebugResourceKind::Texture, textureIndex, versionIndex, source.passes.size());
                texture.versions.push_back(std::move(version));
            }
            result.data->textures.push_back(std::move(texture));
        }

        result.data->buffers.reserve(source.buffers.size());
        for (UInt32 bufferIndex = 0; bufferIndex < source.buffers.size(); ++bufferIndex)
        {
            const FrameGraphDebugSourceBuffer& sourceBuffer = source.buffers[bufferIndex];
            FrameGraphDebugBuffer buffer;
            buffer.name = sourceBuffer.name;
            buffer.size = sourceBuffer.size;
            buffer.versions.reserve(sourceBuffer.versions.size());
            for (UInt32 versionIndex = 0; versionIndex < sourceBuffer.versions.size(); ++versionIndex)
            {
                const FrameGraphDebugSourceVersion& sourceVersion = sourceBuffer.versions[versionIndex];
                buffer.versions.push_back(BuildResourceVersion(sourceVersion, compiledIndices));
                AddVersionDependencies(
                    result.data->dependencies, sourceVersion, FrameGraphDebugResourceKind::Buffer, bufferIndex, versionIndex, source.passes.size());
            }
            result.data->buffers.push_back(std::move(buffer));
        }

        std::sort(result.data->dependencies.begin(), result.data->dependencies.end(), DependencyLess);
        result.data->dependencies.erase(std::unique(result.data->dependencies.begin(), result.data->dependencies.end(), DependenciesEqual),
                                        result.data->dependencies.end());
        return result;
    }

    ErrorCode FrameGraphDebugCaptureExchange::RequestCapture(Float32 previewScale)
    {
        if (!IsFrameGraphDebugPreviewScaleValid(previewScale))
        {
            return ErrorCode::InvalidArgument;
        }

        LockGuard lock(mutex_);
        if (status_ == FrameGraphDebugCaptureStatus::Armed || status_ == FrameGraphDebugCaptureStatus::Capturing)
        {
            return ErrorCode::InvalidState;
        }

        pendingRequest_ = FrameGraphDebugCaptureRequest{nextRequestId_++, previewScale};
        activeRequestId_.reset();
        failureMessage_.clear();
        status_ = FrameGraphDebugCaptureStatus::Armed;
        return ErrorCode::None;
    }

    std::optional<FrameGraphDebugCaptureRequest> FrameGraphDebugCaptureExchange::ConsumeRequest()
    {
        LockGuard lock(mutex_);
        if (status_ != FrameGraphDebugCaptureStatus::Armed || !pendingRequest_)
        {
            return std::nullopt;
        }

        const FrameGraphDebugCaptureRequest request = *pendingRequest_;
        pendingRequest_.reset();
        activeRequestId_ = request.requestId;
        status_ = FrameGraphDebugCaptureStatus::Capturing;
        return request;
    }

    FrameGraphDebugCapturePublishResult FrameGraphDebugCaptureExchange::Publish(std::shared_ptr<const FrameGraphDebugData> data)
    {
        LockGuard lock(mutex_);
        if (!data || status_ != FrameGraphDebugCaptureStatus::Capturing || !activeRequestId_ || data->requestId != *activeRequestId_)
        {
            return {false, std::move(data)};
        }

        publishedData_.swap(data);
        activeRequestId_.reset();
        failureMessage_.clear();
        status_ = FrameGraphDebugCaptureStatus::Ready;
        return {true, std::move(data)};
    }

    void FrameGraphDebugCaptureExchange::Fail(UInt64 requestId, std::string message)
    {
        LockGuard lock(mutex_);
        if (status_ != FrameGraphDebugCaptureStatus::Capturing || !activeRequestId_ || requestId != *activeRequestId_)
        {
            return;
        }

        activeRequestId_.reset();
        failureMessage_ = std::move(message);
        status_ = FrameGraphDebugCaptureStatus::Failed;
    }

    std::shared_ptr<const FrameGraphDebugData> FrameGraphDebugCaptureExchange::TakePublishedData()
    {
        LockGuard lock(mutex_);
        return std::move(publishedData_);
    }

    FrameGraphDebugCaptureStatus FrameGraphDebugCaptureExchange::GetStatus() const
    {
        LockGuard lock(mutex_);
        return status_;
    }

    std::string FrameGraphDebugCaptureExchange::GetFailureMessage() const
    {
        LockGuard lock(mutex_);
        return failureMessage_;
    }

    std::shared_ptr<const FrameGraphDebugData> FrameGraphDebugCaptureExchange::Reset() noexcept
    {
        LockGuard lock(mutex_);
        pendingRequest_.reset();
        activeRequestId_.reset();
        std::shared_ptr<const FrameGraphDebugData> publishedData;
        publishedData.swap(publishedData_);
        failureMessage_.clear();
        status_ = FrameGraphDebugCaptureStatus::Idle;
        return publishedData;
    }
} // namespace ve
