# FrameGraph Debugger Topology Layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render the captured FrameGraph as a dependency-layered left-to-right graph with left input pins, right output pins, and a persistent legend explaining node, link, and pin colors.

**Architecture:** Extend `FrameGraphDebugPanelModel` with ImGui-independent layout, pin, and visual-role functions, and test those functions before changing the UI. `FrameGraphDebugPanel` consumes the model results, performs one-time node placement for each immutable snapshot, renders a three-column node body, and uses one role-to-color function for both graph elements and the legend.

**Tech Stack:** C++20, Dear ImGui 1.92.8, imgui-node-editor, CMake/MSVC, project-owned `VEngineFrameGraphDebugTests`.

---

## File Structure

- Modify `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.h`: declare pure layout positions, pin records, color roles, and builder/classification functions.
- Modify `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.cpp`: implement dependency layering, stable row assignment, pin deduplication, and semantic role classification.
- Modify `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanel.h`: add legend rendering and cached layout state.
- Modify `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanel.cpp`: render the legend, three-column nodes, role colors, and dependency-layer positions.
- Modify `Tests/Unit/FrameGraphDebugTests.cpp`: add focused tests for layout, pins, roles, and malformed cycles.
- Verify `Docs/Superpowers/Specs/2026-08-04-framegraph-debugger-topology-layout-design.md`: update only if implementation discovers a necessary semantic correction.

### Task 1: Dependency-Layer Layout Model

**Files:**
- Modify: `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.h`
- Modify: `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.cpp`
- Test: `Tests/Unit/FrameGraphDebugTests.cpp`

- [ ] **Step 1: Write the failing dependency-layout tests**

Add a focused test near the existing panel model tests:

```cpp
bool TestPanelDependencyLayoutFlowsLeftToRight()
{
    ve::FrameGraphDebugData data;
    data.passes.resize(6);
    for (ve::UInt32 passIndex = 0; passIndex < 5; ++passIndex)
    {
        data.passes[passIndex].registrationIndex = passIndex;
        data.passes[passIndex].compiledIndex = passIndex;
        data.passes[passIndex].retained = true;
    }
    data.passes[5].registrationIndex = 5;
    data.passes[5].culled = true;

    data.dependencies.push_back({0, 1, ve::FrameGraphDebugResourceKind::Texture, 0, 0, ve::FrameGraphDebugDependencyHazard::Raw});
    data.dependencies.push_back({0, 2, ve::FrameGraphDebugResourceKind::Texture, 0, 0, ve::FrameGraphDebugDependencyHazard::Raw});
    data.dependencies.push_back({1, 3, ve::FrameGraphDebugResourceKind::Texture, 0, 0, ve::FrameGraphDebugDependencyHazard::Raw});
    data.dependencies.push_back({2, 3, ve::FrameGraphDebugResourceKind::Texture, 0, 0, ve::FrameGraphDebugDependencyHazard::Raw});

    const auto layout = ve::editor::BuildFrameGraphDebugPanelLayout(data);
    bool passed = Expect(layout.has_value() && layout->size() == data.passes.size(), "valid panel graph should produce one position per pass");
    if (!layout.has_value() || layout->size() != data.passes.size())
    {
        return false;
    }

    passed &= Expect((*layout)[0] == ve::editor::FrameGraphDebugPanelNodePosition{0, 0}, "root pass should start in the leftmost layer");
    passed &= Expect((*layout)[4] == ve::editor::FrameGraphDebugPanelNodePosition{0, 1}, "independent passes should stack stably in layer zero");
    passed &= Expect((*layout)[1] == ve::editor::FrameGraphDebugPanelNodePosition{1, 0} &&
                         (*layout)[2] == ve::editor::FrameGraphDebugPanelNodePosition{1, 1},
                     "fan-out passes should share the next layer in compiled order");
    passed &= Expect((*layout)[3] == ve::editor::FrameGraphDebugPanelNodePosition{2, 0}, "fan-in pass should follow its deepest predecessor");
    passed &= Expect((*layout)[5] == ve::editor::FrameGraphDebugPanelNodePosition{3, 0}, "culled pass should follow executable layers");

    ve::FrameGraphDebugData cyclic = data;
    cyclic.dependencies.clear();
    cyclic.dependencies.push_back({0, 1, ve::FrameGraphDebugResourceKind::Texture, 0, 0, ve::FrameGraphDebugDependencyHazard::Raw});
    cyclic.dependencies.push_back({1, 0, ve::FrameGraphDebugResourceKind::Texture, 0, 0, ve::FrameGraphDebugDependencyHazard::Raw});
    passed &= Expect(!ve::editor::BuildFrameGraphDebugPanelLayout(cyclic).has_value(), "cyclic executable dependencies must fail closed");
    return passed;
}
```

