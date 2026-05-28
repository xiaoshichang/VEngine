include_guard(GLOBAL)

function(ve_add_example_projects)
    if(NOT VE_BUILD_EXAMPLE_PROJECTS)
        return()
    endif()

    if(NOT WIN32)
        return()
    endif()

    ve_add_asset_tool()

    set(veAssetPipelineSampleRoot "${PROJECT_SOURCE_DIR}/Examples/AssetPipelineSample")
    set(veAssetPipelineSampleWorkspace
        "${veAssetPipelineSampleRoot}/Generated/Editor/Workspace")
    set(veAssetPipelineSampleScriptsProject
        "${veAssetPipelineSampleWorkspace}/VE.Scripting.csproj")
    set(veAssetPipelineSampleScriptsSolution
        "${veAssetPipelineSampleWorkspace}/VE.Scripting.sln")
    set(veAssetPipelineSampleScriptsOutput
        "${veAssetPipelineSampleRoot}/Generated/Scripts/Windows/$<CONFIG>/VE.Scripting")
    set(veAssetPipelineSampleScriptsIntermediate
        "${veAssetPipelineSampleRoot}/Generated/Intermediates/Scripts/Windows/$<CONFIG>/VE.Scripting/")

    file(MAKE_DIRECTORY "${veAssetPipelineSampleWorkspace}")
    file(WRITE "${veAssetPipelineSampleScriptsProject}"
"<Project Sdk=\"Microsoft.NET.Sdk\">
  <PropertyGroup>
    <TargetFramework Condition=\"'$(TargetFramework)' == ''\">${VE_DOTNET_TARGET_FRAMEWORK}</TargetFramework>
    <AssemblyName>VE.Scripting</AssemblyName>
    <RootNamespace>VE.Scripting</RootNamespace>
    <ImplicitUsings>disable</ImplicitUsings>
    <LangVersion>latest</LangVersion>
    <Nullable>enable</Nullable>
    <OutputType>Library</OutputType>
    <EnableDynamicLoading>true</EnableDynamicLoading>
    <GenerateRuntimeConfigurationFiles>true</GenerateRuntimeConfigurationFiles>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <DefaultItemExcludes>$(DefaultItemExcludes);bin/**;obj/**</DefaultItemExcludes>
  </PropertyGroup>

  <ItemGroup>
    <Compile Include=\"../../../Scripts/VE.Scripting/**/*.cs\" Link=\"Scripts/%(RecursiveDir)%(Filename)%(Extension)\" />
    <ProjectReference Include=\"${PROJECT_SOURCE_DIR}/Managed/VEngine.ScriptAPI/VEngine.ScriptAPI.csproj\" />
  </ItemGroup>
</Project>
")
    file(WRITE "${veAssetPipelineSampleScriptsSolution}"
"Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
VisualStudioVersion = 17.0.31903.59
MinimumVisualStudioVersion = 10.0.40219.1
Project(\"{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}\") = \"VE.Scripting\", \"VE.Scripting.csproj\", \"{4D779A8E-A1F8-4D8E-A1A3-F1F4A10A8E01}\"
EndProject
Project(\"{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}\") = \"VEngine.ScriptAPI\", \"${PROJECT_SOURCE_DIR}/Managed/VEngine.ScriptAPI/VEngine.ScriptAPI.csproj\", \"{16AE4D74-21E2-41C4-8B28-E0824C1C7F41}\"
EndProject
Global
    GlobalSection(SolutionConfigurationPlatforms) = preSolution
        Debug|Any CPU = Debug|Any CPU
        Release|Any CPU = Release|Any CPU
    EndGlobalSection
    GlobalSection(ProjectConfigurationPlatforms) = postSolution
        {4D779A8E-A1F8-4D8E-A1A3-F1F4A10A8E01}.Debug|Any CPU.ActiveCfg = Debug|Any CPU
        {4D779A8E-A1F8-4D8E-A1A3-F1F4A10A8E01}.Debug|Any CPU.Build.0 = Debug|Any CPU
        {4D779A8E-A1F8-4D8E-A1A3-F1F4A10A8E01}.Release|Any CPU.ActiveCfg = Release|Any CPU
        {4D779A8E-A1F8-4D8E-A1A3-F1F4A10A8E01}.Release|Any CPU.Build.0 = Release|Any CPU
        {16AE4D74-21E2-41C4-8B28-E0824C1C7F41}.Debug|Any CPU.ActiveCfg = Debug|Any CPU
        {16AE4D74-21E2-41C4-8B28-E0824C1C7F41}.Debug|Any CPU.Build.0 = Debug|Any CPU
        {16AE4D74-21E2-41C4-8B28-E0824C1C7F41}.Release|Any CPU.ActiveCfg = Release|Any CPU
        {16AE4D74-21E2-41C4-8B28-E0824C1C7F41}.Release|Any CPU.Build.0 = Release|Any CPU
    EndGlobalSection
EndGlobal
")

    add_custom_target(VEngineExampleAssetPipelineSample ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${veAssetPipelineSampleRoot}/Generated/Assets/ImportCache"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${veAssetPipelineSampleRoot}/Generated/Build/Windows"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${veAssetPipelineSampleRoot}/Generated/Editor/Workspace"
        COMMAND $<TARGET_FILE:VEngineAssetTool>
            import
            --project "${veAssetPipelineSampleRoot}"
            --source "Assets/Samples/Models/Cube.obj"
        COMMAND $<TARGET_FILE:VEngineAssetTool>
            validate
            --project "${veAssetPipelineSampleRoot}"
        DEPENDS VEngineAssetTool
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Building VEngine example project: AssetPipelineSample"
        VERBATIM
    )

    set_target_properties(VEngineExampleAssetPipelineSample
        PROPERTIES
            FOLDER "Examples"
    )

    if(TARGET VEngineScriptAPI)
        add_custom_target(VEngineExampleAssetPipelineSampleScripts ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${veAssetPipelineSampleScriptsOutput}"
            COMMAND "${VE_DOTNET_EXECUTABLE}"
                build "${veAssetPipelineSampleScriptsProject}"
                --configuration $<CONFIG>
                --framework "${VE_DOTNET_TARGET_FRAMEWORK}"
                --output "${veAssetPipelineSampleScriptsOutput}"
                -p:BaseIntermediateOutputPath="${veAssetPipelineSampleScriptsIntermediate}"
            DEPENDS
                VEngineScriptAPI
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMENT "Building VEngine sample scripts: AssetPipelineSample"
            VERBATIM
        )

        set_target_properties(VEngineExampleAssetPipelineSampleScripts
            PROPERTIES
                FOLDER "Examples"
        )
    endif()

    if(TARGET VEnginePlayer)
        add_dependencies(VEnginePlayer VEngineExampleAssetPipelineSample)

        ve_add_package_tool()

        add_custom_target(VEngineExampleAssetPipelineSamplePackage ALL
            COMMAND $<TARGET_FILE:VEnginePackageTool>
                package
                --project "${veAssetPipelineSampleRoot}"
                --platform Windows
                --config $<CONFIG>
                --output "${veAssetPipelineSampleRoot}/Generated/Build/Windows/$<CONFIG>"
                --player $<TARGET_FILE:VEnginePlayer>
            DEPENDS
                VEnginePackageTool
                VEnginePlayer
                VEngineExampleAssetPipelineSample
                VEngineExampleAssetPipelineSampleScripts
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMENT "Packaging VEngine example project: AssetPipelineSample"
            VERBATIM
        )

        set_target_properties(VEngineExampleAssetPipelineSamplePackage
            PROPERTIES
                FOLDER "Examples"
        )
    endif()

    if(TARGET VEngineEditor)
        add_dependencies(VEngineEditor VEngineExampleAssetPipelineSample)
    endif()
endfunction()
