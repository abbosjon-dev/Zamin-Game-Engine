#pragma once

#include <Zamin/Zamin.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Zamin {

    class AssetUsageCache;

    // What the Inspector shows for a file picked in the content browser (instead of the selected
    // entity): name, type, path, UUID, where it is used and a preview for its type - textures
    // (size, channels, format, GPU memory, channel views), models (a 3D view that turns and plays
    // the animations, submeshes, triangles, materials, skeleton), audio (length, channels, sample
    // rate, waveform, play / stop), scenes and prefabs (entities), fonts, scripts; folders and
    // several files get a summary. Import settings of textures and models are edited here: Apply
    // saves them to the .zmeta file and imports the asset again at once (everything using it
    // updates in place); Revert drops the edits.
    class AssetInspectorPanel
    {
    public:
        AssetInspectorPanel();
        ~AssetInspectorPanel();

        // A file or folder of the project (absolute); an empty path clears the panel. Edits that
        // were not applied are offered for applying first (DrawPopups).
        void SetAsset(const std::filesystem::path& path);
        // The content browser's selection: more than one item shows a summary of them.
        void SetSelection(const std::vector<std::filesystem::path>& paths);
        // Nothing shown (an entity was picked): unapplied edits are offered for applying first.
        void Clear();
        // Another project: everything goes, unapplied edits too.
        void Reset();
        bool HasAsset() const { return !m_Path.empty(); }
        const std::filesystem::path& GetPath() const { return m_Path; }

        // Draws into the current window (the Inspector).
        void OnImGuiRender();
        // Modal popups (unapplied import settings); called once a frame outside any window.
        void DrawPopups();

        // Where the "used by" list comes from (owned by the editor).
        void SetUsageCache(AssetUsageCache* cache) { m_Usage = cache; }
        // A file of the list was clicked: the editor shows it in the content browser (and here).
        std::function<void(const std::filesystem::path&)> OnRevealRequested;
        std::function<void(const std::filesystem::path&)> OnOpenScene;

        // Edited import settings that are not applied yet.
        bool HasUnappliedChanges() const;
        // Saves the edited import settings and imports the asset again. False when there was
        // nothing to apply or the asset is unknown to the registry.
        bool Apply();
        void Revert();

        // Automation / tests: the settings being edited, preview controls.
        TextureImportSettings& GetEditedTextureSettings() { return m_TextureEdit; }
        ModelImportSettings& GetEditedModelSettings() { return m_ModelEdit; }
        void SetTextureChannelView(ImageChannelView view) { m_ChannelView = view; }
        // Audio: plays the sound; model: plays its first animation.
        void StartPreviewPlayback();
        void StopAudio();

    private:
        const AssetMetadata* FindMetadata() const;
        // Follows the asset when it moved (by UUID) and picks up settings changed on disk.
        void Sync();
        void LoadSettings(const AssetMetadata* meta);
        // The file changed on disk (or another file is shown): cached file info is read again.
        bool CheckFileVersion();
        void ResetPreviews();

        // Saves edited settings of an asset that may not be the one shown any more.
        struct PendingEdits
        {
            std::filesystem::path Path;
            AssetHandle Handle;
            AssetType Type = AssetType::Other;
            TextureImportSettings Texture;
            ModelImportSettings Model;
        };
        PendingEdits CaptureEdits() const;
        bool ApplyEdits(const PendingEdits& edits, bool showStatus);
        // Before another asset is shown: unapplied edits go to the popup.
        void KeepUnappliedEdits();

        void DrawHeader(const AssetMetadata* meta);
        void DrawUsage();
        void DrawTexture(const AssetMetadata* meta);
        void DrawTexturePreview(const Ref<Texture2D>& texture);
        void DrawModel(const AssetMetadata* meta);
        void DrawModelPreview(const Ref<Model>& model);
        void DrawModelInfo(const Ref<Model>& model);
        void DrawAudio();
        void DrawWaveform(float height);
        void DrawSceneFile();
        void DrawFont();
        void DrawScript();
        void DrawFolder();
        void DrawMultiSelection();
        void DrawApplyBar(bool canApply);
        // The texture for the preview: loaded once per file version (a broken file is not read
        // again every frame).
        Ref<Texture2D> GetPreviewTexture();
        // The texture shown for the chosen channel view (the preview itself for RGBA).
        Ref<Texture2D> GetChannelTexture(const Ref<Texture2D>& preview);
        const ImageData* GetPixels();
        void RenderModelPreview(const Ref<Model>& model, uint32_t width, uint32_t height);

    private:
        std::filesystem::path m_Path;
        AssetType m_Type = AssetType::Other;
        AssetHandle m_Handle;
        bool m_IsFolder = false;
        std::vector<std::filesystem::path> m_Selection;
        AssetUsageCache* m_Usage = nullptr;

        TextureImportSettings m_TextureSaved, m_TextureEdit;
        ModelImportSettings m_ModelSaved, m_ModelEdit;
        std::vector<PendingEdits> m_Pending;  // unapplied edits of assets no longer shown

        // version of the shown file (cached info is read again when it changes)
        std::filesystem::path m_VersionPath;
        std::filesystem::file_time_type m_VersionTime{};
        uintmax_t m_VersionSize = 0;

        // texture
        Ref<Texture2D> m_Preview;
        std::filesystem::path m_PreviewPath;
        std::filesystem::file_time_type m_PreviewTime{};
        ImageFileInfo m_ImageInfo;
        bool m_ImageInfoRead = false;
        ImageChannelView m_ChannelView = ImageChannelView::RGBA;
        ImageChannelView m_ChannelTextureView = ImageChannelView::RGBA;
        Ref<Texture2D> m_ChannelTexture;
        Ref<Texture2D> m_ChannelSource;       // the preview texture the channel view was made for
        uint32_t m_ChannelSourceWidth = 0, m_ChannelSourceHeight = 0;
        std::unique_ptr<ImageData> m_Pixels;  // decoded on demand (channel views, pixel under the mouse)
        bool m_PixelsFailed = false;
        int m_Zoom = 0;                       // 0 = fit, else pixels per texel

        // model
        Ref<Model> m_Model;
        bool m_ModelLoaded = false;
        ModelFileInfo m_ModelInfo;
        bool m_ModelInfoRead = false;
        Ref<Framebuffer> m_ModelTarget, m_ModelFinal;
        float m_Yaw = 0.6f, m_Pitch = 0.35f, m_ZoomDistance = 1.0f;
        bool m_AutoRotate = true;
        int m_Clip = -1;                      // animation shown (-1 = bind pose)
        bool m_ClipPlaying = false;
        float m_ClipTime = 0.0f;
        double m_LastPreviewTime = -1.0;
        std::vector<glm::mat4> m_Joints;

        // font
        Ref<Font> m_Font;
        bool m_FontLoaded = false;

        // audio
        AudioFileInfo m_AudioInfo;
        bool m_AudioInfoRead = false;
        struct Waveform
        {
            std::atomic<bool> Done{ false };
            std::vector<glm::vec2> Peaks;
            std::string Error;
        };
        std::shared_ptr<Waveform> m_Waveform;
        uint32_t m_Sound = 0;
        bool m_Loop = false;
        float m_Volume = 0.8f;

        // scene / prefab / script / folder summaries (read once per version)
        struct FileSummary
        {
            bool Read = false;
            bool Valid = false;
            size_t Entities = 0;
            std::vector<std::string> Roots;
            std::vector<std::pair<std::string, size_t>> Components; // name, count
            size_t Lines = 0;
            std::vector<std::string> Classes;
            std::string Error;
        } m_Summary;
        struct FolderSummary
        {
            std::filesystem::path Path;
            double Time = -100.0;
            size_t Files = 0, Folders = 0;
            uintmax_t Size = 0;
            std::vector<std::pair<AssetType, size_t>> Types;
        } m_FolderSummary;

        // Last apply result shown under the buttons.
        std::string m_Status;
        bool m_StatusError = false;
        double m_StatusTime = -100.0;
    };

}
