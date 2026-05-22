#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
constexpr std::string_view ChineseRelativePath =
    "\xE4\xB8\xAD\xE6\x96\x87/\xE6\xB5\x8B\xE8\xAF\x95.txt";

bool Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }

    return condition;
}

template <typename T>
bool ExpectOk(const ve::Result<T>& result, const char* message)
{
    if (result)
    {
        return true;
    }

    std::cerr << "FAILED: " << message << ": " << ve::ToString(result.GetError().GetCode());

    if (!result.GetError().GetMessage().empty())
    {
        std::cerr << ": " << result.GetError().GetMessage();
    }

    std::cerr << '\n';
    return false;
}

bool ExpectOk(const ve::Result<void>& result, const char* message)
{
    if (result)
    {
        return true;
    }

    std::cerr << "FAILED: " << message << ": " << ve::ToString(result.GetError().GetCode());

    if (!result.GetError().GetMessage().empty())
    {
        std::cerr << ": " << result.GetError().GetMessage();
    }

    std::cerr << '\n';
    return false;
}

ve::Path GetTestRoot()
{
    return ve::FileSystem::GetCurrentWorkingDirectory() / "Generated/FileSystemTests";
}

void RemoveTestRoot()
{
    std::error_code error;
    std::filesystem::remove_all(std::filesystem::path("Generated") / "FileSystemTests", error);
}

bool ContainsEntry(
    const std::vector<ve::FileSystem::DirectoryEntry>& entries,
    std::string_view name,
    ve::FileSystem::DirectoryEntryType type)
{
    return std::any_of(entries.begin(), entries.end(), [name, type](const ve::FileSystem::DirectoryEntry& entry)
    {
        return entry.name == name && entry.type == type;
    });
}

bool TestPathNormalization()
{
    bool passed = true;

    passed &= Expect(
        ve::Path("Assets\\Textures\\./Brick.png").GetString() == "Assets/Textures/Brick.png",
        "Path should normalize slashes and dot segments");
    passed &= Expect(
        ve::Path("C:\\VEngine\\..\\Game\\file.txt").GetString() == "C:/Game/file.txt",
        "Path should preserve Windows drive roots and resolve parent segments");
    passed &= Expect(
        ve::Path("../Assets/./Mesh.fbx").GetString() == "../Assets/Mesh.fbx",
        "Relative path should preserve unresolved leading parent segments");
    passed &= Expect(
        ve::Path("Assets//Textures///Brick.png/").GetString() == "Assets/Textures/Brick.png",
        "Path should collapse repeated separators and trim trailing slash");
    passed &= Expect(
        ve::Path("//server/share/Folder/../File.txt").GetString() == "//server/share/File.txt",
        "UNC path should preserve server and share root");
    passed &= Expect(
        ve::Path("//server//share//Folder").GetString() == "//server/share/Folder",
        "UNC path should collapse repeated separators after the server name");
    passed &= Expect(
        ve::Path("//server/share").GetParentPath().GetString() == "//server/share",
        "UNC share root should parent to itself");
    passed &= Expect(ve::Path("/").GetParentPath().GetString() == "/", "Unix root should parent to itself");
    passed &= Expect(ve::Path("C:/").GetParentPath().GetString() == "C:/", "Windows drive root should parent to itself");
    passed &= Expect(ve::Path("C:/Game/file.txt").IsAbsolute(), "Windows drive-root path should be absolute");
    passed &= Expect(!ve::Path("C:Game/file.txt").IsAbsolute(), "Windows drive-relative path should stay relative");

    return passed;
}

