#include "engine/assets/AssetRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

#include <nlohmann/json.hpp>

namespace engine::assets
{
namespace
{
using json = nlohmann::json;

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string SanitizePathPart(std::string value)
{
    for (char& c : value)
    {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == '/'))
        {
            c = '_';
        }
    }
    return value;
}
} // namespace

AssetRegistry::AssetRegistry(const std::filesystem::path& assetsRoot)
    : m_assetsRoot(assetsRoot)
{
}

void AssetRegistry::EnsureAssetDirectories() const
{
    std::error_code ec;
    std::filesystem::create_directories(m_assetsRoot, ec);
    std::filesystem::create_directories(m_assetsRoot / "meshes", ec);
    std::filesystem::create_directories(m_assetsRoot / "textures", ec);
    std::filesystem::create_directories(m_assetsRoot / "materials", ec);
    std::filesystem::create_directories(m_assetsRoot / "animations", ec);
    std::filesystem::create_directories(m_assetsRoot / "environments", ec);
    std::filesystem::create_directories(m_assetsRoot / "prefabs", ec);
    std::filesystem::create_directories(m_assetsRoot / "loops", ec);
    std::filesystem::create_directories(m_assetsRoot / "maps", ec);
}

std::vector<AssetEntry> AssetRegistry::ListDirectory(const std::string& relativeDir) const
{
    EnsureAssetDirectories();
    std::vector<AssetEntry> entries;

    std::filesystem::path dir = m_assetsRoot;
    if (!relativeDir.empty() && relativeDir != "." && relativeDir != "/")
    {
        dir /= relativeDir;
    }
    if (!std::filesystem::exists(dir))
    {
        return entries;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        AssetEntry out;
        out.directory = entry.is_directory();
        out.name = entry.path().filename().string();
        out.relativePath = NormalizeRelativePath(std::filesystem::relative(entry.path(), m_assetsRoot));
        out.kind = out.directory ? AssetKind::Unknown : KindFromPath(entry.path());
        entries.push_back(out);
    }

    std::sort(entries.begin(), entries.end(), [](const AssetEntry& a, const AssetEntry& b) {
        if (a.directory != b.directory)
        {
            return a.directory && !b.directory;
        }
        return a.name < b.name;
    });
    return entries;
}

ImportResult AssetRegistry::ImportExternalFile(const std::string& sourcePath) const
{
    EnsureAssetDirectories();
    ImportResult result;

    if (sourcePath.empty())
    {
        result.message = "Source path is empty.";
        return result;
    }

    std::filesystem::path source = sourcePath;
    std::error_code ec;
    if (!std::filesystem::exists(source, ec) || !std::filesystem::is_regular_file(source, ec))
    {
        result.message = "Source file not found: " + sourcePath;
        return result;
    }

    const std::string ext = ToLower(source.extension().string());
    const std::filesystem::path importDir = ImportDirectoryForExtension(ext);
    if (importDir.empty())
    {
        result.message = "Unsupported extension: " + ext;
        return result;
    }

    std::filesystem::create_directories(importDir, ec);
    if (ec)
    {
        result.message = "Failed to create import folder: " + importDir.string();
        return result;
    }

    const std::string safeName = SanitizePathPart(source.filename().string());
    const std::filesystem::path destination = importDir / safeName;

    if (!NeedsImport(source, destination))
    {
        result.success = true;
        result.relativePath = NormalizeRelativePath(std::filesystem::relative(destination, m_assetsRoot));
        result.message = "Asset unchanged, import skipped.";
        return result;
    }

    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        result.message = "Import copy failed: " + ec.message();
        return result;
    }

    std::string metaError;
    if (!WriteMetaFile(source, destination, &metaError))
    {
        result.message = metaError;
        return result;
    }

    result.success = true;
    result.relativePath = NormalizeRelativePath(std::filesystem::relative(destination, m_assetsRoot));
    result.message = "Imported " + destination.filename().string();
    return result;
}

bool AssetRegistry::CreateFolder(const std::string& relativeDir, std::string* outError) const
{
    EnsureAssetDirectories();
    if (relativeDir.empty())
    {
        if (outError != nullptr)
        {
            *outError = "Folder path is empty.";
        }
        return false;
    }
    std::error_code ec;
    const std::filesystem::path absolute = AbsolutePath(relativeDir);
    std::filesystem::create_directories(absolute, ec);
    if (ec)
    {
        if (outError != nullptr)
        {
            *outError = "Failed to create folder: " + ec.message();
        }
        return false;
    }
    return true;
}

bool AssetRegistry::DeletePath(const std::string& relativePath, std::string* outError) const
{
    EnsureAssetDirectories();
    if (relativePath.empty())
    {
        if (outError != nullptr)
        {
            *outError = "Path is empty.";
        }
        return false;
    }

    std::error_code ec;
    const std::filesystem::path absolute = AbsolutePath(relativePath);
    if (!std::filesystem::exists(absolute, ec))
    {
        if (outError != nullptr)
        {
            *outError = "Path does not exist.";
        }
        return false;
    }

    std::filesystem::remove_all(absolute, ec);
    if (ec)
    {
        if (outError != nullptr)
        {
            *outError = "Delete failed: " + ec.message();
        }
        return false;
    }
    return true;
}

