#include "Editor/macOS/MacEditorProjectRegistryBackend.h"

#include "Editor/Core/EditorProjectRegistry.h"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace ve::editor
{
    namespace
    {
        NSString* const RecentProjectsDefaultsKey = @"RecentProjects";

        [[nodiscard]] bool ContainsProjectPath(std::span<const std::string> projectPaths, const std::string& path)
        {
            return std::find(projectPaths.begin(), projectPaths.end(), path) != projectPaths.end();
        }
    } // namespace

    std::vector<std::string> MacEditorProjectRegistryBackend::LoadRecentProjects()
    {
        std::vector<std::string> recentProjects;

        NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
        NSArray* storedProjects = [defaults arrayForKey:RecentProjectsDefaultsKey];
        if (storedProjects == nil)
        {
            return recentProjects;
        }

        for (id storedProject in storedProjects)
        {
            if (![storedProject isKindOfClass:[NSString class]])
            {
                continue;
            }

            const char* utf8Path = [(NSString*)storedProject UTF8String];
            if (utf8Path == nullptr)
            {
                continue;
            }

            std::string path(utf8Path);
            if (!path.empty() && !ContainsProjectPath(recentProjects, path))
            {
                recentProjects.push_back(std::move(path));
            }

            if (recentProjects.size() >= EditorProjectRegistry::MaxRecentProjectCount)
            {
                break;
            }
        }

        return recentProjects;
    }

    void MacEditorProjectRegistryBackend::SaveRecentProjects(std::span<const std::string> projectPaths)
    {
        NSMutableArray* storedProjects = [NSMutableArray arrayWithCapacity:EditorProjectRegistry::MaxRecentProjectCount];
        std::size_t savedCount = 0;
        for (const std::string& path : projectPaths)
        {
            if (path.empty())
            {
                continue;
            }

            NSString* storedPath = [NSString stringWithUTF8String:path.c_str()];
            if (storedPath == nil)
            {
                continue;
            }

            [storedProjects addObject:storedPath];
            ++savedCount;
            if (savedCount >= EditorProjectRegistry::MaxRecentProjectCount)
            {
                break;
            }
        }

        NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
        [defaults setObject:storedProjects forKey:RecentProjectsDefaultsKey];
    }

    std::unique_ptr<EditorProjectRegistryBackend> CreateMacEditorProjectRegistryBackend()
    {
        return std::make_unique<MacEditorProjectRegistryBackend>();
    }
} // namespace ve::editor