bool TestPathQueriesAndJoining()
{
    bool passed = true;

    const ve::Path base("C:/Project");
    const ve::Path joined = base / "Assets\\Textures/Brick.png";

    passed &= Expect(
        joined.GetString() == "C:/Project/Assets/Textures/Brick.png",
        "Join should append relative paths and normalize separators");
    passed &= Expect(
        (base / ve::Path("D:/Other/File.txt")).GetString() == "D:/Other/File.txt",
        "Join should keep an absolute right-hand path");

    const ve::Path filePath("Assets/Textures/Brick.Albedo.png");
    passed &= Expect(filePath.GetParentPath().GetString() == "Assets/Textures", "Parent path should drop filename");
    passed &= Expect(filePath.GetFilename() == "Brick.Albedo.png", "Filename should return final segment");
    passed &= Expect(filePath.GetExtension() == ".png", "Extension should return final suffix");
    passed &= Expect(ve::Path(".gitignore").GetExtension().empty(), "Dotfile without suffix should not have extension");
    passed &= Expect(ve::Path("Assets").GetParentPath().IsEmpty(), "Single relative segment should have empty parent");

    return passed;
}

bool TestTextAndBinaryFiles()
{
    bool passed = true;

    const ve::Path root = GetTestRoot();
    const ve::Path textPath = root / "Text/Hello.txt";
    const ve::Path binaryPath = root / "Binary/Payload.bin";

    passed &= ExpectOk(ve::FileSystem::WriteTextFile(textPath, "line1\nline2"), "WriteTextFile should create parent dirs");

    const ve::Result<std::string> textResult = ve::FileSystem::ReadTextFile(textPath);
    passed &= ExpectOk(textResult, "ReadTextFile should read written text");

    if (textResult)
    {
        passed &= Expect(textResult.GetValue() == "line1\nline2", "ReadTextFile should preserve UTF-8 bytes and newlines");
    }

    const std::array<std::byte, 4> sourceBytes = {
        std::byte{0x00},
        std::byte{0x7F},
        std::byte{0x80},
        std::byte{0xFF},
    };

    passed &= ExpectOk(
        ve::FileSystem::WriteBinaryFile(binaryPath, sourceBytes.data(), sourceBytes.size()),
        "WriteBinaryFile should write raw bytes");

    const ve::Result<std::vector<std::byte>> binaryResult = ve::FileSystem::ReadBinaryFile(binaryPath);
    passed &= ExpectOk(binaryResult, "ReadBinaryFile should read written bytes");

    if (binaryResult)
    {
        const std::vector<std::byte> expectedBytes(sourceBytes.begin(), sourceBytes.end());
        passed &= Expect(binaryResult.GetValue() == expectedBytes, "ReadBinaryFile should preserve raw bytes");
    }

    return passed;
}

bool TestUtf8TextAndChinesePath()
{
    bool passed = true;

    const ve::Path root = GetTestRoot();
    const ve::Path bomPath = root / "Text/Bom.txt";
    const ve::Path chinesePath = root / ChineseRelativePath;

    const std::array<std::byte, 6> bomText = {
        std::byte{0xEF},
        std::byte{0xBB},
        std::byte{0xBF},
        std::byte{0x68},
        std::byte{0x69},
        std::byte{0x21},
    };

    passed &= ExpectOk(
        ve::FileSystem::WriteBinaryFile(bomPath, bomText.data(), bomText.size()),
        "WriteBinaryFile should write UTF-8 BOM test data");

    const ve::Result<std::string> bomResult = ve::FileSystem::ReadTextFile(bomPath);
    passed &= ExpectOk(bomResult, "ReadTextFile should read UTF-8 BOM file");

    if (bomResult)
    {
        passed &= Expect(bomResult.GetValue() == "hi!", "ReadTextFile should strip UTF-8 BOM");
    }

    passed &= ExpectOk(
        ve::FileSystem::WriteTextFile(chinesePath, "Chinese path smoke test"),
        "WriteTextFile should support UTF-8 paths");

    const ve::Result<std::string> chineseResult = ve::FileSystem::ReadTextFile(chinesePath);
    passed &= ExpectOk(chineseResult, "ReadTextFile should support UTF-8 paths");

    if (chineseResult)
    {
        passed &= Expect(
            chineseResult.GetValue() == "Chinese path smoke test",
            "Chinese path text should round-trip through Windows UTF-16 boundary");
    }

    return passed;
}

