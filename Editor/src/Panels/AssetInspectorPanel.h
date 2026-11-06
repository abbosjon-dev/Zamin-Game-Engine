#pragma once

#include <Zamin/Zamin.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Zamin {

    class AssetUsageCache;

    // What the Inspector shows for a file picked in the content browser (instead of the selected
    // entity): name, type, path, UUID, where it is used and a preview for its type - textures
    // (size, channels, format, GPU memory, channel views), models (a 3D view that turns and plays
    // the animations, submeshes, triangles, materials, skeleton), audio (length, channels, sample
    // rate, waveform, play / stop), scenes and prefabs (entities), fonts, scripts, materials (a
    // turning preview sphere and the material editor); folders and several files get a summary.
    // Import settings of textures, models and sounds (load mode, preload, the file's volume) are
    // edited here: Apply saves them to the .zmeta file and imports the asset again at once
    // (everything using it updates in place); Revert drops the edits. Material edits show in the scene at once (the shared material object changes) and go
    // to the .zmat file with Save; Revert puts the saved values back.
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
        // Files were written here (a model's materials exported): the content browser lists them.
        std::function<void(const std::vector<std::filesystem::path>&)> OnFilesCreated;
        // Textures: the Sprite Editor is to open for the shown file.
        std::function<void(const std::filesystem::path&)> OnOpenSpriteEditor;
        // "Animatsiya oynasida ochish" of a .zanim clip.
        std::function<void(const std::filesystem::path&)> OnOpenAnimation;

        // Models: writes the file's materials as .zmat files (Model::ExportMaterials) into the
        // default folder next to it. False when nothing could be exported (the reason is shown).
        bool ExportModelMaterials();
        // Automation: the model's Materials section is opened and scrolled to.
        void RevealModelMaterials() { m_ExportScrollTo = true; }
        // The last export of the shown model (OutputDirectory empty = none).
        const ModelMaterialExport& GetLastMaterialExport() const { return m_Export; }

        // Edited import settings that are not applied yet.
        bool HasUnappliedChanges() const;
        // Saves the edited import settings and imports the asset again. False when there was
        // nothing to apply or the asset is unknown to the registry.
        bool Apply();
        void Revert();

        // Automation / tests: the settings being edited, preview controls.
        TextureImportSettings& GetEditedTextureSettings() { return m_TextureEdit; }
        ModelImportSettings& GetEditedModelSettings() { return m_ModelEdit; }
        AudioClipSettings& GetEditedAudioSettings() { return m_AudioEdit; }
        void SetTextureChannelView(ImageChannelView view) { m_ChannelView = view; }
        // Audio: plays the sound; model: plays its first animation.
        void StartPreviewPlayback();
        // Models: entry `index` of the clip settings previewed, the view scrolled to them (automation).
        void PreviewClipSetting(int index) { m_PendingClipSettingPreview = index; m_ScrollToClipSettings = true; }
        void StopAudio();
        uint32_t PlayPreviewSound();
        // Audio: the preview loops (inside the edited loop region); the waveform zoomed in
        // `factor` times around `center` (a fraction of the sound).
        void SetPreviewLoop(bool loop) { m_Loop = loop; }
        void ZoomWaveform(double factor, double center);
        const WaveformView& GetWaveformView() const { return m_WaveView; }

        // The shown material's values as edited (null when no material is shown). After changing
        // them, ApplyMaterialEdit shows them on the material (what the editor fields do).
        MaterialData* GetEditedMaterial() { return m_Material ? &m_MaterialEdit : nullptr; }
        void ApplyMaterialEdit();
        // Writes the edited values to the .zmat file / puts the saved ones back.
        bool SaveMaterial();
        void RevertMaterial();
        bool HasUnsavedMaterial() const { return m_Material && !(m_MaterialEdit == m_MaterialSaved); }
        MaterialPreview::Settings& GetMaterialPreviewSettings() { return m_MaterialView; }
        void SetMaterialSceneLighting(bool scene) { m_MaterialSceneLight = scene; }
        // Lighting of the material preview's "scene" mode: fills the open scene's HDRI (map,
        // intensity, rotation); false when it has none.
        std::function<bool(MaterialPreview::Settings&)> GetSceneLighting;

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
            AudioClipSettings Audio;
            Ref<MaterialAsset> Material;          // materials: the object holding the edits
            MaterialData MaterialSaved, MaterialEdit;
        };
        PendingEdits CaptureEdits() const;
        bool ApplyEdits(const PendingEdits& edits, bool showStatus);
        // Before another asset is shown: unapplied edits go to the popup.
        void KeepUnappliedEdits();

        void DrawHeader(const AssetMetadata* meta);
        // The file could not be imported: the reason (AssetImportLog) and a retry button.
        void DrawImportError();
        void DrawUsage();
        void DrawTexture(const AssetMetadata* meta);
        void DrawTexturePreview(const Ref<Texture2D>& texture);
        void DrawModel(const AssetMetadata* meta);
        void DrawModelPreview(const Ref<Model>& model);
        void DrawModelInfo(const Ref<Model>& model);
        // The clip settings of the import (AssetInspectorModelClips.cpp): trim / split the file's clips.
        void DrawModelClipSettings();
        const AnimationClip* FindSourceClip(const std::string& name) const;
        void StartClipSettingPreview(int entry);
        void RefreshClipSettingPreview();
        void DrawAudio(const AssetMetadata* meta);
        void DrawWaveform(float height);
        void DrawSceneFile();
        void DrawFont();
        void DrawScript();
        void DrawMaterial();
        // A physics material (.zphysmat): its values, edited live (the running scene follows) and
        // saved to the file once an edit is finished.
        void DrawPhysicsMaterial();
        void DrawAnimationClip();
        // A sprite animation clip (.zsanim): a playing preview, its frames, rate and events, edited
        // live and saved to the file once an edit is finished.
        void DrawSpriteClip();
        void DrawMaterialPreview();
        // The material's fields (into m_MaterialEdit); true when one changed.
        bool DrawMaterialFields();
        void DrawMaterialSaveBar();
        // The material changed elsewhere (its file was read again): taken over, edits stay on top.
        void SyncMaterial();
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
        AudioClipSettings m_AudioSaved, m_AudioEdit;
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
        // the clip settings' preview: entry m_ClipSettingPreview of m_ModelEdit.Clips cut out of its
        // source as edited (not applied yet), shown instead of m_Clip (-1: none)
        int m_ClipSettingPreview = -1;
        AnimationClip m_ClipSettingClip;
        bool m_ScrollToClipSettings = false;
        int m_PendingClipSettingPreview = -1;   // started once the model is there
        // the last "export materials" of the shown model
        bool m_ExportOverwrite = false;
        std::filesystem::path m_ExportModel;  // which model m_Export is about (empty = none yet)
        ModelMaterialExport m_Export;
        std::string m_ExportError;
        bool m_ExportOpenSection = false;     // open the Materials section once (the result is shown there)
        bool m_ExportScrollTo = false;

        // material: the library's shared object (edits show in the scene at once)
        Ref<MaterialAsset> m_Material;
        bool m_MaterialLoaded = false;
        bool m_PhysicsMaterialUnsaved = false;
        // sprite clip preview
        float m_SpriteClipTime = 0.0f;
        bool m_SpriteClipPlaying = true;
        bool m_SpriteClipUnsaved = false;
        MaterialData m_MaterialSaved, m_MaterialEdit;
        uint64_t m_MaterialRevision = 0;      // the material's revision after this panel's last change
        MaterialPreview m_MaterialPreview;
        MaterialPreview::Settings m_MaterialView;
        bool m_MaterialAutoRotate = true;
        bool m_MaterialSceneLight = false;    // the open scene's HDRI instead of the sky gradient

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
        // The waveform's zoom and what the mouse is dragging on it.
        enum class WaveDrag { None, Scrub, LoopStart, LoopEnd, NewLoop, Pan };
        WaveformView m_WaveView;
        WaveDrag m_WaveDrag = WaveDrag::None;
        double m_WaveDragAnchor = 0.0;      // the sound fraction the drag started at
        float m_ScrubTime = -1.0f;          // where the last scrub seek went
        float m_WaveMenuTime = 0.0f;        // the time the context menu was opened at
        glm::vec2 m_PreviewLoop{ -1.0f };   // the loop region the playing preview got (seconds)
        // The loop region the preview should use (the edited import settings; 0, 0: all of it).
        glm::vec2 GetEditedLoopRegion() const;
        void ApplyPreviewLoop();

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

        // import error of the shown file (looked up again when the log changes)
        std::optional<AssetImportError> m_ImportError;
        std::filesystem::path m_ImportErrorPath;
        uint64_t m_ImportErrorRevision = ~0ull;

        // Last apply result shown under the buttons.
        std::string m_Status;
        bool m_StatusError = false;
        double m_StatusTime = -100.0;
    };

}