Add `TestPanelDependencyLayoutFlowsLeftToRight()` to the `main()` conjunction.

- [ ] **Step 2: Build the test target and verify RED**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineFrameGraphDebugTests
```

Expected: compilation fails because `FrameGraphDebugPanelNodePosition` and `BuildFrameGraphDebugPanelLayout` do not exist.

- [ ] **Step 3: Declare the layout model**

Add to `FrameGraphDebugPanelModel.h`:

```cpp
struct FrameGraphDebugPanelNodePosition
{
    UInt32 column = 0;
    UInt32 row = 0;

    auto operator<=>(const FrameGraphDebugPanelNodePosition&) const = default;
};

[[nodiscard]] std::optional<std::vector<FrameGraphDebugPanelNodePosition>> BuildFrameGraphDebugPanelLayout(const FrameGraphDebugData& data);
```

- [ ] **Step 4: Implement the minimal layered layout**

In `FrameGraphDebugPanelModel.cpp`, implement `BuildFrameGraphDebugPanelLayout` with these exact rules:

1. Reject a pass count above `UInt32` range or any dependency with an out-of-range pass index.
2. Treat passes with `compiledIndex` and `!culled` as executable.
3. Build executable adjacency and indegree arrays from dependencies whose endpoints are both executable.
4. Run Kahn topological ordering. Select ready passes by `(compiledIndex, registrationIndex, passIndex)` so results are deterministic.
5. Set each successor column to `max(currentColumn, predecessorColumn + 1)`.
6. Return `std::nullopt` if the executable subgraph is cyclic.
7. Group executable passes by column and assign rows in `(compiledIndex, registrationIndex, passIndex)` order.
8. Put every non-executable pass into `maximumExecutableColumn + 1`, assigning rows by `(registrationIndex, passIndex)`.
9. Return a vector indexed by pass index.

Use checked `SizeT`/`UInt32` conversions and the existing invalid-index conventions; do not introduce an ImGui dependency into the model.

Add `<tuple>` and implement:

```cpp
std::optional<std::vector<FrameGraphDebugPanelNodePosition>> BuildFrameGraphDebugPanelLayout(const FrameGraphDebugData& data)
{
    if (data.passes.size() > std::numeric_limits<UInt32>::max())
    {
        return std::nullopt;
    }

    const SizeT passCount = data.passes.size();
    std::vector<bool> executable(passCount, false);
    SizeT executableCount = 0;
    for (SizeT passIndex = 0; passIndex < passCount; ++passIndex)
    {
        executable[passIndex] = data.passes[passIndex].compiledIndex.has_value() && !data.passes[passIndex].culled;
        executableCount += executable[passIndex] ? 1 : 0;
    }

    std::vector<std::vector<UInt32>> successors(passCount);
    std::vector<UInt32> indegrees(passCount, 0);
    for (const FrameGraphDebugDependency& dependency : data.dependencies)
    {
        if (dependency.beforePass >= passCount || dependency.afterPass >= passCount)
        {
            return std::nullopt;
        }
        if (executable[dependency.beforePass] && executable[dependency.afterPass])
        {
            successors[dependency.beforePass].push_back(dependency.afterPass);
            ++indegrees[dependency.afterPass];
        }
    }

    const auto executionLess = [&data](UInt32 left, UInt32 right)
    {
        const FrameGraphDebugPass& leftPass = data.passes[left];
        const FrameGraphDebugPass& rightPass = data.passes[right];
        return std::tuple(*leftPass.compiledIndex, leftPass.registrationIndex, left) <
               std::tuple(*rightPass.compiledIndex, rightPass.registrationIndex, right);
    };

    std::vector<UInt32> ready;
    std::vector<UInt32> columns(passCount, 0);
    for (SizeT passIndex = 0; passIndex < passCount; ++passIndex)
    {
        if (executable[passIndex] && indegrees[passIndex] == 0)
        {
            ready.push_back(static_cast<UInt32>(passIndex));
        }
    }

    SizeT processedCount = 0;
    while (!ready.empty())
    {
        std::sort(ready.begin(), ready.end(), executionLess);
        const UInt32 passIndex = ready.front();
        ready.erase(ready.begin());
        ++processedCount;
        for (UInt32 successor : successors[passIndex])
        {
            columns[successor] = (std::max)(columns[successor], columns[passIndex] + 1);
            if (--indegrees[successor] == 0)
            {
                ready.push_back(successor);
            }
        }
    }
    if (processedCount != executableCount)
    {
        return std::nullopt;
    }

    UInt32 maximumExecutableColumn = 0;
    std::vector<FrameGraphDebugPanelNodePosition> positions(passCount);
    for (SizeT passIndex = 0; passIndex < passCount; ++passIndex)
    {
        if (executable[passIndex])
        {
            maximumExecutableColumn = (std::max)(maximumExecutableColumn, columns[passIndex]);
        }
    }
    std::vector<std::vector<UInt32>> passesByColumn(static_cast<SizeT>(maximumExecutableColumn) + 1);
    for (SizeT passIndex = 0; passIndex < passCount; ++passIndex)
    {
        if (executable[passIndex])
        {
            passesByColumn[columns[passIndex]].push_back(static_cast<UInt32>(passIndex));
        }
    }
    for (UInt32 column = 0; column < passesByColumn.size(); ++column)
    {
        std::vector<UInt32>& passes = passesByColumn[column];
        std::sort(passes.begin(), passes.end(), executionLess);
        for (SizeT row = 0; row < passes.size(); ++row)
        {
            positions[passes[row]] = {column, static_cast<UInt32>(row)};
        }
    }

    std::vector<UInt32> nonExecutable;
    for (SizeT passIndex = 0; passIndex < passCount; ++passIndex)
    {
        if (!executable[passIndex])
        {
            nonExecutable.push_back(static_cast<UInt32>(passIndex));
        }
    }
    std::sort(nonExecutable.begin(),
              nonExecutable.end(),
              [&data](UInt32 left, UInt32 right)
              { return std::tuple(data.passes[left].registrationIndex, left) < std::tuple(data.passes[right].registrationIndex, right); });
    const UInt32 nonExecutableColumn = executableCount == 0 ? 0 : maximumExecutableColumn + 1;
    for (SizeT row = 0; row < nonExecutable.size(); ++row)
    {
        positions[nonExecutable[row]] = {nonExecutableColumn, static_cast<UInt32>(row)};
    }
    return positions;
}
```

- [ ] **Step 5: Build and run the focused test**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineFrameGraphDebugTests
Build/windows-msvc-tests/Debug/VEngineFrameGraphDebugTests.exe
```