bool AssetRegistry::RenamePath(const std::string& fromRelativePath, const std::string& toRelativePath, std::string* outError) const
{
    EnsureAssetDirectories();
    if (fromRelativePath.empty() || toRelativePath.empty())
    {
        if (outError != nullptr)
        {
            *outError = "Rename path is empty.";
        }
        return false;
    }

    std::error_code ec;
    const std::filesystem::path from = AbsolutePath(fromRelativePath);
    const std::filesystem::path to = AbsolutePath(toRelativePath);
    std::filesystem::create_directories(to.parent_path(), ec);
    ec.clear();
    std::filesystem::rename(from, to, ec);
    if (ec)
    {
        if (outError != nullptr)
        {
            *outError = "Rename failed: " + ec.message();
        }
        return false;
    }
    return true;
}

std::filesystem::path AssetRegistry::AbsolutePath(const std::string& relativePath) const
{
    if (relativePath.empty() || relativePath == ".")
    {
        return m_assetsRoot;
    }
    return m_assetsRoot / relativePath;
}

std::string AssetRegistry::NormalizeRelativePath(const std::filesystem::path& path) const
{
    return path.generic_string();
}

AssetKind AssetRegistry::KindFromPath(const std::filesystem::path& path)
{
    const std::string ext = ToLower(path.extension().string());
    if (ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx")
    {
        return AssetKind::Mesh;
    }
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp")
    {
        return AssetKind::Texture;
    }
    if (ext == ".json")
    {
        const std::string p = path.generic_string();
        if (p.find("/materials/") != std::string::npos || p.find("\\materials\\") != std::string::npos)
        {
            return AssetKind::Material;
        }
        if (p.find("/animations/") != std::string::npos || p.find("\\animations\\") != std::string::npos)
        {
            return AssetKind::Animation;
        }
        if (p.find("/environments/") != std::string::npos || p.find("\\environments\\") != std::string::npos)
        {
            return AssetKind::Environment;
        }
        if (p.find("/prefabs/") != std::string::npos || p.find("\\prefabs\\") != std::string::npos)
        {
            return AssetKind::Prefab;
        }
        if (p.find("/loops/") != std::string::npos || p.find("\\loops\\") != std::string::npos)
        {
            return AssetKind::Loop;
        }
        if (p.find("/maps/") != std::string::npos || p.find("\\maps\\") != std::string::npos)
        {
            return AssetKind::Map;
        }
    }
    return AssetKind::Unknown;
}

std::filesystem::path AssetRegistry::ImportDirectoryForExtension(const std::string& extensionLower) const
{
    if (extensionLower == ".gltf" || extensionLower == ".glb" || extensionLower == ".obj" || extensionLower == ".fbx")
    {
        return m_assetsRoot / "meshes";
    }
    if (extensionLower == ".png" || extensionLower == ".jpg" || extensionLower == ".jpeg" || extensionLower == ".tga" || extensionLower == ".bmp")
    {
        return m_assetsRoot / "textures";
    }
    if (extensionLower == ".json")
    {
        return m_assetsRoot;
    }
    return {};
}

bool AssetRegistry::NeedsImport(const std::filesystem::path& source, const std::filesystem::path& destination) const
{
    std::error_code ec;
    if (!std::filesystem::exists(destination, ec))
    {
        return true;
    }

    const auto srcTime = std::filesystem::last_write_time(source, ec);
    if (ec)
    {
        return true;
    }
    ec.clear();
    const auto dstTime = std::filesystem::last_write_time(destination, ec);
    if (ec)
    {
        return true;
    }

    if (srcTime > dstTime)
    {
        return true;
    }

    ec.clear();
    const auto srcSize = std::filesystem::file_size(source, ec);
    if (ec)
    {
        return true;
    }
    ec.clear();
    const auto dstSize = std::filesystem::file_size(destination, ec);
    if (ec)
    {
        return true;
    }
    return srcSize != dstSize;
}

bool AssetRegistry::WriteMetaFile(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::string* outError
) const
{
    std::error_code ec;
    const auto sourceTime = std::filesystem::last_write_time(source, ec);
    if (ec)
    {
        if (outError != nullptr)
        {
            *outError = "Failed to read source timestamp: " + ec.message();
        }
        return false;
    }

    const std::filesystem::path metaPath = destination;
    const std::filesystem::path sidecar = metaPath.string() + ".meta.json";

    json meta;
    meta["asset_guid"] = std::to_string(std::hash<std::string>{}(destination.generic_string()));
    meta["source_path"] = source.generic_string();
    meta["import_path"] = NormalizeRelativePath(std::filesystem::relative(destination, m_assetsRoot));
    meta["source_write_time"] = sourceTime.time_since_epoch().count();
    meta["import_settings"] = {
        {"generate_mips", true},
        {"compress", false},
    };

    std::ofstream stream(sidecar);
    if (!stream.is_open())
    {
        if (outError != nullptr)
        {
            *outError = "Failed to write asset metadata: " + sidecar.generic_string();
        }
        return false;
    }
    stream << meta.dump(2) << "\n";
    return true;
}
} // namespace engine::assets
