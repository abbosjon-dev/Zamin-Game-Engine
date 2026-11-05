#pragma once

#include <Zamin/Zamin.h>

#include <filesystem>
#include <string>

namespace Zamin {

    // What the Inspector shows for a file picked in the content browser (instead of the selected
    // entity): name, type, path, UUID and - for textures and models - the import settings of its
    // .zmeta file. Apply saves them and imports the asset again at once (everything using it
    // updates in place); Revert drops the edits.
    class AssetInspectorPanel
    {
    public:
        // A file of the project (absolute); an empty path or a folder clears the panel.
        void SetAsset(const std::filesystem::path& path);
        void Clear();
        bool HasAsset() const { return !m_Path.empty(); }
        const std::filesystem::path& GetPath() const { return m_Path; }

        // Draws into the current window (the Inspector).
        void OnImGuiRender();

        // Edited import settings that are not applied yet.
        bool HasUnappliedChanges() const;
        // Saves the edited import settings and imports the asset again. False when there was
        // nothing to apply or the asset is unknown to the registry.
        bool Apply();
        void Revert();

        // Automation / tests: the settings being edited.
        TextureImportSettings& GetEditedTextureSettings() { return m_TextureEdit; }
        ModelImportSettings& GetEditedModelSettings() { return m_ModelEdit; }

    private:
        const AssetMetadata* FindMetadata() const;
        // Follows the asset when it moved (by UUID) and picks up settings changed on disk.
        void Sync();
        void LoadSettings(const AssetMetadata* meta);

        void DrawHeader(const AssetMetadata* meta);
        void DrawTexture(const AssetMetadata* meta);
        void DrawModel(const AssetMetadata* meta);
        void DrawApplyBar(bool canApply);
        // The texture for the preview: loaded once per file version (a broken file is not read
        // again every frame).
        Ref<Texture2D> GetPreviewTexture();

    private:
        std::filesystem::path m_Path;
        AssetType m_Type = AssetType::Other;
        AssetHandle m_Handle;

        TextureImportSettings m_TextureSaved, m_TextureEdit;
        ModelImportSettings m_ModelSaved, m_ModelEdit;

        Ref<Texture2D> m_Preview;
        std::filesystem::path m_PreviewPath;
        std::filesystem::file_time_type m_PreviewTime{};

        // Last apply result shown under the buttons.
        std::string m_Status;
        bool m_StatusError = false;
        double m_StatusTime = -100.0;
    };

}