Expected: build succeeds and prints `VEngineFrameGraphDebugTests passed`.

- [ ] **Step 6: Commit the layout model**

```text
git add Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.h Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.cpp Tests/Unit/FrameGraphDebugTests.cpp
git commit -m "editor: add frame graph dependency layout model"
```

### Task 2: Pin Classification And Shared Visual Roles

**Files:**
- Modify: `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.h`
- Modify: `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.cpp`
- Test: `Tests/Unit/FrameGraphDebugTests.cpp`

- [ ] **Step 1: Write failing tests for left/right pins and color roles**

Add:

```cpp
bool TestPanelPinColumnsAndVisualRoles()
{
    ve::FrameGraphDebugPass pass;
    pass.type = ve::FrameGraphDebugPassType::Compute;
    pass.accesses.push_back({ve::FrameGraphDebugResourceKind::Texture, 3, 1, std::nullopt, 0, false});
    pass.accesses.push_back({ve::FrameGraphDebugResourceKind::Buffer, 7, 2, 3, 0, true});
    pass.accesses.push_back({ve::FrameGraphDebugResourceKind::Buffer, 7, 2, 3, 0, true});

    const auto pins = ve::editor::BuildFrameGraphDebugPanelPins(pass);
    bool passed = Expect(pins.has_value(), "valid accesses should build panel pins");
    if (!pins.has_value())
    {
        return false;
    }
    passed &= Expect(pins->inputs.size() == 2 && pins->outputs.size() == 1, "input and output pins should be deduplicated into opposite columns");
    passed &= Expect(pins->inputs[0] == ve::editor::FrameGraphDebugPanelPin{ve::FrameGraphDebugResourceKind::Texture, 3, 1},
                     "read-only texture version should be a left input pin");
    passed &= Expect(pins->outputs[0] == ve::editor::FrameGraphDebugPanelPin{ve::FrameGraphDebugResourceKind::Buffer, 7, 3},
                     "produced buffer version should be a right output pin");

    passed &= Expect(ve::editor::GetFrameGraphDebugPanelPassRole(pass) == ve::editor::FrameGraphDebugPanelVisualRole::ComputePass,
                     "compute pass should use the compute role");
    pass.culled = true;
    passed &= Expect(ve::editor::GetFrameGraphDebugPanelPassRole(pass) == ve::editor::FrameGraphDebugPanelVisualRole::CulledPass,
                     "culled role should override pass type");
    passed &= Expect(ve::editor::GetFrameGraphDebugPanelDependencyRole(ve::FrameGraphDebugDependencyHazard::Raw) ==
                         ve::editor::FrameGraphDebugPanelVisualRole::RawLink &&
                         ve::editor::GetFrameGraphDebugPanelDependencyRole(ve::FrameGraphDebugDependencyHazard::War) ==
                             ve::editor::FrameGraphDebugPanelVisualRole::WarLink &&
                         ve::editor::GetFrameGraphDebugPanelDependencyRole(ve::FrameGraphDebugDependencyHazard::Waw) ==
                             ve::editor::FrameGraphDebugPanelVisualRole::WawLink,
                     "RAW/WAR/WAW hazards should keep distinct legend roles");
    passed &= Expect(ve::editor::GetFrameGraphDebugPanelResourceRole(ve::FrameGraphDebugResourceKind::Texture) ==
                         ve::editor::FrameGraphDebugPanelVisualRole::TexturePin &&
                         ve::editor::GetFrameGraphDebugPanelResourceRole(ve::FrameGraphDebugResourceKind::Buffer) ==
                             ve::editor::FrameGraphDebugPanelVisualRole::BufferPin,
                     "texture and buffer pins should keep distinct legend roles");
    return passed;
}
```