bool TestDirectoryOperations()
{
    bool passed = true;

    const ve::Path root = GetTestRoot();
    const ve::Path listRoot = root / "List";
    const ve::Path childDirectory = listRoot / "Child";
    const ve::Path nestedFile = childDirectory / "Nested.txt";
    const ve::Path filePath = listRoot / "Entry.txt";

    passed &= ExpectOk(ve::FileSystem::CreateDirectories(childDirectory), "CreateDirectories should create nested dirs");
    passed &= ExpectOk(ve::FileSystem::WriteTextFile(filePath, "entry"), "WriteTextFile should create a list file");
    passed &= ExpectOk(ve::FileSystem::WriteTextFile(nestedFile, "nested"), "WriteTextFile should create a nested file");

    passed &= Expect(ve::FileSystem::Exists(listRoot), "Exists should report created directory");
    passed &= Expect(ve::FileSystem::IsDirectory(childDirectory), "IsDirectory should report created directory");
    passed &= Expect(ve::FileSystem::IsFile(filePath), "IsFile should report created file");

    const ve::Result<std::vector<ve::FileSystem::DirectoryEntry>> entriesResult = ve::FileSystem::ListDirectory(listRoot);
    passed &= ExpectOk(entriesResult, "ListDirectory should list an existing directory");

    if (entriesResult)
    {
        const std::vector<ve::FileSystem::DirectoryEntry>& entries = entriesResult.GetValue();

        passed &= Expect(
            ContainsEntry(entries, "Child", ve::FileSystem::DirectoryEntryType::Directory),
            "ListDirectory should include immediate child directory");
        passed &= Expect(
            ContainsEntry(entries, "Entry.txt", ve::FileSystem::DirectoryEntryType::File),
            "ListDirectory should include immediate child file");
        passed &= Expect(
            !ContainsEntry(entries, "Nested.txt", ve::FileSystem::DirectoryEntryType::File),
            "ListDirectory should be non-recursive");
    }

    return passed;
}

bool TestProjectRootResolution()
{
    bool passed = true;

    const ve::Path previousRoot = ve::FileSystem::GetProjectRoot();
    const ve::Path root = GetTestRoot() / "Project";

    ve::FileSystem::SetProjectRoot(root);

    passed &= Expect(
        ve::FileSystem::GetProjectRoot() == root,
        "SetProjectRoot should store normalized project root");
    passed &= Expect(
        ve::FileSystem::ResolveProjectPath(ve::Path("Assets/Mesh.fbx")) == root / "Assets/Mesh.fbx",
        "ResolveProjectPath should resolve relative paths against project root");
    passed &= Expect(
        ve::FileSystem::ResolveProjectPath(ve::Path("C:/Absolute/File.txt")).GetString() == "C:/Absolute/File.txt",
        "ResolveProjectPath should leave absolute paths unchanged");

    ve::FileSystem::SetProjectRoot(previousRoot);
    return passed;
}

bool TestMissingFileError()
{
    bool passed = true;

    const ve::Result<std::string> missingResult =
        ve::FileSystem::ReadTextFile(GetTestRoot() / "Missing/DoesNotExist.txt");

    passed &= Expect(!missingResult, "ReadTextFile should fail for missing file");

    if (!missingResult)
    {
        passed &= Expect(
            missingResult.GetError().GetCode() == ve::ErrorCode::NotFound,
            "Missing file should return ErrorCode::NotFound");
    }

    return passed;
}
}

int main()
{
    RemoveTestRoot();

    bool passed = true;

    passed &= TestPathNormalization();
    passed &= TestPathQueriesAndJoining();
    passed &= TestTextAndBinaryFiles();
    passed &= TestUtf8TextAndChinesePath();
    passed &= TestDirectoryOperations();
    passed &= TestProjectRootResolution();
    passed &= TestMissingFileError();

    RemoveTestRoot();

    if (passed)
    {
        std::cout << "VEngineFileSystemTests passed" << '\n';
        return 0;
    }

    return 1;
}
