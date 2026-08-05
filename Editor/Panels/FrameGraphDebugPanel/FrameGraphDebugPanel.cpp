#include "Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanel.h"

#include "Editor/Core/Editor.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <imgui_node_editor.h>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ve::editor
{
    namespace ed = ax::NodeEditor;

    namespace
    {
        constexpr float MinimumPreviewScale = 0.1F;
        constexpr float MaximumPreviewScale = 1.0F;
        constexpr float MinimumPreviewZoom = 0.1F;
        constexpr float MaximumPreviewZoom = 16.0F;
        constexpr float MinimumPreviewCanvasHeight = 220.0F;
        constexpr float NodeColumnSpacing = 620.0F;
        constexpr float NodeRowSpacing = 230.0F;
        constexpr float NodePinColumnWidth = 190.0F;
        constexpr float NodePinColumnGap = 20.0F;
        constexpr float NodeContentWidth = NodePinColumnWidth * 2.0F + NodePinColumnGap;

        struct FixedWidthLabelRect
        {
            ImVec2 minimum;
            ImVec2 maximum;
        };

        [[nodiscard]] Float32 MeasurePanelText(std::string_view text)
        {
            return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
        }

        [[nodiscard]] FixedWidthLabelRect
        RenderFixedWidthLabel(const FrameGraphDebugPanelDisplayLabel& label,
                              const ImVec4& color,
                              float maximumWidth,
                              bool alignRight,
                              std::optional<std::string>& deferredTooltip)
        {
            const float width = (std::max)(1.0F, maximumWidth);
            const float height = ImGui::GetTextLineHeight();
            const ImVec2 minimum = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(width, height));
            const ImVec2 maximum(minimum.x + width, minimum.y + height);

            const float textWidth = ImGui::CalcTextSize(label.visible.c_str()).x;
            const float textX = alignRight ? (std::max)(minimum.x, maximum.x - textWidth) : minimum.x;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(minimum, maximum, true);
            drawList->AddText(ImVec2(textX, minimum.y), ImGui::ColorConvertFloat4ToU32(color), label.visible.c_str());
            drawList->PopClipRect();

            if (std::optional<std::string> tooltip = BuildFrameGraphDebugPanelTooltipText(label, ImGui::IsItemHovered()); tooltip.has_value())
            {
                deferredTooltip = std::move(*tooltip);
            }
            return {minimum, maximum};
        }

        [[nodiscard]] ed::NodeId ToNodeId(UInt64 id) noexcept
        {
            return ed::NodeId(static_cast<uintptr_t>(id));
        }

        [[nodiscard]] ed::PinId ToPinId(UInt64 id) noexcept
        {
            return ed::PinId(static_cast<uintptr_t>(id));
        }

        [[nodiscard]] ed::LinkId ToLinkId(UInt64 id) noexcept
        {
            return ed::LinkId(static_cast<uintptr_t>(id));
        }

        [[nodiscard]] UInt64 FromNodeId(ed::NodeId id) noexcept
        {
            return static_cast<UInt64>(id.Get());
        }

        [[nodiscard]] UInt64 FromPinId(ed::PinId id) noexcept
        {
            return static_cast<UInt64>(id.Get());
        }

        [[nodiscard]] UInt64 FromLinkId(ed::LinkId id) noexcept
        {
            return static_cast<UInt64>(id.Get());
        }

        [[nodiscard]] std::optional<UInt64>
        FindPanelPinId(const FrameGraphDebugPanelIdRegistry& registry, UInt32 passIndex, const FrameGraphDebugPanelPin& pin, bool output) noexcept
        {
            switch (pin.resourceKind)
            {
            case FrameGraphDebugResourceKind::Texture:
                return registry.FindTexturePinId(passIndex, pin.resourceIndex, pin.version, output);
            case FrameGraphDebugResourceKind::Buffer:
                return registry.FindBufferPinId(passIndex, pin.resourceIndex, pin.version, output);
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] std::optional<UInt32> ToPanelIndex(SizeT index) noexcept
        {
            if (index > (std::numeric_limits<UInt32>::max)())
            {
                return std::nullopt;
            }
            return static_cast<UInt32>(index);
        }

        [[nodiscard]] bool IsDependencyPassRangeValid(const FrameGraphDebugData& data, const FrameGraphDebugDependency& dependency) noexcept
        {
            return dependency.beforePass < data.passes.size() && dependency.afterPass < data.passes.size();
        }

        [[nodiscard]] const char* ToString(FrameGraphDebugCaptureStatus status) noexcept
        {
            switch (status)
            {
            case FrameGraphDebugCaptureStatus::Idle:
                return "Idle";
            case FrameGraphDebugCaptureStatus::Armed:
                return "Armed";
            case FrameGraphDebugCaptureStatus::Capturing:
                return "Capturing";
            case FrameGraphDebugCaptureStatus::Ready:
                return "Ready";
            case FrameGraphDebugCaptureStatus::Failed:
                return "Failed";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* ToString(FrameGraphDebugPassType type) noexcept
        {
            return type == FrameGraphDebugPassType::Raster ? "Raster" : "Compute";
        }

        [[nodiscard]] const char* ToString(FrameGraphDebugResourceKind kind) noexcept
        {
            return kind == FrameGraphDebugResourceKind::Texture ? "Texture" : "Buffer";
        }

        [[nodiscard]] const char* ToString(FrameGraphDebugDependencyHazard hazard) noexcept
        {
            switch (hazard)
            {
            case FrameGraphDebugDependencyHazard::Raw:
                return "RAW";
            case FrameGraphDebugDependencyHazard::War:
                return "WAR";
            case FrameGraphDebugDependencyHazard::Waw:
                return "WAW";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* ToString(FrameGraphDebugPreviewState state) noexcept
        {
            switch (state)
            {
            case FrameGraphDebugPreviewState::Unavailable:
                return "Unavailable";
            case FrameGraphDebugPreviewState::Ready:
                return "Ready";
            case FrameGraphDebugPreviewState::Failed:
                return "Failed";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* ToString(rhi::RhiFormat format) noexcept
        {
            switch (format)
            {
            case rhi::RhiFormat::Unknown:
                return "Unknown";
            case rhi::RhiFormat::Rgba8Unorm:
                return "Rgba8Unorm";
            case rhi::RhiFormat::Bgra8Unorm:
                return "Bgra8Unorm";
            case rhi::RhiFormat::Rgb32Float:
                return "Rgb32Float";
            case rhi::RhiFormat::R32Uint:
                return "R32Uint";
            case rhi::RhiFormat::Depth32Float:
                return "Depth32Float";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* ToString(rhi::RhiLoadAction action) noexcept
        {
            switch (action)
            {
            case rhi::RhiLoadAction::Load:
                return "Load";
            case rhi::RhiLoadAction::Clear:
                return "Clear";
            case rhi::RhiLoadAction::DontCare:
                return "DontCare";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* ToString(rhi::RhiStoreAction action) noexcept
        {
            return action == rhi::RhiStoreAction::Store ? "Store" : "DontCare";
        }

        [[nodiscard]] const char* TextureAccessName(UInt32 value) noexcept
        {
            switch (static_cast<FrameGraphTextureAccess>(value))
            {
            case FrameGraphTextureAccess::ColorAttachment:
                return "ColorAttachment";
            case FrameGraphTextureAccess::DepthAttachment:
                return "DepthAttachment";
            case FrameGraphTextureAccess::ShaderRead:
                return "ShaderRead";
            case FrameGraphTextureAccess::ShaderReadWrite:
                return "ShaderReadWrite";
            case FrameGraphTextureAccess::CopySource:
                return "CopySource";
            case FrameGraphTextureAccess::CopyDestination:
                return "CopyDestination";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* BufferAccessName(UInt32 value) noexcept
        {
            switch (static_cast<FrameGraphBufferAccess>(value))
            {
            case FrameGraphBufferAccess::ShaderRead:
                return "ShaderRead";
            case FrameGraphBufferAccess::ShaderReadWrite:
                return "ShaderReadWrite";
            case FrameGraphBufferAccess::CopySource:
                return "CopySource";
            case FrameGraphBufferAccess::CopyDestination:
                return "CopyDestination";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* AccessName(const FrameGraphDebugAccess& access) noexcept
        {
            return access.resourceKind == FrameGraphDebugResourceKind::Texture ? TextureAccessName(access.accessValue) : BufferAccessName(access.accessValue);
        }

        [[nodiscard]] ImVec4 VisualRoleColor(FrameGraphDebugPanelVisualRole role) noexcept
        {
            switch (role)
            {
            case FrameGraphDebugPanelVisualRole::RasterPass:
                return ImVec4(0.12F, 0.30F, 0.48F, 0.95F);
            case FrameGraphDebugPanelVisualRole::ComputePass:
                return ImVec4(0.35F, 0.18F, 0.48F, 0.95F);
            case FrameGraphDebugPanelVisualRole::CulledPass:
                return ImVec4(0.20F, 0.20F, 0.22F, 0.85F);
            case FrameGraphDebugPanelVisualRole::RawLink:
                return ImVec4(0.25F, 0.82F, 0.38F, 1.0F);
            case FrameGraphDebugPanelVisualRole::WarLink:
                return ImVec4(0.95F, 0.72F, 0.20F, 1.0F);
            case FrameGraphDebugPanelVisualRole::WawLink:
                return ImVec4(0.95F, 0.28F, 0.28F, 1.0F);
            case FrameGraphDebugPanelVisualRole::TexturePin:
                return ImVec4(0.20F, 0.75F, 0.95F, 1.0F);
            case FrameGraphDebugPanelVisualRole::BufferPin:
                return ImVec4(0.95F, 0.67F, 0.18F, 1.0F);
            }
            return ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
        }

        [[nodiscard]] ImVec4 DependencyColor(FrameGraphDebugDependencyHazard hazard) noexcept
        {
            const std::optional<FrameGraphDebugPanelVisualRole> role = GetFrameGraphDebugPanelDependencyRole(hazard);
            return role.has_value() ? VisualRoleColor(*role) : ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
        }

        [[nodiscard]] bool ContainsCaseInsensitive(const std::string& value, const char* search)
        {
            if (search == nullptr || search[0] == '\0')
            {
                return true;
            }
            std::string lowerValue(value);
            std::string lowerSearch(search);
            std::transform(
                lowerValue.begin(), lowerValue.end(), lowerValue.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            std::transform(lowerSearch.begin(),
                           lowerSearch.end(),
                           lowerSearch.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return lowerValue.find(lowerSearch) != std::string::npos;
        }

        [[nodiscard]] const std::string& GetResourceName(const FrameGraphDebugData& data, FrameGraphDebugResourceKind kind, UInt32 index)
        {
            static const std::string InvalidName = "<invalid>";
            if (kind == FrameGraphDebugResourceKind::Texture)
            {
                return index < data.textures.size() ? data.textures[index].name : InvalidName;
            }
            return index < data.buffers.size() ? data.buffers[index].name : InvalidName;
        }

        [[nodiscard]] const FrameGraphDebugResourceVersion*
        GetResourceVersion(const FrameGraphDebugData& data, FrameGraphDebugResourceKind kind, UInt32 index, UInt32 version) noexcept
        {
            if (kind == FrameGraphDebugResourceKind::Texture)
            {
                return index < data.textures.size() && version < data.textures[index].versions.size() ? &data.textures[index].versions[version] : nullptr;
            }
            return index < data.buffers.size() && version < data.buffers[index].versions.size() ? &data.buffers[index].versions[version] : nullptr;
        }

        void TextOptionalIndex(const char* label, const std::optional<UInt32>& value)
        {
            if (value.has_value())
            {
                ImGui::Text("%s: %u", label, *value);
            }
            else
            {
                ImGui::Text("%s: -", label);
            }
        }

        [[nodiscard]] const char* TextureUsageText(rhi::RhiTextureUsage usage)
        {
            static thread_local std::string result;
            result.clear();
            const UInt32 value = static_cast<UInt32>(usage);
            const auto append = [&](rhi::RhiTextureUsage bit, const char* name)
            {
                if ((value & static_cast<UInt32>(bit)) != 0)
                {
                    if (!result.empty())
                    {
                        result += " | ";
                    }
                    result += name;
                }
            };
            append(rhi::RhiTextureUsage::Sampled, "Sampled");
            append(rhi::RhiTextureUsage::RenderTarget, "RenderTarget");
            append(rhi::RhiTextureUsage::DepthStencil, "DepthStencil");
            append(rhi::RhiTextureUsage::Storage, "Storage");
            if (result.empty())
            {
                result = "None";
            }
            return result.c_str();
        }
    } // namespace

    void FrameGraphDebugPanel::Init(Editor& editor)
    {
        if (editor_ != nullptr)
        {
            return;
        }

        BasePanel::Init(editor);
        editor_ = &editor;
        ed::Config config;
        config.SettingsFile = nullptr;
        nodeEditorContext_ = ed::CreateEditor(&config);
        VE_ASSERT_MESSAGE(nodeEditorContext_ != nullptr, "FrameGraphDebugPanel failed to create the node-editor context.");
    }

    bool FrameGraphDebugPanel::Shutdown()
    {
        if (editor_ == nullptr)
        {
            return true;
        }

        debugData_ = editor_->GetRenderSystem().RetireFrameGraphDebugData(std::move(debugData_));
        if (debugData_ != nullptr)
        {
            return false;
        }

        idRegistry_.Clear();
        nodePositions_.clear();
        positionedNodes_.clear();
        nodeLayoutValid_ = false;
        if (nodeEditorContext_ != nullptr)
        {
            ed::DestroyEditor(nodeEditorContext_);
            nodeEditorContext_ = nullptr;
        }
        editor_ = nullptr;
        captureResultPending_ = false;
        return true;
    }

    const char* FrameGraphDebugPanel::GetName() const noexcept
    {
        return "Frame Graph";
    }

    void FrameGraphDebugPanel::RenderContent()
    {
        if (editor_ == nullptr || nodeEditorContext_ == nullptr)
        {
            ImGui::TextDisabled("Frame Graph debugger is unavailable.");
            return;
        }

        PollCapture();
        RenderToolbar();
        RenderTopologyLegend();
        ImGui::Separator();

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImGuiStyle& style = ImGui::GetStyle();
        const FrameGraphDebugPanelColumnLayout columns = CalculateFrameGraphDebugPanelColumnLayout(available.x, style.ItemSpacing.x);

        if (ImGui::BeginChild("FrameGraphDebugLeftColumn", ImVec2(columns.leftWidth, 0.0F)))
        {
            const float topologyHeight = (std::max)(130.0F, ImGui::GetContentRegionAvail().y * 0.60F);
            RenderTopology(topologyHeight);
            ImGui::Separator();

            if (ImGui::BeginChild("FrameGraphDebugTables", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders))
            {
                RenderTables();
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("FrameGraphDebugPreview", ImVec2(columns.rightWidth, 0.0F), ImGuiChildFlags_Borders))
        {
            RenderPreviewPane();
        }
        ImGui::EndChild();
    }

    void FrameGraphDebugPanel::PollCapture()
    {
        RenderSystem& renderSystem = editor_->GetRenderSystem();
        const FrameGraphDebugCaptureStatus status = renderSystem.GetFrameGraphDebugCaptureStatus();
        if (status == FrameGraphDebugCaptureStatus::Failed)
        {
            captureResultPending_ = false;
            return;
        }
        if (!captureResultPending_ || status != FrameGraphDebugCaptureStatus::Ready)
        {
            return;
        }

        debugData_ = renderSystem.RetireFrameGraphDebugData(std::move(debugData_));
        if (debugData_ != nullptr)
        {
            return;
        }

        debugData_ = renderSystem.TakeFrameGraphDebugData();
        if (debugData_ == nullptr)
        {
            return;
        }

        captureResultPending_ = false;
        ResetSnapshotUiState();
    }

    void FrameGraphDebugPanel::RenderToolbar()
    {
        RenderSystem& renderSystem = editor_->GetRenderSystem();
        const FrameGraphDebugCaptureStatus status = renderSystem.GetFrameGraphDebugCaptureStatus();

        ImGui::SetNextItemWidth(120.0F);
        ImGui::SliderFloat("Preview Scale", &previewScale_, MinimumPreviewScale, MaximumPreviewScale, "%.2f");
        previewScale_ = std::clamp(previewScale_, MinimumPreviewScale, MaximumPreviewScale);
        ImGui::SameLine();

        const bool canCapture = CanCaptureFrameGraph(editor_->IsPlaying(), status, captureResultPending_);
        if (!canCapture)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Capture Next Frame"))
        {
            const ErrorCode result = renderSystem.RequestFrameGraphDebugCapture(previewScale_);
            if (result == ErrorCode::None)
            {
                captureResultPending_ = true;
            }
            if (ShouldPauseAfterFrameGraphCapture(editor_->IsPlaying(), editor_->IsPaused(), result))
            {
                editor_->TogglePause();
            }
        }
        if (!canCapture)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        ImGui::Text("Status: %s", ToString(status));
        if (status == FrameGraphDebugCaptureStatus::Failed)
        {
            const std::string failure = renderSystem.GetFrameGraphDebugCaptureFailure();
            if (!failure.empty())
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.95F, 0.30F, 0.30F, 1.0F), "%s", failure.c_str());
            }
        }

        ImGui::SetNextItemWidth(180.0F);
        ImGui::InputTextWithHint("##FrameGraphNameSearch", "Search names", nameSearch_, std::size(nameSearch_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(105.0F);
        ImGui::Combo("Pass", &passKindFilter_, "All\0Raster\0Compute\0");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(105.0F);
        ImGui::Combo("Resource", &resourceKindFilter_, "All\0Texture\0Buffer\0");
        ImGui::SameLine();
        ImGui::Checkbox("Show Culled", &showCulled_);

        if (debugData_ != nullptr)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Frame %llu / Request %llu / %.2fx",
                                static_cast<unsigned long long>(debugData_->frameIndex),
                                static_cast<unsigned long long>(debugData_->requestId),
                                debugData_->previewScale);
        }
    }

    void FrameGraphDebugPanel::RenderTopologyLegend()
    {
        bool first = true;
        RenderLegendEntry("Nodes: Raster", FrameGraphDebugPanelVisualRole::RasterPass, false, first);
        RenderLegendEntry("Compute", FrameGraphDebugPanelVisualRole::ComputePass, false, first);
        RenderLegendEntry("Culled", FrameGraphDebugPanelVisualRole::CulledPass, false, first);
        RenderLegendEntry("Links: RAW (write -> read)", FrameGraphDebugPanelVisualRole::RawLink, true, first);
        RenderLegendEntry("WAR (read -> write)", FrameGraphDebugPanelVisualRole::WarLink, true, first);
        RenderLegendEntry("WAW (write -> write)", FrameGraphDebugPanelVisualRole::WawLink, true, first);
        RenderLegendEntry("Pins: Texture", FrameGraphDebugPanelVisualRole::TexturePin, false, first);
        RenderLegendEntry("Buffer", FrameGraphDebugPanelVisualRole::BufferPin, false, first);
    }

    void FrameGraphDebugPanel::RenderLegendEntry(const char* label, FrameGraphDebugPanelVisualRole role, bool line, bool& first)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float swatchHeight = ImGui::GetTextLineHeight();
        const float swatchWidth = line ? 18.0F : swatchHeight;
        const float itemWidth = swatchWidth + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
        if (!first)
        {
            const float contentRight = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + itemWidth <= contentRight)
            {
                ImGui::SameLine();
            }
        }

        const ImVec2 swatchPosition = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(swatchWidth, swatchHeight));
        const ImU32 color = ImGui::ColorConvertFloat4ToU32(VisualRoleColor(role));
        if (line)
        {
            const float centerY = swatchPosition.y + swatchHeight * 0.5F;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(swatchPosition.x, centerY), ImVec2(swatchPosition.x + swatchWidth, centerY), color, 2.0F);
        }
        else
        {
            ImGui::GetWindowDrawList()->AddRectFilled(swatchPosition, ImVec2(swatchPosition.x + swatchWidth, swatchPosition.y + swatchHeight), color);
        }
        ImGui::SameLine(0.0F, style.ItemInnerSpacing.x);
        ImGui::TextUnformatted(label);
        first = false;
    }

    void FrameGraphDebugPanel::RenderTopology(float height)
    {
        if (debugData_ == nullptr)
        {
            ImGui::BeginChild("FrameGraphTopologyEmpty", ImVec2(0.0F, height), ImGuiChildFlags_Borders);
            ImGui::TextDisabled("Enter Play Mode and capture the next frame to inspect its FrameGraph.");
            ImGui::EndChild();
            return;
        }
        if (!idRegistry_.IsValid())
        {
            ImGui::BeginChild("FrameGraphTopologyInvalid", ImVec2(0.0F, height), ImGuiChildFlags_Borders);
            ImGui::TextColored(ImVec4(0.95F, 0.30F, 0.30F, 1.0F), "Captured graph contains invalid topology identifiers.");
            ImGui::EndChild();
            return;
        }
        if (!nodeLayoutValid_ || nodePositions_.size() != debugData_->passes.size())
        {
            ImGui::BeginChild("FrameGraphTopologyInvalidLayout", ImVec2(0.0F, height), ImGuiChildFlags_Borders);
            ImGui::TextColored(ImVec4(0.95F, 0.30F, 0.30F, 1.0F), "Captured graph contains an invalid dependency layout.");
            ImGui::EndChild();
            return;
        }

        struct ValidatedPassTopology
        {
            FrameGraphDebugPanelPins pins;
            FrameGraphDebugPanelVisualRole role = FrameGraphDebugPanelVisualRole::RasterPass;
            UInt64 nodeId = 0;
            bool render = false;
        };
        struct ValidatedDependencyTopology
        {
            FrameGraphDebugPanelDependencyEndpointIds endpoints;
            ImVec4 color = ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
            UInt64 linkId = 0;
            bool render = false;
        };

        std::vector<ValidatedPassTopology> validatedPasses(debugData_->passes.size());
        std::vector<ValidatedDependencyTopology> validatedDependencies(debugData_->dependencies.size());
        std::vector<UInt64> declaredPinIds;
        std::string invalidTopologyMessage;
        for (SizeT passOffset = 0; passOffset < debugData_->passes.size(); ++passOffset)
        {
            const std::optional<UInt32> convertedPassIndex = ToPanelIndex(passOffset);
            if (!convertedPassIndex.has_value())
            {
                invalidTopologyMessage = "Captured graph exceeds the supported topology pass range.";
                break;
            }
            const UInt32 passIndex = *convertedPassIndex;
            const FrameGraphDebugPass& pass = debugData_->passes[passIndex];
            if (!showCulled_ && pass.culled)
            {
                continue;
            }

            const std::optional<UInt64> nodeId = idRegistry_.FindPassNodeId(passIndex);
            const std::optional<FrameGraphDebugPanelPins> pins = BuildFrameGraphDebugPanelPins(pass);
            const std::optional<FrameGraphDebugPanelVisualRole> passRole = GetFrameGraphDebugPanelPassRole(pass);
            if (!nodeId.has_value())
            {
                invalidTopologyMessage = "Captured graph pass " + std::to_string(passIndex) + " is missing its topology node identifier.";
                break;
            }
            if (!pins.has_value())
            {
                invalidTopologyMessage = "Captured graph pass " + std::to_string(passIndex) + " contains invalid pin topology.";
                break;
            }
            if (!passRole.has_value())
            {
                invalidTopologyMessage = "Captured graph pass " + std::to_string(passIndex) + " has an invalid node visual role.";
                break;
            }

            const auto ValidatePins = [&](const std::vector<FrameGraphDebugPanelPin>& passPins, bool output)
            {
                for (const FrameGraphDebugPanelPin& pin : passPins)
                {
                    if (!GetFrameGraphDebugPanelResourceRole(pin.resourceKind).has_value())
                    {
                        invalidTopologyMessage = "Captured graph pass " + std::to_string(passIndex) + " contains an invalid resource visual role.";
                        return false;
                    }
                    if (GetResourceVersion(*debugData_, pin.resourceKind, pin.resourceIndex, pin.version) == nullptr)
                    {
                        invalidTopologyMessage = "Captured graph pass " + std::to_string(passIndex) + " references an invalid resource version.";
                        return false;
                    }
                    const std::optional<UInt64> pinId = FindPanelPinId(idRegistry_, passIndex, pin, output);
                    if (!pinId.has_value())
                    {
                        invalidTopologyMessage =
                            "Captured graph pass " + std::to_string(passIndex) + " is missing a required " + (output ? "output" : "input") + " pin identifier.";
                        return false;
                    }
                    declaredPinIds.push_back(*pinId);
                }
                return true;
            };
            if (!ValidatePins(pins->inputs, false) || !ValidatePins(pins->outputs, true))
            {
                break;
            }

            validatedPasses[passIndex] = ValidatedPassTopology{*pins, *passRole, *nodeId, true};
        }

        if (invalidTopologyMessage.empty())
        {
            for (SizeT dependencyOffset = 0; dependencyOffset < debugData_->dependencies.size(); ++dependencyOffset)
            {
                const std::optional<UInt32> convertedDependencyIndex = ToPanelIndex(dependencyOffset);
                if (!convertedDependencyIndex.has_value())
                {
                    invalidTopologyMessage = "Captured graph exceeds the supported topology dependency range.";
                    break;
                }
                const UInt32 dependencyIndex = *convertedDependencyIndex;
                const FrameGraphDebugDependency& dependency = debugData_->dependencies[dependencyIndex];
                if (!IsDependencyPassRangeValid(*debugData_, dependency))
                {
                    invalidTopologyMessage = "Captured graph dependency " + std::to_string(dependencyIndex) + " references an invalid pass.";
                    break;
                }
                if (!showCulled_ && (debugData_->passes[dependency.beforePass].culled || debugData_->passes[dependency.afterPass].culled))
                {
                    continue;
                }

                const std::optional<UInt64> linkId = idRegistry_.FindDependencyLinkId(dependencyIndex);
                const std::optional<FrameGraphDebugPanelDependencyEndpointIds> endpoints = idRegistry_.FindDependencyEndpointIds(dependencyIndex);
                if (!linkId.has_value() && !endpoints.has_value())
                {
                    continue;
                }
                if (!linkId.has_value() || !endpoints.has_value() || !GetFrameGraphDebugPanelDependencyRole(dependency.hazard).has_value())
                {
                    invalidTopologyMessage = "Captured graph dependency " + std::to_string(dependencyIndex) + " has invalid link topology.";
                    break;
                }
                if (std::find(declaredPinIds.begin(), declaredPinIds.end(), endpoints->startId) == declaredPinIds.end() ||
                    std::find(declaredPinIds.begin(), declaredPinIds.end(), endpoints->endId) == declaredPinIds.end())
                {
                    invalidTopologyMessage = "Captured graph dependency " + std::to_string(dependencyIndex) + " targets an undeclared pin.";
                    break;
                }

                validatedDependencies[dependencyIndex] = ValidatedDependencyTopology{*endpoints, DependencyColor(dependency.hazard), *linkId, true};
            }
        }

        if (!invalidTopologyMessage.empty())
        {
            ImGui::BeginChild("FrameGraphTopologyInvalidElements", ImVec2(0.0F, height), ImGuiChildFlags_Borders);
            ImGui::TextColored(ImVec4(0.95F, 0.30F, 0.30F, 1.0F), "%s", invalidTopologyMessage.c_str());
            ImGui::EndChild();
            return;
        }

        ed::SetCurrentEditor(nodeEditorContext_);
        ed::Begin("FrameGraphTopology", ImVec2(0.0F, height));
        std::optional<std::string> deferredTooltip;

        for (SizeT passOffset = 0; passOffset < debugData_->passes.size(); ++passOffset)
        {
            const std::optional<UInt32> convertedPassIndex = ToPanelIndex(passOffset);
            if (!convertedPassIndex.has_value())
            {
                break;
            }
            const UInt32 passIndex = *convertedPassIndex;
            const FrameGraphDebugPass& pass = debugData_->passes[passIndex];
            const ValidatedPassTopology& topology = validatedPasses[passIndex];
            if (!topology.render)
            {
                continue;
            }

            ed::PushStyleColor(ed::StyleColor_NodeBg, VisualRoleColor(topology.role));
            ed::BeginNode(ToNodeId(topology.nodeId));
            ImGui::PushID(static_cast<int>(passIndex));

            const FrameGraphDebugPanelDisplayLabel passLabel = BuildFrameGraphDebugPanelDisplayLabel("", pass.name, "", NodeContentWidth, &MeasurePanelText);
            (void)RenderFixedWidthLabel(passLabel, ImGui::GetStyleColorVec4(ImGuiCol_Text), NodeContentWidth, false, deferredTooltip);
            ImGui::TextDisabled("%s%s", ToString(pass.type), pass.culled ? " / Culled" : "");

            const ImVec2 separatorPosition = ImGui::GetCursorScreenPos();
            const float separatorHeight = ImGui::GetStyle().ItemSpacing.y;
            ImGui::Dummy(ImVec2(NodeContentWidth, separatorHeight));
            const float separatorY = separatorPosition.y + separatorHeight * 0.5F;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(separatorPosition.x, separatorY),
                                                ImVec2(separatorPosition.x + NodeContentWidth, separatorY),
                                                ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_Separator)));

            const SizeT rowCount = (std::max)({topology.pins.inputs.size(), topology.pins.outputs.size(), SizeT{1}});
            for (SizeT row = 0; row < rowCount; ++row)
            {
                if (row < topology.pins.inputs.size())
                {
                    RenderNodePin(passIndex, topology.pins.inputs[row], false, deferredTooltip);
                }
                else
                {
                    ImGui::Dummy(ImVec2(NodePinColumnWidth, ImGui::GetTextLineHeight()));
                }

                ImGui::SameLine(0.0F, NodePinColumnGap);
                if (row < topology.pins.outputs.size())
                {
                    RenderNodePin(passIndex, topology.pins.outputs[row], true, deferredTooltip);
                }
                else
                {
                    ImGui::Dummy(ImVec2(NodePinColumnWidth, ImGui::GetTextLineHeight()));
                }
            }

            ImGui::PopID();
            ed::EndNode();
            ed::PopStyleColor();

            if (passIndex < positionedNodes_.size() && !positionedNodes_[passIndex])
            {
                const FrameGraphDebugPanelNodePosition& position = nodePositions_[passIndex];
                ed::SetNodePosition(ToNodeId(topology.nodeId),
                                    ImVec2(static_cast<float>(position.column) * NodeColumnSpacing, static_cast<float>(position.row) * NodeRowSpacing));
                positionedNodes_[passIndex] = true;
            }
        }

        for (SizeT dependencyOffset = 0; dependencyOffset < debugData_->dependencies.size(); ++dependencyOffset)
        {
            const std::optional<UInt32> convertedDependencyIndex = ToPanelIndex(dependencyOffset);
            if (!convertedDependencyIndex.has_value())
            {
                break;
            }
            const UInt32 dependencyIndex = *convertedDependencyIndex;
            const ValidatedDependencyTopology& topology = validatedDependencies[dependencyIndex];
            if (topology.render)
            {
                ed::Link(ToLinkId(topology.linkId), ToPinId(topology.endpoints.startId), ToPinId(topology.endpoints.endId), topology.color, 2.0F);
            }
        }

        if (navigateToSelection_)
        {
            ed::NavigateToSelection(true);
            navigateToSelection_ = false;
        }
        ed::End();

        if (deferredTooltip.has_value())
        {
            ImGui::SetTooltip("%s", deferredTooltip->c_str());
        }

        if (ed::HasSelectionChanged())
        {
            ed::NodeId node;
            if (ed::GetSelectedNodes(&node, 1) > 0)
            {
                const std::optional<FrameGraphDebugPanelElement> element = idRegistry_.FindElement(FromNodeId(node));
                if (element.has_value())
                {
                    SelectPass(element->passIndex, false);
                }
            }
            else
            {
                ed::LinkId link;
                if (ed::GetSelectedLinks(&link, 1) > 0)
                {
                    const std::optional<FrameGraphDebugPanelElement> element = idRegistry_.FindElement(FromLinkId(link));
                    if (element.has_value())
                    {
                        SelectDependency(element->dependencyIndex, false);
                    }
                }
            }
        }

        const ed::PinId hoveredPin = ed::GetHoveredPin();
        if (hoveredPin && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const std::optional<FrameGraphDebugPanelElement> element = idRegistry_.FindElement(FromPinId(hoveredPin));
            if (element.has_value())
            {
                const bool texture =
                    element->kind == FrameGraphDebugPanelElementKind::TextureInputPin || element->kind == FrameGraphDebugPanelElementKind::TextureOutputPin;
                SelectResource(
                    texture ? FrameGraphDebugResourceKind::Texture : FrameGraphDebugResourceKind::Buffer, element->resourceIndex, element->version, false);
            }
        }
        ed::SetCurrentEditor(nullptr);
    }

    void FrameGraphDebugPanel::RenderNodePin(
        UInt32 passIndex, const FrameGraphDebugPanelPin& pin, bool output, std::optional<std::string>& deferredTooltip)
    {
        if (debugData_ == nullptr || passIndex >= debugData_->passes.size())
        {
            return;
        }

        const std::optional<FrameGraphDebugPanelVisualRole> role = GetFrameGraphDebugPanelResourceRole(pin.resourceKind);
        if (!role.has_value())
        {
            return;
        }

        const std::optional<UInt64> pinId = FindPanelPinId(idRegistry_, passIndex, pin, output);
        if (!pinId.has_value())
        {
            return;
        }

        const ImVec4 color = VisualRoleColor(*role);
        const std::string versionSuffix = " v" + std::to_string(pin.version) + (output ? " ->" : "");
        const std::string_view prefix = output ? std::string_view{} : std::string_view{"<- "};
        const FrameGraphDebugPanelDisplayLabel label = BuildFrameGraphDebugPanelDisplayLabel(
            prefix, GetResourceName(*debugData_, pin.resourceKind, pin.resourceIndex), versionSuffix, NodePinColumnWidth, &MeasurePanelText);

        ed::PushStyleColor(ed::StyleColor_PinRect, color);
        ed::PushStyleColor(ed::StyleColor_PinRectBorder, color);
        ed::BeginPin(ToPinId(*pinId), output ? ed::PinKind::Output : ed::PinKind::Input);
        ed::PinPivotAlignment(output ? ImVec2(1.0F, 0.5F) : ImVec2(0.0F, 0.5F));
        const FixedWidthLabelRect labelRect = RenderFixedWidthLabel(label, color, NodePinColumnWidth, output, deferredTooltip);
        ed::PinRect(labelRect.minimum, labelRect.maximum);
        ed::EndPin();
        ed::PopStyleColor(2);
    }

    void FrameGraphDebugPanel::RenderTables()
    {
        if (debugData_ == nullptr)
        {
            ImGui::TextDisabled("No capture available.");
            return;
        }
        if (ImGui::BeginTabBar("FrameGraphDebugTableTabs"))
        {
            if (ImGui::BeginTabItem("Passes"))
            {
                RenderPassTable();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Resources"))
            {
                RenderResourceTable();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Dependencies"))
            {
                RenderDependencyTable();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void FrameGraphDebugPanel::RenderPassTable()
    {
        std::vector<UInt32> indices(debugData_->passes.size());
        std::iota(indices.begin(), indices.end(), 0U);
        constexpr ImGuiTableFlags Flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
                                          ImGuiTableFlags_SortMulti | ImGuiTableFlags_Resizable;
        if (!ImGui::BeginTable("FrameGraphPassTable", 5, Flags))
        {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Reg", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 54.0F, 0);
        ImGui::TableSetupColumn("Compiled", ImGuiTableColumnFlags_WidthFixed, 72.0F, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.0F, 2);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 70.0F, 3);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 62.0F, 4);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0)
        {
            std::stable_sort(
                indices.begin(),
                indices.end(),
                [&](UInt32 leftIndex, UInt32 rightIndex)
                {
                    const FrameGraphDebugPass& left = debugData_->passes[leftIndex];
                    const FrameGraphDebugPass& right = debugData_->passes[rightIndex];
                    for (int specIndex = 0; specIndex < specs->SpecsCount; ++specIndex)
                    {
                        const ImGuiTableColumnSortSpecs& spec = specs->Specs[specIndex];
                        int comparison = 0;
                        if (spec.ColumnUserID == 0)
                        {
                            comparison = left.registrationIndex < right.registrationIndex ? -1 : left.registrationIndex > right.registrationIndex ? 1 : 0;
                        }
                        else if (spec.ColumnUserID == 1)
                        {
                            const UInt32 leftCompiled = left.compiledIndex.value_or((std::numeric_limits<UInt32>::max)());
                            const UInt32 rightCompiled = right.compiledIndex.value_or((std::numeric_limits<UInt32>::max)());
                            comparison = leftCompiled < rightCompiled ? -1 : leftCompiled > rightCompiled ? 1 : 0;
                        }
                        else if (spec.ColumnUserID == 2)
                        {
                            comparison = left.name.compare(right.name);
                        }
                        if (comparison != 0)
                        {
                            return spec.SortDirection == ImGuiSortDirection_Ascending ? comparison < 0 : comparison > 0;
                        }
                    }
                    return leftIndex < rightIndex;
                });
        }

        for (UInt32 passIndex : indices)
        {
            const FrameGraphDebugPass& pass = debugData_->passes[passIndex];
            if ((!showCulled_ && pass.culled) || (passKindFilter_ == 1 && pass.type != FrameGraphDebugPassType::Raster) ||
                (passKindFilter_ == 2 && pass.type != FrameGraphDebugPassType::Compute) || !ContainsCaseInsensitive(pass.name, nameSearch_))
            {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool selected = selectionKind_ == SelectionKind::Pass && selectedPassIndex_ == passIndex;
            const std::string rowId = "##FrameGraphPass" + std::to_string(passIndex);
            if (ImGui::Selectable(rowId.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
            {
                SelectPass(passIndex, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
            }
            ImGui::SameLine();
            ImGui::Text("%u", pass.registrationIndex);
            ImGui::TableNextColumn();
            pass.compiledIndex.has_value() ? ImGui::Text("%u", *pass.compiledIndex) : ImGui::TextUnformatted("-");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(pass.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ToString(pass.type));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(pass.culled ? "Culled" : pass.retained ? "Retained" : "Other");
        }
        ImGui::EndTable();
    }

    void FrameGraphDebugPanel::RenderResourceTable()
    {
        constexpr ImGuiTableFlags Flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
        if (!ImGui::BeginTable("FrameGraphResourceTable", 6, Flags))
        {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 66.0F);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 58.0F);
        ImGui::TableSetupColumn("Producer", ImGuiTableColumnFlags_WidthFixed, 64.0F);
        ImGui::TableSetupColumn("Lifetime", ImGuiTableColumnFlags_WidthFixed, 78.0F);
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, 76.0F);
        ImGui::TableHeadersRow();

        const auto emitResource = [&](FrameGraphDebugResourceKind kind, UInt32 resourceIndex, const std::string& name, const auto& resource)
        {
            if ((resourceKindFilter_ == 1 && kind != FrameGraphDebugResourceKind::Texture) ||
                (resourceKindFilter_ == 2 && kind != FrameGraphDebugResourceKind::Buffer) || !ContainsCaseInsensitive(name, nameSearch_))
            {
                return;
            }
            for (SizeT versionOffset = 0; versionOffset < resource.versions.size(); ++versionOffset)
            {
                const std::optional<UInt32> convertedVersion = ToPanelIndex(versionOffset);
                if (!convertedVersion.has_value())
                {
                    break;
                }
                const UInt32 version = *convertedVersion;
                const FrameGraphDebugResourceVersion& record = resource.versions[version];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool selected = selectionKind_ == SelectionKind::Resource && selectedResourceKind_ == kind && selectedResourceIndex_ == resourceIndex &&
                                      selectedVersion_ == version;
                const std::string rowId =
                    "##FrameGraphResource" + std::to_string(static_cast<UInt32>(kind)) + ":" + std::to_string(resourceIndex) + ":" + std::to_string(version);
                if (ImGui::Selectable(rowId.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                {
                    SelectResource(kind, resourceIndex, version, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(ToString(kind));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%u", version);
                ImGui::TableNextColumn();
                record.producer.has_value() ? ImGui::Text("%u", *record.producer) : ImGui::TextUnformatted("-");
                ImGui::TableNextColumn();
                if (record.firstCompiledUse.has_value() && record.lastCompiledUse.has_value())
                {
                    ImGui::Text("%u..%u", *record.firstCompiledUse, *record.lastCompiledUse);
                }
                else
                {
                    ImGui::TextUnformatted("-");
                }
                ImGui::TableNextColumn();
                if constexpr (std::is_same_v<std::decay_t<decltype(resource)>, FrameGraphDebugTexture>)
                {
                    ImGui::TextUnformatted(ToString(record.preview.state));
                }
                else
                {
                    ImGui::TextDisabled("Metadata");
                }
            }
        };

        for (SizeT textureOffset = 0; textureOffset < debugData_->textures.size(); ++textureOffset)
        {
            const std::optional<UInt32> convertedTextureIndex = ToPanelIndex(textureOffset);
            if (!convertedTextureIndex.has_value())
            {
                break;
            }
            const UInt32 textureIndex = *convertedTextureIndex;
            emitResource(FrameGraphDebugResourceKind::Texture, textureIndex, debugData_->textures[textureIndex].name, debugData_->textures[textureIndex]);
        }
        for (SizeT bufferOffset = 0; bufferOffset < debugData_->buffers.size(); ++bufferOffset)
        {
            const std::optional<UInt32> convertedBufferIndex = ToPanelIndex(bufferOffset);
            if (!convertedBufferIndex.has_value())
            {
                break;
            }
            const UInt32 bufferIndex = *convertedBufferIndex;
            emitResource(FrameGraphDebugResourceKind::Buffer, bufferIndex, debugData_->buffers[bufferIndex].name, debugData_->buffers[bufferIndex]);
        }
        ImGui::EndTable();
    }

    void FrameGraphDebugPanel::RenderDependencyTable()
    {
        constexpr ImGuiTableFlags Flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
        if (!ImGui::BeginTable("FrameGraphDependencyTable", 6, Flags))
        {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Hazard", ImGuiTableColumnFlags_WidthFixed, 58.0F);
        ImGui::TableSetupColumn("Before", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("After", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 62.0F);
        ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 56.0F);
        ImGui::TableHeadersRow();
        for (SizeT dependencyOffset = 0; dependencyOffset < debugData_->dependencies.size(); ++dependencyOffset)
        {
            const std::optional<UInt32> convertedDependencyIndex = ToPanelIndex(dependencyOffset);
            if (!convertedDependencyIndex.has_value())
            {
                break;
            }
            const UInt32 dependencyIndex = *convertedDependencyIndex;
            const FrameGraphDebugDependency& dependency = debugData_->dependencies[dependencyIndex];
            if (!IsDependencyPassRangeValid(*debugData_, dependency))
            {
                continue;
            }
            const FrameGraphDebugPass& before = debugData_->passes[dependency.beforePass];
            const FrameGraphDebugPass& after = debugData_->passes[dependency.afterPass];
            const std::string& resourceName = GetResourceName(*debugData_, dependency.resourceKind, dependency.resourceIndex);
            if ((!showCulled_ && (before.culled || after.culled)) ||
                (resourceKindFilter_ == 1 && dependency.resourceKind != FrameGraphDebugResourceKind::Texture) ||
                (resourceKindFilter_ == 2 && dependency.resourceKind != FrameGraphDebugResourceKind::Buffer) ||
                (!ContainsCaseInsensitive(before.name, nameSearch_) && !ContainsCaseInsensitive(after.name, nameSearch_) &&
                 !ContainsCaseInsensitive(resourceName, nameSearch_)))
            {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool selected = selectionKind_ == SelectionKind::Dependency && selectedDependencyIndex_ == dependencyIndex;
            const std::string rowId = "##FrameGraphDependency" + std::to_string(dependencyIndex);
            if (ImGui::Selectable(rowId.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
            {
                SelectDependency(dependencyIndex, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
            }
            ImGui::SameLine();
            ImGui::TextColored(DependencyColor(dependency.hazard), "%s", ToString(dependency.hazard));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(before.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(after.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ToString(dependency.resourceKind));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(resourceName.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%u", dependency.version);
        }
        ImGui::EndTable();
    }

    void FrameGraphDebugPanel::RenderDetails()
    {
        if (debugData_ == nullptr)
        {
            ImGui::TextDisabled("No capture available.");
            return;
        }

        switch (selectionKind_)
        {
        case SelectionKind::Pass:
            if (selectedPassIndex_ < debugData_->passes.size())
            {
                RenderPassDetails(debugData_->passes[selectedPassIndex_]);
            }
            break;
        case SelectionKind::Resource:
            RenderResourceDetails();
            break;
        case SelectionKind::Dependency:
            if (selectedDependencyIndex_ < debugData_->dependencies.size())
            {
                RenderDependencyDetails(debugData_->dependencies[selectedDependencyIndex_]);
            }
            break;
        case SelectionKind::None:
        default:
            ImGui::TextDisabled("Select a pass, resource version, or dependency.");
            break;
        }
    }

    void FrameGraphDebugPanel::RenderPreviewPane()
    {
        if (debugData_ == nullptr)
        {
            ImGui::TextDisabled("No capture available.");
            return;
        }
        if (selectionKind_ != SelectionKind::Resource || selectedResourceKind_ != FrameGraphDebugResourceKind::Texture ||
            selectedResourceIndex_ >= debugData_->textures.size())
        {
            ImGui::TextDisabled("Select a texture resource version to preview it.");
            return;
        }

        const FrameGraphDebugTexture& texture = debugData_->textures[selectedResourceIndex_];
        ImGui::Text("Texture: %s", texture.name.c_str());
        ImGui::Text("Version: %u", selectedVersion_);
        ImGui::Separator();
        RenderTexturePreview(texture, selectedVersion_);
    }

    void FrameGraphDebugPanel::RenderPassDetails(const FrameGraphDebugPass& pass)
    {
        ImGui::Text("Pass: %s", pass.name.c_str());
        ImGui::Text("Type: %s", ToString(pass.type));
        ImGui::Text("Registration: %u", pass.registrationIndex);
        TextOptionalIndex("Compiled", pass.compiledIndex);
        ImGui::Text("Retained: %s", pass.retained ? "yes" : "no");
        ImGui::Text("Culled: %s", pass.culled ? "yes" : "no");
        ImGui::Text("Side effect: %s", pass.sideEffect ? "yes" : "no");

        if (ImGui::CollapsingHeader("Accesses", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (const FrameGraphDebugAccess& access : pass.accesses)
            {
                ImGui::BulletText("%s %s[%u] v%u%s%s; value=%u; %s; write=%s",
                                  ToString(access.resourceKind),
                                  GetResourceName(*debugData_, access.resourceKind, access.resourceIndex).c_str(),
                                  access.resourceIndex,
                                  access.inputVersion,
                                  access.outputVersion.has_value() ? " -> v" : "",
                                  access.outputVersion.has_value() ? std::to_string(*access.outputVersion).c_str() : "",
                                  access.accessValue,
                                  AccessName(access),
                                  access.write ? "yes" : "no");
            }
        }
        if (ImGui::CollapsingHeader("Attachments", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (const FrameGraphDebugAttachment& attachment : pass.attachments)
            {
                ImGui::BulletText("Texture[%u] v%u; %s; load=%s; store=%s; readOnly=%s",
                                  attachment.textureIndex,
                                  attachment.version,
                                  attachment.depth ? "Depth" : "Color",
                                  ToString(attachment.loadAction),
                                  ToString(attachment.storeAction),
                                  attachment.readOnly ? "yes" : "no");
            }
        }
        if (ImGui::CollapsingHeader("UAV Barriers", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (const FrameGraphTextureHandle handle : pass.textureUavBarriers)
            {
                ImGui::BulletText("Texture[%u] v%u", handle.index, handle.version);
            }
            for (const FrameGraphBufferHandle handle : pass.bufferUavBarriers)
            {
                ImGui::BulletText("Buffer[%u] v%u", handle.index, handle.version);
            }
        }
        if (ImGui::CollapsingHeader("Dependencies", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (const FrameGraphDebugDependency& dependency : debugData_->dependencies)
            {
                if (dependency.beforePass == selectedPassIndex_ || dependency.afterPass == selectedPassIndex_)
                {
                    ImGui::BulletText("%s: pass %u -> %u, %s[%u] v%u",
                                      ToString(dependency.hazard),
                                      dependency.beforePass,
                                      dependency.afterPass,
                                      ToString(dependency.resourceKind),
                                      dependency.resourceIndex,
                                      dependency.version);
                }
            }
        }
    }

    void FrameGraphDebugPanel::RenderResourceDetails()
    {
        const FrameGraphDebugResourceVersion* version = GetResourceVersion(*debugData_, selectedResourceKind_, selectedResourceIndex_, selectedVersion_);
        if (version == nullptr)
        {
            ImGui::TextDisabled("Selected resource version is unavailable.");
            return;
        }

        const std::string& name = GetResourceName(*debugData_, selectedResourceKind_, selectedResourceIndex_);
        ImGui::Text("%s: %s", ToString(selectedResourceKind_), name.c_str());
        ImGui::Text("Index: %u", selectedResourceIndex_);
        ImGui::Text("Version: %u", selectedVersion_);
        TextOptionalIndex("Producer pass", version->producer);
        TextOptionalIndex("First compiled use", version->firstCompiledUse);
        TextOptionalIndex("Last compiled use", version->lastCompiledUse);
        ImGui::Text("Exported: %s", version->exported ? "yes" : "no");
        ImGui::Text("Readers:");
        if (version->readers.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("none");
        }
        else
        {
            for (UInt32 reader : version->readers)
            {
                ImGui::SameLine();
                ImGui::Text("%u", reader);
            }
        }

        if (selectedResourceKind_ == FrameGraphDebugResourceKind::Buffer)
        {
            const FrameGraphDebugBuffer& buffer = debugData_->buffers[selectedResourceIndex_];
            ImGui::Text("Size: %llu bytes", static_cast<unsigned long long>(buffer.size));
            ImGui::Separator();
            ImGui::TextDisabled("Buffer contents are metadata-only and are not captured.");
            return;
        }

        const FrameGraphDebugTexture& texture = debugData_->textures[selectedResourceIndex_];
        ImGui::Text("Dimension: Texture2D");
        ImGui::Text("Extent: %u x %u x %u", texture.desc.width, texture.desc.height, texture.desc.depth);
        ImGui::Text("Mip levels: %u", texture.desc.mipLevelCount);
        ImGui::Text("Format: %s", ToString(texture.desc.format));
        ImGui::Text("Usage: %s (0x%X)", TextureUsageText(texture.desc.usage), static_cast<UInt32>(texture.desc.usage));
        ImGui::Text("Imported: %s", texture.imported ? "yes" : "no");
        ImGui::Text("Swapchain: %s", texture.swapchain ? "yes" : "no");
        ImGui::Separator();
        RenderTexturePreview(texture, selectedVersion_);
    }

    void FrameGraphDebugPanel::RenderDependencyDetails(const FrameGraphDebugDependency& dependency)
    {
        ImGui::TextColored(DependencyColor(dependency.hazard), "Hazard: %s", ToString(dependency.hazard));
        if (!IsDependencyPassRangeValid(*debugData_, dependency))
        {
            ImGui::TextColored(ImVec4(0.95F, 0.30F, 0.30F, 1.0F),
                               "Invalid pass references: %u -> %u (pass count: %llu)",
                               dependency.beforePass,
                               dependency.afterPass,
                               static_cast<unsigned long long>(debugData_->passes.size()));
            return;
        }
        ImGui::Text("Before pass: %u (%s)", dependency.beforePass, debugData_->passes[dependency.beforePass].name.c_str());
        ImGui::Text("After pass: %u (%s)", dependency.afterPass, debugData_->passes[dependency.afterPass].name.c_str());
        ImGui::Text("Resource: %s[%u] %s",
                    ToString(dependency.resourceKind),
                    dependency.resourceIndex,
                    GetResourceName(*debugData_, dependency.resourceKind, dependency.resourceIndex).c_str());
        ImGui::Text("Logical version: %u", dependency.version);
        const FrameGraphDebugResourceVersion* version = GetResourceVersion(*debugData_, dependency.resourceKind, dependency.resourceIndex, dependency.version);
        if (version != nullptr)
        {
            TextOptionalIndex("Producer", version->producer);
            TextOptionalIndex("First compiled use", version->firstCompiledUse);
            TextOptionalIndex("Last compiled use", version->lastCompiledUse);
        }
    }

    void FrameGraphDebugPanel::RenderTexturePreview(const FrameGraphDebugTexture& texture, UInt32 versionIndex)
    {
        if (versionIndex >= texture.versions.size())
        {
            ImGui::TextDisabled("Preview unavailable: invalid version.");
            return;
        }
        const FrameGraphDebugPreview& preview = texture.versions[versionIndex].preview;
        ImGui::Text("Preview: %s", ToString(preview.state));
        ImGui::Text("Source: %u x %u %s", preview.sourceExtent.width, preview.sourceExtent.height, ToString(preview.sourceFormat));
        ImGui::Text("Captured: %u x %u", preview.previewExtent.width, preview.previewExtent.height);
        if (preview.state != FrameGraphDebugPreviewState::Ready || preview.texture == nullptr)
        {
            ImGui::TextDisabled("%s", preview.message.empty() ? "Preview is unavailable." : preview.message.c_str());
            return;
        }
        void* handle = preview.texture->GetNativeSampledViewHandle();
        if (handle == nullptr)
        {
            ImGui::TextDisabled("Preview is ready, but no native sampled view is available.");
            return;
        }

        if (ImGui::Button("Fit"))
        {
            previewFit_ = true;
            previewZoom_ = 1.0F;
            previewPanX_ = 0.0F;
            previewPanY_ = 0.0F;
        }
        ImGui::SameLine();
        if (ImGui::Button("1:1"))
        {
            previewFit_ = false;
            previewZoom_ = 1.0F;
            previewPanX_ = 0.0F;
            previewPanY_ = 0.0F;
        }
        ImGui::SameLine();
        ImGui::Text("Zoom %.2fx", previewFit_ ? previewFitScale_ * previewZoom_ : previewZoom_);

        const float previewCanvasHeight = (std::max)(MinimumPreviewCanvasHeight, ImGui::GetContentRegionAvail().y);
        if (!ImGui::BeginChild("FrameGraphTexturePreviewCanvas", ImVec2(0.0F, previewCanvasHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar))
        {
            ImGui::EndChild();
            return;
        }
        const ImVec2 canvasPosition = ImGui::GetCursorScreenPos();
        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        const float width = static_cast<float>(preview.previewExtent.width);
        const float height = static_cast<float>(preview.previewExtent.height);
        previewFitScale_ = width > 0.0F && height > 0.0F ? (std::min)(canvasSize.x / width, canvasSize.y / height) : 1.0F;
        const float scale = (previewFit_ ? previewFitScale_ : 1.0F) * previewZoom_;
        const ImVec2 imageSize(width * scale, height * scale);

        if (ImGui::IsWindowHovered())
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0F)
            {
                previewZoom_ = std::clamp(previewZoom_ * std::pow(1.15F, wheel), MinimumPreviewZoom, MaximumPreviewZoom);
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            {
                previewPanX_ += ImGui::GetIO().MouseDelta.x;
                previewPanY_ += ImGui::GetIO().MouseDelta.y;
            }
        }

        const ImVec2 imagePosition(canvasPosition.x + (canvasSize.x - imageSize.x) * 0.5F + previewPanX_,
                                   canvasPosition.y + (canvasSize.y - imageSize.y) * 0.5F + previewPanY_);
        ImGui::SetCursorScreenPos(imagePosition);
        const ImTextureRef textureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(handle)));
        ImGui::Image(textureRef, imageSize);
        ImGui::EndChild();
    }

    void FrameGraphDebugPanel::ResetSnapshotUiState()
    {
        selectionKind_ = SelectionKind::None;
        selectedPassIndex_ = InvalidFrameGraphDebugPassIndex;
        selectedResourceIndex_ = InvalidFrameGraphResourceIndex;
        selectedVersion_ = 0;
        selectedDependencyIndex_ = InvalidFrameGraphDebugPassIndex;
        previewFit_ = true;
        previewZoom_ = 1.0F;
        previewPanX_ = 0.0F;
        previewPanY_ = 0.0F;
        navigateToSelection_ = false;
        nodePositions_.clear();
        nodeLayoutValid_ = false;
        if (debugData_ != nullptr)
        {
            (void)idRegistry_.Reset(*debugData_);
            std::optional<std::vector<FrameGraphDebugPanelNodePosition>> positions = BuildFrameGraphDebugPanelLayout(*debugData_);
            if (positions.has_value())
            {
                nodePositions_ = std::move(*positions);
                nodeLayoutValid_ = true;
            }
            positionedNodes_.assign(debugData_->passes.size(), false);
        }
        else
        {
            idRegistry_.Clear();
            positionedNodes_.clear();
        }
    }

    void FrameGraphDebugPanel::SelectPass(UInt32 passIndex, bool navigate)
    {
        if (debugData_ == nullptr || passIndex >= debugData_->passes.size())
        {
            return;
        }
        selectionKind_ = SelectionKind::Pass;
        selectedPassIndex_ = passIndex;
        if (navigate)
        {
            const std::optional<UInt64> nodeId = idRegistry_.FindPassNodeId(passIndex);
            if (nodeId.has_value())
            {
                ed::SetCurrentEditor(nodeEditorContext_);
                ed::SelectNode(ToNodeId(*nodeId));
                ed::SetCurrentEditor(nullptr);
                navigateToSelection_ = true;
            }
        }
    }

    void FrameGraphDebugPanel::SelectResource(FrameGraphDebugResourceKind kind, UInt32 resourceIndex, UInt32 version, bool navigate)
    {
        if (debugData_ == nullptr || GetResourceVersion(*debugData_, kind, resourceIndex, version) == nullptr)
        {
            return;
        }
        selectionKind_ = SelectionKind::Resource;
        selectedResourceKind_ = kind;
        selectedResourceIndex_ = resourceIndex;
        selectedVersion_ = version;
        previewFit_ = true;
        previewZoom_ = 1.0F;
        previewPanX_ = 0.0F;
        previewPanY_ = 0.0F;
        if (!navigate)
        {
            return;
        }

        const FrameGraphDebugResourceVersion* resourceVersion = GetResourceVersion(*debugData_, kind, resourceIndex, version);
        std::optional<UInt32> passIndex = resourceVersion->producer;
        if (!passIndex.has_value() && !resourceVersion->readers.empty())
        {
            passIndex = resourceVersion->readers.front();
        }
        if (passIndex.has_value())
        {
            const std::optional<UInt64> nodeId = idRegistry_.FindPassNodeId(*passIndex);
            if (nodeId.has_value())
            {
                ed::SetCurrentEditor(nodeEditorContext_);
                ed::CenterNodeOnScreen(ToNodeId(*nodeId));
                ed::SetCurrentEditor(nullptr);
            }
        }
    }

    void FrameGraphDebugPanel::SelectDependency(UInt32 dependencyIndex, bool navigate)
    {
        if (debugData_ == nullptr || dependencyIndex >= debugData_->dependencies.size())
        {
            return;
        }
        selectionKind_ = SelectionKind::Dependency;
        selectedDependencyIndex_ = dependencyIndex;
        if (navigate)
        {
            const std::optional<UInt64> linkId = idRegistry_.FindDependencyLinkId(dependencyIndex);
            if (linkId.has_value())
            {
                ed::SetCurrentEditor(nodeEditorContext_);
                ed::SelectLink(ToLinkId(*linkId));
                ed::SetCurrentEditor(nullptr);
                navigateToSelection_ = true;
            }
        }
    }
} // namespace ve::editor