Add the test to `main()`.

- [ ] **Step 2: Build and verify RED**

Run the focused build command from Task 1.

Expected: compilation fails because the pin records, builders, and role functions are missing.

- [ ] **Step 3: Declare pin and role model types**

Add to `FrameGraphDebugPanelModel.h`:

```cpp
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

[[nodiscard]] std::optional<FrameGraphDebugPanelPins> BuildFrameGraphDebugPanelPins(const FrameGraphDebugPass& pass);
[[nodiscard]] std::optional<FrameGraphDebugPanelVisualRole> GetFrameGraphDebugPanelPassRole(const FrameGraphDebugPass& pass) noexcept;
[[nodiscard]] std::optional<FrameGraphDebugPanelVisualRole> GetFrameGraphDebugPanelDependencyRole(FrameGraphDebugDependencyHazard hazard) noexcept;
[[nodiscard]] std::optional<FrameGraphDebugPanelVisualRole> GetFrameGraphDebugPanelResourceRole(FrameGraphDebugResourceKind kind) noexcept;
```

Update the test assertions to compare optionals with the expected role.

- [ ] **Step 4: Implement pin collection and semantic roles**

Implement these rules in `FrameGraphDebugPanelModel.cpp`:

- Every valid access contributes its input version to `inputs`.
- An access with `outputVersion` contributes that produced version to `outputs`.
- Deduplicate each side by `(resourceKind, resourceIndex, version)` while preserving first appearance.
- Return `std::nullopt` for unknown resource kinds or invalid resource indices.
- Culled pass maps to `CulledPass`; otherwise Raster/Compute map to their roles; unknown enum values return `std::nullopt`.
- RAW/WAR/WAW and Texture/Buffer map one-to-one; unknown values return `std::nullopt`.

Implement:

