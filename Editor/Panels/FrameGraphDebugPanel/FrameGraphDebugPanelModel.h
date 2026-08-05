#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.h"

#include <compare>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ve::editor
{
    enum class FrameGraphDebugPanelElementKind : UInt8
    {
        PassNode,
        TextureInputPin,
        TextureOutputPin,
        BufferInputPin,
        BufferOutputPin,
        DependencyLink,
    };

    struct FrameGraphDebugPanelElement
    {
        FrameGraphDebugPanelElementKind kind = FrameGraphDebugPanelElementKind::PassNode;
        UInt32 passIndex = InvalidFrameGraphDebugPassIndex;
        UInt32 resourceIndex = InvalidFrameGraphResourceIndex;
        UInt32 version = 0;
        UInt32 dependencyIndex = InvalidFrameGraphDebugPassIndex;

        auto operator<=>(const FrameGraphDebugPanelElement&) const = default;
    };

    struct FrameGraphDebugPanelDependencyEndpointIds
    {
        UInt64 startId = 0;
        UInt64 endId = 0;
    };

    struct FrameGraphDebugPanelNodePosition
    {
        UInt32 column = 0;
        UInt32 row = 0;

        auto operator<=>(const FrameGraphDebugPanelNodePosition&) const = default;
    };

    struct FrameGraphDebugPanelColumnLayout
    {
        Float32 leftWidth = 0.0F;
        Float32 rightWidth = 0.0F;
    };

    struct FrameGraphDebugPanelPin
    {
        FrameGraphDebugResourceKind resourceKind = FrameGraphDebugResourceKind::Texture;
        UInt32 resourceIndex = InvalidFrameGraphResourceIndex;
        UInt32 version = 0;

        auto operator<=>(const FrameGraphDebugPanelPin&) const = default;
    };

    struct FrameGraphDebugPanelPins
    {
        std::vector<FrameGraphDebugPanelPin> inputs;
        std::vector<FrameGraphDebugPanelPin> outputs;
    };

    struct FrameGraphDebugPanelDisplayLabel
    {
        std::string visible;
        std::string full;
        bool elided = false;
    };

    using FrameGraphDebugPanelTextMeasure = Float32 (*)(std::string_view text);

    enum class FrameGraphDebugPanelVisualRole : UInt8
    {
        RasterPass,
        ComputePass,
        CulledPass,
        RawLink,
        WarLink,
        WawLink,
        TexturePin,
        BufferPin,
    };

    /// Owns deterministic, collision-free imgui-node-editor IDs for one immutable debug snapshot.
    class FrameGraphDebugPanelIdRegistry
    {
    public:
        FrameGraphDebugPanelIdRegistry() = default;
        explicit FrameGraphDebugPanelIdRegistry(const FrameGraphDebugData& data);

        [[nodiscard]] bool Reset(const FrameGraphDebugData& data);
        void Clear() noexcept;

        [[nodiscard]] std::optional<UInt64> FindPassNodeId(UInt32 passIndex) const noexcept;
        [[nodiscard]] std::optional<UInt64> FindTexturePinId(UInt32 passIndex, UInt32 textureIndex, UInt32 version, bool output) const noexcept;
        [[nodiscard]] std::optional<UInt64> FindBufferPinId(UInt32 passIndex, UInt32 bufferIndex, UInt32 version, bool output) const noexcept;
        [[nodiscard]] std::optional<UInt64> FindDependencyLinkId(UInt32 dependencyIndex) const noexcept;
        [[nodiscard]] std::optional<FrameGraphDebugPanelDependencyEndpointIds> FindDependencyEndpointIds(UInt32 dependencyIndex) const noexcept;
        [[nodiscard]] std::optional<UInt64> FindId(const FrameGraphDebugPanelElement& element) const noexcept;
        [[nodiscard]] std::optional<FrameGraphDebugPanelElement> FindElement(UInt64 id) const noexcept;
        [[nodiscard]] SizeT GetElementCount() const noexcept;
        [[nodiscard]] bool IsValid() const noexcept;

    private:
        struct DependencyEndpointElements
        {
            FrameGraphDebugPanelElement start;
            FrameGraphDebugPanelElement end;
        };

        std::vector<FrameGraphDebugPanelElement> elements_;
        std::vector<std::optional<DependencyEndpointElements>> dependencyEndpoints_;
        bool valid_ = false;
    };

    [[nodiscard]] std::optional<FrameGraphDebugPanelPins> BuildFrameGraphDebugPanelPins(const FrameGraphDebugPass& pass);
    [[nodiscard]] FrameGraphDebugPanelDisplayLabel BuildFrameGraphDebugPanelDisplayLabel(
        std::string_view prefix, std::string_view value, std::string_view suffix, Float32 maximumWidth, FrameGraphDebugPanelTextMeasure measureText);
    [[nodiscard]] std::optional<std::string>
    BuildFrameGraphDebugPanelTooltipText(const FrameGraphDebugPanelDisplayLabel& label, bool hovered);
    [[nodiscard]] std::optional<FrameGraphDebugPanelVisualRole> GetFrameGraphDebugPanelPassRole(const FrameGraphDebugPass& pass) noexcept;
    [[nodiscard]] std::optional<FrameGraphDebugPanelVisualRole> GetFrameGraphDebugPanelDependencyRole(FrameGraphDebugDependencyHazard hazard) noexcept;
    [[nodiscard]] std::optional<FrameGraphDebugPanelVisualRole> GetFrameGraphDebugPanelResourceRole(FrameGraphDebugResourceKind resourceKind) noexcept;
    [[nodiscard]] std::optional<std::vector<FrameGraphDebugPanelNodePosition>> BuildFrameGraphDebugPanelLayout(const FrameGraphDebugData& data);
    [[nodiscard]] FrameGraphDebugPanelColumnLayout CalculateFrameGraphDebugPanelColumnLayout(Float32 availableWidth, Float32 itemSpacing) noexcept;
    [[nodiscard]] bool CanCaptureFrameGraph(bool isPlaying, FrameGraphDebugCaptureStatus status, bool captureResultPending = false) noexcept;
    [[nodiscard]] bool ShouldPauseAfterFrameGraphCapture(bool isPlaying, bool isPaused, ErrorCode requestResult) noexcept;
} // namespace ve::editor
