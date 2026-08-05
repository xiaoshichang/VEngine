#include "FrameGraphDebugPanelModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace ve::editor
{
    namespace
    {
        [[nodiscard]] FrameGraphDebugPanelElement MakePassNodeElement(UInt32 passIndex) noexcept
        {
            FrameGraphDebugPanelElement element;
            element.kind = FrameGraphDebugPanelElementKind::PassNode;
            element.passIndex = passIndex;
            return element;
        }

        [[nodiscard]] std::optional<FrameGraphDebugPanelElement>
        MakePinElement(FrameGraphDebugResourceKind resourceKind, UInt32 passIndex, UInt32 resourceIndex, UInt32 version, bool output) noexcept
        {
            FrameGraphDebugPanelElement element;
            switch (resourceKind)
            {
            case FrameGraphDebugResourceKind::Texture:
                element.kind = output ? FrameGraphDebugPanelElementKind::TextureOutputPin : FrameGraphDebugPanelElementKind::TextureInputPin;
                break;
            case FrameGraphDebugResourceKind::Buffer:
                element.kind = output ? FrameGraphDebugPanelElementKind::BufferOutputPin : FrameGraphDebugPanelElementKind::BufferInputPin;
                break;
            default:
                return std::nullopt;
            }
            element.passIndex = passIndex;
            element.resourceIndex = resourceIndex;
            element.version = version;
            return element;
        }

        [[nodiscard]] FrameGraphDebugPanelElement MakeDependencyLinkElement(UInt32 dependencyIndex) noexcept
        {
            FrameGraphDebugPanelElement element;
            element.kind = FrameGraphDebugPanelElementKind::DependencyLink;
            element.dependencyIndex = dependencyIndex;
            return element;
        }

        [[nodiscard]] bool IsKnownResourceKind(FrameGraphDebugResourceKind resourceKind) noexcept
        {
            switch (resourceKind)
            {
            case FrameGraphDebugResourceKind::Texture:
            case FrameGraphDebugResourceKind::Buffer:
                return true;
            default:
                return false;
            }
        }

        void AddUniquePin(std::vector<FrameGraphDebugPanelPin>& pins, const FrameGraphDebugPanelPin& pin)
        {
            if (std::find(pins.begin(), pins.end(), pin) == pins.end())
            {
                pins.push_back(pin);
            }
        }

        [[nodiscard]] SizeT PreviousUtf8CodePointStart(std::string_view text, SizeT end) noexcept
        {
            if (end == 0)
            {
                return 0;
            }

            SizeT position = end - 1;
            while (position > 0 && (static_cast<UInt8>(text[position]) & 0xC0U) == 0x80U)
            {
                --position;
            }
            return position;
        }

        [[nodiscard]] std::string BuildDisplayLabelText(std::string_view prefix, std::string_view value, std::string_view suffix)
        {
            std::string text;
            text.reserve(prefix.size() + value.size() + suffix.size());
            text.append(prefix);
            text.append(value);
            text.append(suffix);
            return text;
        }

        [[nodiscard]] bool AddAccessElements(std::vector<FrameGraphDebugPanelElement>& elements, UInt32 passIndex, const FrameGraphDebugAccess& access)
        {
            if (access.resourceIndex == InvalidFrameGraphResourceIndex)
            {
                return false;
            }

            const std::optional<FrameGraphDebugPanelElement> input =
                MakePinElement(access.resourceKind, passIndex, access.resourceIndex, access.inputVersion, false);
            if (!input.has_value())
            {
                return false;
            }
            elements.push_back(*input);

            if (access.outputVersion.has_value())
            {
                const std::optional<FrameGraphDebugPanelElement> output =
                    MakePinElement(access.resourceKind, passIndex, access.resourceIndex, *access.outputVersion, true);
                if (!output.has_value())
                {
                    return false;
                }
                elements.push_back(*output);
            }
            return true;
        }

        [[nodiscard]] std::optional<std::pair<FrameGraphDebugPanelElement, FrameGraphDebugPanelElement>>
        MakeDependencyEndpoints(const FrameGraphDebugDependency& dependency) noexcept
        {
            bool startOutput = false;
            switch (dependency.hazard)
            {
            case FrameGraphDebugDependencyHazard::Raw:
            case FrameGraphDebugDependencyHazard::Waw:
                startOutput = true;
                break;
            case FrameGraphDebugDependencyHazard::War:
                startOutput = false;
                break;
            default:
                return std::nullopt;
            }

            const std::optional<FrameGraphDebugPanelElement> start =
                MakePinElement(dependency.resourceKind, dependency.beforePass, dependency.resourceIndex, dependency.version, startOutput);
            const std::optional<FrameGraphDebugPanelElement> end =
                MakePinElement(dependency.resourceKind, dependency.afterPass, dependency.resourceIndex, dependency.version, false);
            if (!start.has_value() || !end.has_value())
            {
                return std::nullopt;
            }
            return std::pair(*start, *end);
        }
    } // namespace

    FrameGraphDebugPanelIdRegistry::FrameGraphDebugPanelIdRegistry(const FrameGraphDebugData& data)
    {
        (void)Reset(data);
    }

    bool FrameGraphDebugPanelIdRegistry::Reset(const FrameGraphDebugData& data)
    {
        Clear();
        if (data.passes.size() > std::numeric_limits<UInt32>::max() || data.dependencies.size() > std::numeric_limits<UInt32>::max())
        {
            return false;
        }

        for (SizeT passIndex = 0; passIndex < data.passes.size(); ++passIndex)
        {
            const UInt32 panelPassIndex = static_cast<UInt32>(passIndex);
            elements_.push_back(MakePassNodeElement(panelPassIndex));
            for (const FrameGraphDebugAccess& access : data.passes[passIndex].accesses)
            {
                if (!AddAccessElements(elements_, panelPassIndex, access))
                {
                    Clear();
                    return false;
                }
            }
        }

        std::sort(elements_.begin(), elements_.end());
        elements_.erase(std::unique(elements_.begin(), elements_.end()), elements_.end());
        dependencyEndpoints_.resize(data.dependencies.size());

        for (SizeT dependencyIndex = 0; dependencyIndex < data.dependencies.size(); ++dependencyIndex)
        {
            const UInt32 panelDependencyIndex = static_cast<UInt32>(dependencyIndex);
            const FrameGraphDebugDependency& dependency = data.dependencies[dependencyIndex];
            if (dependency.beforePass == InvalidFrameGraphDebugPassIndex || dependency.afterPass == InvalidFrameGraphDebugPassIndex ||
                dependency.beforePass >= data.passes.size() || dependency.afterPass >= data.passes.size() ||
                dependency.resourceIndex == InvalidFrameGraphResourceIndex)
            {
                Clear();
                return false;
            }

            const std::optional<std::pair<FrameGraphDebugPanelElement, FrameGraphDebugPanelElement>> endpoints = MakeDependencyEndpoints(dependency);
            if (!endpoints.has_value())
            {
                Clear();
                return false;
            }
            if (!std::binary_search(elements_.begin(), elements_.end(), endpoints->first) ||
                !std::binary_search(elements_.begin(), elements_.end(), endpoints->second))
            {
                continue;
            }

            dependencyEndpoints_[dependencyIndex] = DependencyEndpointElements{endpoints->first, endpoints->second};
            elements_.push_back(MakeDependencyLinkElement(panelDependencyIndex));
        }

        std::sort(elements_.begin(), elements_.end());
        elements_.erase(std::unique(elements_.begin(), elements_.end()), elements_.end());
        valid_ = true;
        return true;
    }

    void FrameGraphDebugPanelIdRegistry::Clear() noexcept
    {
        elements_.clear();
        dependencyEndpoints_.clear();
        valid_ = false;
    }

    std::optional<UInt64> FrameGraphDebugPanelIdRegistry::FindPassNodeId(UInt32 passIndex) const noexcept
    {
        return FindId(MakePassNodeElement(passIndex));
    }

    std::optional<UInt64> FrameGraphDebugPanelIdRegistry::FindTexturePinId(UInt32 passIndex, UInt32 textureIndex, UInt32 version, bool output) const noexcept
    {
        const std::optional<FrameGraphDebugPanelElement> element =
            MakePinElement(FrameGraphDebugResourceKind::Texture, passIndex, textureIndex, version, output);
        return element.has_value() ? FindId(*element) : std::nullopt;
    }

    std::optional<UInt64> FrameGraphDebugPanelIdRegistry::FindBufferPinId(UInt32 passIndex, UInt32 bufferIndex, UInt32 version, bool output) const noexcept
    {
        const std::optional<FrameGraphDebugPanelElement> element = MakePinElement(FrameGraphDebugResourceKind::Buffer, passIndex, bufferIndex, version, output);
        return element.has_value() ? FindId(*element) : std::nullopt;
    }

    std::optional<UInt64> FrameGraphDebugPanelIdRegistry::FindDependencyLinkId(UInt32 dependencyIndex) const noexcept
    {
        return FindId(MakeDependencyLinkElement(dependencyIndex));
    }

    std::optional<FrameGraphDebugPanelDependencyEndpointIds> FrameGraphDebugPanelIdRegistry::FindDependencyEndpointIds(UInt32 dependencyIndex) const noexcept
    {
        if (dependencyIndex >= dependencyEndpoints_.size() || !dependencyEndpoints_[dependencyIndex].has_value())
        {
            return std::nullopt;
        }

        const DependencyEndpointElements& endpoints = *dependencyEndpoints_[dependencyIndex];
        const std::optional<UInt64> startId = FindId(endpoints.start);
        const std::optional<UInt64> endId = FindId(endpoints.end);
        if (!startId.has_value() || !endId.has_value())
        {
            return std::nullopt;
        }
        return FrameGraphDebugPanelDependencyEndpointIds{*startId, *endId};
    }

    std::optional<UInt64> FrameGraphDebugPanelIdRegistry::FindId(const FrameGraphDebugPanelElement& element) const noexcept
    {
        const auto iterator = std::lower_bound(elements_.begin(), elements_.end(), element);
        if (iterator == elements_.end() || *iterator != element)
        {
            return std::nullopt;
        }
        return static_cast<UInt64>(std::distance(elements_.begin(), iterator)) + 1;
    }

    std::optional<FrameGraphDebugPanelElement> FrameGraphDebugPanelIdRegistry::FindElement(UInt64 id) const noexcept
    {
        if (id == 0 || id > elements_.size())
        {
            return std::nullopt;
        }
        return elements_[static_cast<SizeT>(id - 1)];
    }

    SizeT FrameGraphDebugPanelIdRegistry::GetElementCount() const noexcept
    {
        return elements_.size();
    }

    bool FrameGraphDebugPanelIdRegistry::IsValid() const noexcept
    {
        return valid_;
    }

    std::optional<FrameGraphDebugPanelPins> BuildFrameGraphDebugPanelPins(const FrameGraphDebugPass& pass)
    {
        FrameGraphDebugPanelPins pins;
        for (const FrameGraphDebugAccess& access : pass.accesses)
        {
            if (!IsKnownResourceKind(access.resourceKind) || access.resourceIndex == InvalidFrameGraphResourceIndex)
            {
                return std::nullopt;
            }

            AddUniquePin(pins.inputs, {access.resourceKind, access.resourceIndex, access.inputVersion});
            if (access.outputVersion.has_value())
            {
                AddUniquePin(pins.outputs, {access.resourceKind, access.resourceIndex, *access.outputVersion});
            }
        }
        return pins;
    }

    FrameGraphDebugPanelDisplayLabel BuildFrameGraphDebugPanelDisplayLabel(
        std::string_view prefix, std::string_view value, std::string_view suffix, Float32 maximumWidth, FrameGraphDebugPanelTextMeasure measureText)
    {
        FrameGraphDebugPanelDisplayLabel label;
        label.full = BuildDisplayLabelText(prefix, value, suffix);
        label.visible = label.full;
        if (measureText == nullptr || !std::isfinite(maximumWidth) || maximumWidth <= 0.0F)
        {
            return label;
        }

        const auto Fits = [maximumWidth, measureText](std::string_view text)
        {
            const Float32 width = measureText(text);
            return std::isfinite(width) && width >= 0.0F && width <= maximumWidth;
        };
        if (Fits(label.full))
        {
            return label;
        }

        constexpr std::string_view Ellipsis = "...";
        label.elided = true;
        SizeT visibleValueLength = value.size();
        while (visibleValueLength > 0)
        {
            visibleValueLength = PreviousUtf8CodePointStart(value, visibleValueLength);
            label.visible = BuildDisplayLabelText(prefix, value.substr(0, visibleValueLength), Ellipsis);
            label.visible.append(suffix);
            if (Fits(label.visible))
            {
                return label;
            }
        }

        label.visible = BuildDisplayLabelText(prefix, Ellipsis, suffix);
        if (Fits(label.visible))
        {
            return label;
        }
        label.visible = Fits(Ellipsis) ? std::string(Ellipsis) : std::string{};
        return label;
    }

    std::optional<std::string> BuildFrameGraphDebugPanelTooltipText(const FrameGraphDebugPanelDisplayLabel& label, bool hovered)
    {
        return label.elided && hovered ? std::optional<std::string>{label.full} : std::nullopt;
    }

    std::optional<FrameGraphDebugPanelVisualRole> GetFrameGraphDebugPanelPassRole(const FrameGraphDebugPass& pass) noexcept
    {
        if (pass.culled)
        {
            return FrameGraphDebugPanelVisualRole::CulledPass;
        }

        switch (pass.type)
        {
        case FrameGraphDebugPassType::Raster:
            return FrameGraphDebugPanelVisualRole::RasterPass;
        case FrameGraphDebugPassType::Compute:
            return FrameGraphDebugPanelVisualRole::ComputePass;
        default:
            return std::nullopt;
        }
    }

    std::optional<FrameGraphDebugPanelVisualRole> GetFrameGraphDebugPanelDependencyRole(FrameGraphDebugDependencyHazard hazard) noexcept
    {
        switch (hazard)
        {
        case FrameGraphDebugDependencyHazard::Raw:
            return FrameGraphDebugPanelVisualRole::RawLink;
        case FrameGraphDebugDependencyHazard::War:
            return FrameGraphDebugPanelVisualRole::WarLink;
        case FrameGraphDebugDependencyHazard::Waw:
            return FrameGraphDebugPanelVisualRole::WawLink;
        default:
            return std::nullopt;
        }
    }

    std::optional<FrameGraphDebugPanelVisualRole> GetFrameGraphDebugPanelResourceRole(FrameGraphDebugResourceKind resourceKind) noexcept
    {
        switch (resourceKind)
        {
        case FrameGraphDebugResourceKind::Texture:
            return FrameGraphDebugPanelVisualRole::TexturePin;
        case FrameGraphDebugResourceKind::Buffer:
            return FrameGraphDebugPanelVisualRole::BufferPin;
        default:
            return std::nullopt;
        }
    }

    std::optional<std::vector<FrameGraphDebugPanelNodePosition>> BuildFrameGraphDebugPanelLayout(const FrameGraphDebugData& data)
    {
        if (data.passes.size() > std::numeric_limits<UInt32>::max())
        {
            return std::nullopt;
        }

        const UInt32 passCount = static_cast<UInt32>(data.passes.size());
        for (const FrameGraphDebugDependency& dependency : data.dependencies)
        {
            if (dependency.beforePass == InvalidFrameGraphDebugPassIndex || dependency.afterPass == InvalidFrameGraphDebugPassIndex ||
                dependency.beforePass >= passCount || dependency.afterPass >= passCount)
            {
                return std::nullopt;
            }
        }

        const auto IsExecutable = [&data](UInt32 passIndex)
        {
            const FrameGraphDebugPass& pass = data.passes[passIndex];
            return pass.compiledIndex.has_value() && !pass.culled;
        };
        const auto PassOrder = [&data](UInt32 passIndex)
        {
            const FrameGraphDebugPass& pass = data.passes[passIndex];
            return std::tuple(*pass.compiledIndex, pass.registrationIndex, passIndex);
        };

        std::vector<std::vector<UInt32>> successors(passCount);
        std::vector<SizeT> indegrees(passCount, 0);
        for (const FrameGraphDebugDependency& dependency : data.dependencies)
        {
            if (!IsExecutable(dependency.beforePass) || !IsExecutable(dependency.afterPass))
            {
                continue;
            }
            successors[dependency.beforePass].push_back(dependency.afterPass);
            ++indegrees[dependency.afterPass];
        }

        std::vector<UInt32> executablePasses;
        std::vector<UInt32> readyPasses;
        executablePasses.reserve(passCount);
        readyPasses.reserve(passCount);
        for (UInt32 passIndex = 0; passIndex < passCount; ++passIndex)
        {
            if (!IsExecutable(passIndex))
            {
                continue;
            }
            executablePasses.push_back(passIndex);
            if (indegrees[passIndex] == 0)
            {
                readyPasses.push_back(passIndex);
            }
        }

        std::vector<UInt32> columns(passCount, 0);
        SizeT orderedPassCount = 0;
        while (!readyPasses.empty())
        {
            const auto readyIterator = std::min_element(
                readyPasses.begin(), readyPasses.end(), [&PassOrder](UInt32 left, UInt32 right) { return PassOrder(left) < PassOrder(right); });
            const UInt32 passIndex = *readyIterator;
            readyPasses.erase(readyIterator);
            ++orderedPassCount;

            for (const UInt32 successor : successors[passIndex])
            {
                columns[successor] = std::max(columns[successor], static_cast<UInt32>(columns[passIndex] + 1));
                --indegrees[successor];
                if (indegrees[successor] == 0)
                {
                    readyPasses.push_back(successor);
                }
            }
        }
        if (orderedPassCount != executablePasses.size())
        {
            return std::nullopt;
        }

        std::sort(executablePasses.begin(),
                  executablePasses.end(),
                  [&columns, &PassOrder](UInt32 left, UInt32 right)
                  { return std::tuple(columns[left], PassOrder(left)) < std::tuple(columns[right], PassOrder(right)); });

        std::vector<FrameGraphDebugPanelNodePosition> positions(passCount);
        UInt32 maximumExecutableColumn = 0;
        UInt32 previousColumn = 0;
        UInt32 row = 0;
        bool hasExecutablePass = false;
        for (const UInt32 passIndex : executablePasses)
        {
            const UInt32 column = columns[passIndex];
            if (hasExecutablePass && column == previousColumn)
            {
                ++row;
            }
            else
            {
                row = 0;
            }
            positions[passIndex] = {column, row};
            maximumExecutableColumn = column;
            previousColumn = column;
            hasExecutablePass = true;
        }

        std::vector<UInt32> nonExecutablePasses;
        nonExecutablePasses.reserve(static_cast<SizeT>(passCount) - executablePasses.size());
        for (UInt32 passIndex = 0; passIndex < passCount; ++passIndex)
        {
            if (!IsExecutable(passIndex))
            {
                nonExecutablePasses.push_back(passIndex);
            }
        }
        std::sort(nonExecutablePasses.begin(),
                  nonExecutablePasses.end(),
                  [&data](UInt32 left, UInt32 right)
                  { return std::tuple(data.passes[left].registrationIndex, left) < std::tuple(data.passes[right].registrationIndex, right); });

        const UInt32 nonExecutableColumn = hasExecutablePass ? static_cast<UInt32>(maximumExecutableColumn + 1) : 0;
        for (SizeT nonExecutableRow = 0; nonExecutableRow < nonExecutablePasses.size(); ++nonExecutableRow)
        {
            if (nonExecutableRow > std::numeric_limits<UInt32>::max())
            {
                return std::nullopt;
            }
            positions[nonExecutablePasses[nonExecutableRow]] = {nonExecutableColumn, static_cast<UInt32>(nonExecutableRow)};
        }
        return positions;
    }

    FrameGraphDebugPanelColumnLayout CalculateFrameGraphDebugPanelColumnLayout(Float32 availableWidth, Float32 itemSpacing) noexcept
    {
        constexpr Float32 LeftColumnRatio = 0.55F;
        if (!std::isfinite(availableWidth) || availableWidth <= 0.0F)
        {
            return {};
        }

        const Float32 spacing = std::isfinite(itemSpacing) ? std::clamp(itemSpacing, 0.0F, availableWidth) : 0.0F;
        const Float32 usableWidth = availableWidth - spacing;
        const Float32 leftWidth = usableWidth * LeftColumnRatio;
        return {leftWidth, usableWidth - leftWidth};
    }

    bool CanCaptureFrameGraph(bool isPlaying, FrameGraphDebugCaptureStatus status, bool captureResultPending) noexcept
    {
        if (!isPlaying || captureResultPending)
        {
            return false;
        }

        switch (status)
        {
        case FrameGraphDebugCaptureStatus::Idle:
        case FrameGraphDebugCaptureStatus::Ready:
        case FrameGraphDebugCaptureStatus::Failed:
            return true;
        case FrameGraphDebugCaptureStatus::Armed:
        case FrameGraphDebugCaptureStatus::Capturing:
        default:
            return false;
        }
    }

    bool ShouldPauseAfterFrameGraphCapture(bool isPlaying, bool isPaused, ErrorCode requestResult) noexcept
    {
        return isPlaying && !isPaused && requestResult == ErrorCode::None;
    }
} // namespace ve::editor