```cpp
std::optional<FrameGraphDebugPanelPins> BuildFrameGraphDebugPanelPins(const FrameGraphDebugPass& pass)
{
    FrameGraphDebugPanelPins pins;
    const auto addUnique = [](std::vector<FrameGraphDebugPanelPin>& target, const FrameGraphDebugPanelPin& pin)
    {
        if (std::find(target.begin(), target.end(), pin) == target.end())
        {
            target.push_back(pin);
        }
    };
    for (const FrameGraphDebugAccess& access : pass.accesses)
    {
        if ((access.resourceKind != FrameGraphDebugResourceKind::Texture && access.resourceKind != FrameGraphDebugResourceKind::Buffer) ||
            access.resourceIndex == InvalidFrameGraphResourceIndex)
        {
            return std::nullopt;
        }
        addUnique(pins.inputs, {access.resourceKind, access.resourceIndex, access.inputVersion});
        if (access.outputVersion.has_value())
        {
            addUnique(pins.outputs, {access.resourceKind, access.resourceIndex, *access.outputVersion});
        }
    }
    return pins;
}

std::optional<FrameGraphDebugPanelVisualRole> GetFrameGraphDebugPanelPassRole(const FrameGraphDebugPass& pass) noexcept
{
    if (pass.culled)
    {
        return FrameGraphDebugPanelVisualRole::CulledPass;
    }
    switch (pass.type)
    {
    case FrameGraphDebugPassType::Raster: return FrameGraphDebugPanelVisualRole::RasterPass;
    case FrameGraphDebugPassType::Compute: return FrameGraphDebugPanelVisualRole::ComputePass;
    }
    return std::nullopt;
}

std::optional<FrameGraphDebugPanelVisualRole> GetFrameGraphDebugPanelDependencyRole(FrameGraphDebugDependencyHazard hazard) noexcept
{
    switch (hazard)
    {
    case FrameGraphDebugDependencyHazard::Raw: return FrameGraphDebugPanelVisualRole::RawLink;
    case FrameGraphDebugDependencyHazard::War: return FrameGraphDebugPanelVisualRole::WarLink;
    case FrameGraphDebugDependencyHazard::Waw: return FrameGraphDebugPanelVisualRole::WawLink;
    }
    return std::nullopt;
}

std::optional<FrameGraphDebugPanelVisualRole> GetFrameGraphDebugPanelResourceRole(FrameGraphDebugResourceKind kind) noexcept
{
    switch (kind)
    {
    case FrameGraphDebugResourceKind::Texture: return FrameGraphDebugPanelVisualRole::TexturePin;
    case FrameGraphDebugResourceKind::Buffer: return FrameGraphDebugPanelVisualRole::BufferPin;
    }
    return std::nullopt;
}
```

- [ ] **Step 5: Run the focused executable and commit**

Run the Task 1 build and executable commands. Expected: `VEngineFrameGraphDebugTests passed`.

```text
git add Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.h Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.cpp Tests/Unit/FrameGraphDebugTests.cpp
git commit -m "editor: model frame graph pins and visual roles"
```

### Task 3: Render Left Inputs, Right Outputs, Layered Nodes, And Legend

**Files:**
- Modify: `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanel.h`
- Modify: `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanel.cpp`

- [ ] **Step 1: Add cached layout and legend declarations**

In `FrameGraphDebugPanel.h`, add:

```cpp
void RenderTopologyLegend();
void RenderLegendEntry(const char* label, FrameGraphDebugPanelVisualRole role, bool line, bool& first);
void RenderNodePin(UInt32 passIndex, const FrameGraphDebugPanelPin& pin, bool output);

std::vector<FrameGraphDebugPanelNodePosition> nodePositions_;
bool nodeLayoutValid_ = false;
```

- [ ] **Step 2: Use one role-to-color mapping**

Replace the separate `PassColor`, `PinColor`, and `HazardColor` implementations with:

