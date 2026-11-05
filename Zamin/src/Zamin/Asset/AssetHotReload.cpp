#include "Zamin/Asset/AssetHotReload.h"
#include "Zamin/Asset/AssetImportLog.h"
#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Renderer/Environment.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Renderer/MaterialAsset.h"
#include "Zamin/Renderer/Model.h"
#include "Zamin/Renderer/Texture.h"

#include <algorithm>

namespace Zamin {

    namespace fs = std::filesystem;

    bool AssetHotReload::Start(const fs::path& assetDirectory, const FileWatcherOptions& options)
    {
        return m_Watcher.Start(assetDirectory, options);
    }

    void AssetHotReload::Stop()
    {
        m_Watcher.Stop();
        m_MissingWhileLoaded.clear();
    }

    AssetHotReload::Report AssetHotReload::Update(AssetRegistry* registry)
    {
        if (!m_Watcher.IsWatching())
            return {};
        std::vector<FileChange> changes = m_Watcher.Poll();
        if (changes.empty())
            return {};
        return Apply(changes, registry);
    }

    static void AddUnique(std::vector<fs::path>& list, const fs::path& path)
    {
        if (std::find(list.begin(), list.end(), path) == list.end())
            list.push_back(path);
    }

    AssetHotReload::Report AssetHotReload::Apply(const std::vector<FileChange>& changes, AssetRegistry* registry)
    {
        Report report;
        auto reload = [&](const fs::path& path) {
            bool failed = false;
            if (ReloadAsset(path, &failed) > 0)
                AddUnique(report.Reloaded, path);
            if (failed)
                AddUnique(report.Failed, path);
        };

        for (const FileChange& change : changes)
        {
            if (!change.IsDirectory && AssetMetaFile::IsMetaFile(change.Path))
            {
                // New import settings (edited by hand, or from version control): import again.
                if (registry && registry->OnMetaFileChanged(change.Path))
                    reload(AssetMetaFile::GetAssetPath(change.Path));
                continue;
            }
            // A new version of the file: its next import error is reported again.
            AssetImportLog::OnFileChanged(change.Path);
            switch (change.Type)
            {
                case FileChangeType::Added:
                {
                    if (registry)
                        registry->OnFileAdded(change.Path);
                    AddUnique(report.Added, change.Path);
                    // Put back after being deleted while in use (a brand new file is loaded fresh anyway).
                    auto missing = std::find(m_MissingWhileLoaded.begin(), m_MissingWhileLoaded.end(), change.Path);
                    if (missing != m_MissingWhileLoaded.end())
                    {
                        m_MissingWhileLoaded.erase(missing);
                        reload(change.Path);
                    }
                    // A texture a loaded material names but could not find: the material takes it now.
                    else if (!change.IsDirectory && AssetTypes::FromPath(change.Path) == AssetType::Texture &&
                             MaterialLibrary::UsesTexture(change.Path))
                        reload(change.Path);
                    break;
                }
                case FileChangeType::Modified:
                    if (registry && !registry->OnFileModified(change.Path))
                        registry->OnFileAdded(change.Path);
                    AddUnique(report.Modified, change.Path);
                    if (!change.IsDirectory)
                        reload(change.Path);
                    break;
                case FileChangeType::Removed:
                    if (registry)
                        registry->OnFileRemoved(change.Path);
                    AddUnique(report.Removed, change.Path);
                    if (!change.IsDirectory && IsLoaded(change.Path))
                        AddUnique(m_MissingWhileLoaded, change.Path); // keeps its content until it is back
                    break;
            }
        }
        if (registry)
        {
            report.Problems = registry->TakeProblems();
            report.Moved = registry->TakeDetectedMoves();
        }
        return report;
    }

    size_t AssetHotReload::ReloadAsset(const fs::path& path, bool* failed)
    {
        switch (AssetTypes::FromPath(path))
        {
            case AssetType::Texture:
            {
                // In place for the textures already loaded; materials whose texture was missing take it now.
                size_t textures = TextureLibrary::Reload(path, failed);
                size_t materials = MaterialLibrary::ReloadFile(path);
                return std::max(textures, materials);
            }
            case AssetType::Material:    return MaterialLibrary::ReloadFile(path, failed);
            case AssetType::Model:       return Model::ReloadFile(path, failed);
            case AssetType::Font:        return Font::ReloadFile(path, failed);
            case AssetType::Environment: return EnvironmentMap::ReloadFile(path, failed);
            case AssetType::Other:       return Model::ReloadFile(path, failed); // a model's .bin buffer
            default:                     return 0;
        }
    }

    bool AssetHotReload::IsLoaded(const fs::path& path)
    {
        switch (AssetTypes::FromPath(path))
        {
            case AssetType::Texture:     return TextureLibrary::IsLoaded(path) || MaterialLibrary::UsesTexture(path);
            case AssetType::Material:    return MaterialLibrary::IsFileLoaded(path);
            case AssetType::Model:       return Model::IsFileLoaded(path);
            case AssetType::Font:        return Font::IsFileLoaded(path);
            case AssetType::Environment: return EnvironmentMap::IsFileLoaded(path);
            case AssetType::Other:       return Model::IsFileLoaded(path);
            default:                     return false;
        }
    }

    bool AssetHotReload::CanReload(const fs::path& path)
    {
        AssetType type = AssetTypes::FromPath(path);
        return type == AssetType::Texture || type == AssetType::Model || type == AssetType::Font || type == AssetType::Environment ||
               type == AssetType::Material;
    }

}
