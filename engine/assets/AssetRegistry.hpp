#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace engine::assets
{
enum class AssetKind
{
    Unknown,
    Mesh,
    Texture,
    Material,
    Animation,
    Environment,
    Prefab,
    Loop,
    Map
};

struct AssetEntry
{
    std::string relativePath;
    std::string name;
    bool directory = false;
    AssetKind kind = AssetKind::Unknown;
};

struct ImportResult
{
    bool success = false;
    std::string relativePath;
    std::string message;
};

class AssetRegistry
{
public:
    explicit AssetRegistry(const std::filesystem::path& assetsRoot = "assets");

    void EnsureAssetDirectories() const;
    [[nodiscard]] std::vector<AssetEntry> ListDirectory(const std::string& relativeDir) const;

    [[nodiscard]] ImportResult ImportExternalFile(const std::string& sourcePath) const;
    [[nodiscard]] ImportResult ImportExternalFileToDirectory(
        const std::string& sourcePath,
        const std::string& targetRelativeDirectory
    ) const;
    [[nodiscard]] bool CreateFolder(const std::string& relativeDir, std::string* outError = nullptr) const;
    [[nodiscard]] bool DeletePath(const std::string& relativePath, std::string* outError = nullptr) const;
    [[nodiscard]] bool RenamePath(const std::string& fromRelativePath, const std::string& toRelativePath, std::string* outError = nullptr) const;

    [[nodiscard]] std::filesystem::path AbsolutePath(const std::string& relativePath) const;
    [[nodiscard]] std::string NormalizeRelativePath(const std::filesystem::path& path) const;
    [[nodiscard]] static AssetKind KindFromPath(const std::filesystem::path& path);

private:
    [[nodiscard]] std::filesystem::path ImportDirectoryForExtension(const std::string& extensionLower) const;
    [[nodiscard]] bool NeedsImport(const std::filesystem::path& source, const std::filesystem::path& destination) const;
    [[nodiscard]] bool WriteMetaFile(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::string* outError
    ) const;

    std::filesystem::path m_assetsRoot;
};
} // namespace engine::assets