```cpp
[[nodiscard]] ImVec4 VisualRoleColor(FrameGraphDebugPanelVisualRole role) noexcept
{
    switch (role)
    {
    case FrameGraphDebugPanelVisualRole::RasterPass: return ImVec4(0.12F, 0.30F, 0.48F, 0.95F);
    case FrameGraphDebugPanelVisualRole::ComputePass: return ImVec4(0.35F, 0.18F, 0.48F, 0.95F);
    case FrameGraphDebugPanelVisualRole::CulledPass: return ImVec4(0.20F, 0.20F, 0.22F, 0.85F);
    case FrameGraphDebugPanelVisualRole::RawLink: return ImVec4(0.25F, 0.82F, 0.38F, 1.0F);
    case FrameGraphDebugPanelVisualRole::WarLink: return ImVec4(0.95F, 0.72F, 0.20F, 1.0F);
    case FrameGraphDebugPanelVisualRole::WawLink: return ImVec4(0.95F, 0.28F, 0.28F, 1.0F);
    case FrameGraphDebugPanelVisualRole::TexturePin: return ImVec4(0.20F, 0.75F, 0.95F, 1.0F);
    case FrameGraphDebugPanelVisualRole::BufferPin: return ImVec4(0.95F, 0.67F, 0.18F, 1.0F);
    }
    return ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
}
```

Add this checked bridge for existing dependency table/detail rendering:

```cpp
[[nodiscard]] ImVec4 DependencyColor(FrameGraphDebugDependencyHazard hazard) noexcept
{
    const std::optional<FrameGraphDebugPanelVisualRole> role = GetFrameGraphDebugPanelDependencyRole(hazard);
    return role.has_value() ? VisualRoleColor(*role) : ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
}
```

Replace every `HazardColor(dependency.hazard)` call in topology links, the dependency table, and dependency details with `DependencyColor(dependency.hazard)`. Delete `PassColor`, `PinColor`, `HazardColor`, and the now-unused `BuildPinLabel`; node and pin rendering will use the model roles in Step 6. All graph rendering and legend entries must obtain their role from the model and their RGBA color from `VisualRoleColor`.

- [ ] **Step 3: Render the persistent legend**

Call `RenderTopologyLegend()` after `RenderToolbar()` and before the separator/node canvas. Render compact wrapping entries in this order:

```text
Nodes: Raster  Compute  Culled    Links: RAW (write -> read)  WAR (read -> write)  WAW (write -> write)    Pins: Texture  Buffer
```

Use filled square swatches for node/pin entries and two-pixel horizontal line swatches for dependency entries. Before each item, compare its calculated width with `ImGui::GetContentRegionAvail().x`; begin a new line when it would overflow. Keep the legend outside `ed::Begin` so it never pans or zooms.

Implement the methods with the shared color function:

```cpp
void FrameGraphDebugPanel::RenderLegendEntry(const char* label, FrameGraphDebugPanelVisualRole role, bool line, bool& first)
{
    const float swatchWidth = line ? 22.0F : 13.0F;
    const float itemWidth = swatchWidth + 5.0F + ImGui::CalcTextSize(label).x;
    if (!first)
    {
        const float nextX = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
        const float contentRight = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        if (nextX + itemWidth <= contentRight)
        {
            ImGui::SameLine();
        }
    }

    ImGui::BeginGroup();
    const ImVec2 swatchPosition = ImGui::GetCursorScreenPos();
    const float lineHeight = ImGui::GetTextLineHeight();
    ImGui::Dummy(ImVec2(swatchWidth, lineHeight));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(VisualRoleColor(role));
    if (line)
    {
        const float centerY = swatchPosition.y + lineHeight * 0.5F;
        drawList->AddLine(ImVec2(swatchPosition.x, centerY), ImVec2(swatchPosition.x + swatchWidth, centerY), color, 2.0F);
    }
    else
    {
        drawList->AddRectFilled(swatchPosition, ImVec2(swatchPosition.x + swatchWidth, swatchPosition.y + lineHeight), color, 2.0F);
    }
    ImGui::SameLine(0.0F, 5.0F);
    ImGui::TextUnformatted(label);
    ImGui::EndGroup();
    first = false;
}

void FrameGraphDebugPanel::RenderTopologyLegend()
{
    bool first = true;
    RenderLegendEntry("Raster", FrameGraphDebugPanelVisualRole::RasterPass, false, first);
    RenderLegendEntry("Compute", FrameGraphDebugPanelVisualRole::ComputePass, false, first);
    RenderLegendEntry("Culled", FrameGraphDebugPanelVisualRole::CulledPass, false, first);
    RenderLegendEntry("RAW (write -> read)", FrameGraphDebugPanelVisualRole::RawLink, true, first);
    RenderLegendEntry("WAR (read -> write)", FrameGraphDebugPanelVisualRole::WarLink, true, first);
    RenderLegendEntry("WAW (write -> write)", FrameGraphDebugPanelVisualRole::WawLink, true, first);
    RenderLegendEntry("Texture", FrameGraphDebugPanelVisualRole::TexturePin, false, first);
    RenderLegendEntry("Buffer", FrameGraphDebugPanelVisualRole::BufferPin, false, first);
}
```

- [ ] **Step 4: Build and cache layout on snapshot reset**

In `ResetSnapshotUiState()`:

```cpp
nodePositions_.clear();
nodeLayoutValid_ = false;
if (debugData_ != nullptr)
{
    (void)idRegistry_.Reset(*debugData_);
    const auto layout = BuildFrameGraphDebugPanelLayout(*debugData_);
    if (layout.has_value())
    {
        nodePositions_ = *layout;
        nodeLayoutValid_ = true;
    }
    positionedNodes_.assign(debugData_->passes.size(), false);
}
```

Clear `nodePositions_` and `nodeLayoutValid_` in `Shutdown()`. Treat `!nodeLayoutValid_` like an invalid ID registry in `RenderContent()` and show a precise invalid-layout message in `RenderTopology()`.

- [ ] **Step 5: Replace the fixed five-column grid placement**

Replace `NodesPerRow` placement with:

```cpp
constexpr float NodeColumnSpacing = 360.0F;
constexpr float NodeRowSpacing = 230.0F;
const FrameGraphDebugPanelNodePosition& position = nodePositions_[passIndex];
ed::SetNodePosition(ToNodeId(*nodeId),
                    ImVec2(static_cast<float>(position.column) * NodeColumnSpacing,
                           static_cast<float>(position.row) * NodeRowSpacing));
```

Retain the existing `positionedNodes_` guard so manual node dragging is not overwritten.

- [ ] **Step 6: Render pass nodes as input/center/output columns**

For each pass, call `BuildFrameGraphDebugPanelPins(pass)`. Fail closed for an invalid result. Render one ImGui table inside the node with three columns and `max(inputs.size(), outputs.size(), 1)` rows:

- Column zero calls `RenderNodePin(..., false)` for the corresponding input.
- Column one renders pass name on the first row and `%s%s` type/culled state below it.
- Column two calls `RenderNodePin(..., true)` for the corresponding output.

`RenderNodePin` resolves the deterministic pin ID from `idRegistry_`, begins the correct `ed::PinKind`, and renders:

```text
<- ResourceName vN        ResourceName vN ->
```

Use `ed::PinPivotAlignment(ImVec2(0.0F, 0.5F))` for inputs and `ed::PinPivotAlignment(ImVec2(1.0F, 0.5F))` for outputs. Keep texture/buffer colors derived from `GetFrameGraphDebugPanelResourceRole`.

Implement the pin renderer:

```cpp
void FrameGraphDebugPanel::RenderNodePin(UInt32 passIndex, const FrameGraphDebugPanelPin& pin, bool output)
{
    const std::optional<FrameGraphDebugPanelVisualRole> role = GetFrameGraphDebugPanelResourceRole(pin.resourceKind);
    const std::optional<UInt64> pinId = pin.resourceKind == FrameGraphDebugResourceKind::Texture
                                            ? idRegistry_.FindTexturePinId(passIndex, pin.resourceIndex, pin.version, output)
                                            : idRegistry_.FindBufferPinId(passIndex, pin.resourceIndex, pin.version, output);
    if (!role.has_value() || !pinId.has_value())
    {
        return;
    }

    const ImVec4 color = VisualRoleColor(*role);
    ed::PushStyleColor(ed::StyleColor_PinRect, color);
    ed::PushStyleColor(ed::StyleColor_PinRectBorder, color);
    ed::BeginPin(ToPinId(*pinId), output ? ed::PinKind::Output : ed::PinKind::Input);
    ed::PinPivotAlignment(output ? ImVec2(1.0F, 0.5F) : ImVec2(0.0F, 0.5F));
    const std::string label = GetResourceName(*debugData_, pin.resourceKind, pin.resourceIndex) + " v" + std::to_string(pin.version);
    ImGui::TextColored(color, output ? "%s ->" : "<- %s", label.c_str());
    ed::EndPin();
    ed::PopStyleColor(2);
}
```

Replace the existing sequential pin loop inside each node with:

```cpp
const std::optional<FrameGraphDebugPanelPins> pins = BuildFrameGraphDebugPanelPins(pass);
const std::optional<FrameGraphDebugPanelVisualRole> passRole = GetFrameGraphDebugPanelPassRole(pass);
if (!pins.has_value() || !passRole.has_value())
{
    continue;
}

ed::PushStyleColor(ed::StyleColor_NodeBg, VisualRoleColor(*passRole));
ed::BeginNode(ToNodeId(*nodeId));
ImGui::PushID(static_cast<int>(passIndex));
if (ImGui::BeginTable("PassNodeLayout", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
{
    ImGui::TableSetupColumn("Inputs", ImGuiTableColumnFlags_WidthFixed, 190.0F);
    ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 160.0F);
    ImGui::TableSetupColumn("Outputs", ImGuiTableColumnFlags_WidthFixed, 190.0F);
    const SizeT rowCount = (std::max)({pins->inputs.size(), pins->outputs.size(), SizeT{1}});
    for (SizeT row = 0; row < rowCount; ++row)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (row < pins->inputs.size())
        {
            RenderNodePin(passIndex, pins->inputs[row], false);
        }
        ImGui::TableSetColumnIndex(1);
        if (row == 0)
        {
            ImGui::TextUnformatted(pass.name.c_str());
            ImGui::TextDisabled("%s%s", ToString(pass.type), pass.culled ? " / Culled" : "");
        }
        ImGui::TableSetColumnIndex(2);
        if (row < pins->outputs.size())
        {
            RenderNodePin(passIndex, pins->outputs[row], true);
        }
    }
    ImGui::EndTable();
}
ImGui::PopID();
ed::EndNode();
ed::PopStyleColor();
```

- [ ] **Step 7: Build the Editor and run focused tests**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineFrameGraphDebugTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEngineFrameGraphDebugTests --output-on-failure
```

Expected: Editor build succeeds and CTest reports `1/1` passed.

- [ ] **Step 8: Commit the UI integration**

```text
git add Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanel.h Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanel.cpp
git commit -m "editor: render frame graph left to right"
```

### Task 4: Runtime Acceptance And Final Verification

**Files:**
- Verify: `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanel.cpp`
- Verify: `Docs/Superpowers/Specs/2026-08-04-framegraph-debugger-topology-layout-design.md`

- [ ] **Step 1: Format changed project sources and check whitespace**

Run project `.clang-format` on the changed `.h`/`.cpp` files, excluding vendored sources, then run:

```text
git diff --check
```

Expected: no whitespace errors.

- [ ] **Step 2: Run full Windows test verification**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure
```

Expected: all configured tests pass.

- [ ] **Step 3: Run D3D12 Editor capture smoke test**

Launch:

```text
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --dx12 --project "D:\github-desktop\VEngine\DemoProject"
```

Verify manually:

1. Open `Frame Graph`.
2. Enter Play Mode.
3. Click `Capture Next Frame` and confirm Play pauses.
4. Confirm root passes are leftmost and every dependent retained pass is in a strictly later column than each visible predecessor.
5. Confirm input pins are on node left edges and output pins are on right edges.
6. Confirm the legend remains visible while the node canvas pans and zooms.
7. Confirm blue/purple/gray nodes, green/yellow/red links, and cyan/orange pins match the legend.
8. Close the Editor and confirm clean shutdown without FrameGraph retirement assertions.

- [ ] **Step 4: Review the final diff against the approved spec**

Run:

```text
git diff dc539186dd195e4f1ceb047f3fe69ef00f86e336..HEAD -- Editor/Panels/FrameGraphDebugPanel Tests/Unit/FrameGraphDebugTests.cpp Docs/Superpowers/Specs/2026-08-04-framegraph-debugger-topology-layout-design.md
```

Confirm that capture/pause semantics, preview ownership, GPU timing exclusion, and buffer metadata-only behavior are unchanged.

- [ ] **Step 5: Commit any formatting or verification-only corrections**

If formatting changed tracked files:

```text
git add Editor/Panels/FrameGraphDebugPanel Tests/Unit/FrameGraphDebugTests.cpp Docs/Superpowers/Specs/2026-08-04-framegraph-debugger-topology-layout-design.md
git commit -m "style: finalize frame graph topology layout"
```

If no tracked file changed, do not create an empty commit.
