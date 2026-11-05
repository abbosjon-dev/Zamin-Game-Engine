#pragma once

#include <Zamin/Zamin.h>

#include <imgui.h>

#include <memory>
#include <optional>
#include <string>

namespace Zamin {

    // "Sprite muharriri": cuts a texture into named frames - a grid by cell size or by the number of
    // columns / rows, with an offset and padding between the cells, empty cells skipped - or frames
    // drawn by hand on the picture, and edits each frame's name, rectangle, pivot and 9-slice
    // borders. The texture is shown zoomable (mouse wheel) and pannable (middle / right mouse) with
    // every frame's rectangle on it. Save writes the frames to the texture's .zmeta file (its
    // "Sprites" import setting), where Sprite Renderers and Sprite Animations find them by name.
    class SpriteEditorPanel
    {
    public:
        // Shows the window for a texture (path relative to Assets). Unsaved frames of another one
        // are offered for saving first.
        void Open(const std::string& texturePath);
        void OnImGuiRender(bool* show);
        // Another project: everything goes, unsaved frames too.
        void Reset();
        const std::string& GetTexturePath() const { return m_TexturePath; }

        bool HasUnsavedChanges() const { return !m_TexturePath.empty() && !(m_Edit == m_Saved); }
        // Writes the frames to the texture's .zmeta. False when there is no such asset.
        bool Save();
        void Revert();

        // Slices the texture with the current settings: `replace` drops the frames there were.
        // Returns how many frames were made.
        size_t Slice(bool replace = true);
        SpriteSliceSettings& GetSliceSettings() { return m_Slice; }
        SpriteSheet& GetEditedSheet() { return m_Edit; }
        void SetSelected(int index) { m_Selected = index; }
        void RequestFocus() { m_FocusRequested = true; }

    private:
        void Load(const std::string& texturePath);
        // The registry's frames changed on disk (and nothing is edited here): taken over.
        void Sync();
        const ImageData* GetPixels();
        void DrawToolbar();
        void DrawSidebar();
        void DrawSliceSettings();
        void DrawFrameList();
        void DrawSelectedFrame();
        void DrawCanvas();
        void DrawUnsavedPopup(bool* show);
        void SetStatus(std::string text, bool error = false);
        // The pixel of the texture under a screen point (may lie outside the picture).
        glm::vec2 ScreenToPixel(const ImVec2& p) const;
        ImVec2 PixelToScreen(const glm::vec2& p) const;

    private:
        std::string m_TexturePath;
        AssetHandle m_Handle;
        Ref<Texture2D> m_Texture;
        uint32_t m_Width = 0, m_Height = 0;
        SpriteSheet m_Saved, m_Edit;
        uint64_t m_SeenRevision = ~0ull;
        SpriteSliceSettings m_Slice;
        bool m_SliceReplace = true;
        bool m_ShowSlicePreview = true;
        int m_Selected = -1;
        std::unique_ptr<ImageData> m_Pixels;  // rows top first (SkipEmpty)
        bool m_PixelsFailed = false;

        // canvas view
        float m_Zoom = 0.0f;                  // screen px per texel (0 = fit on the next frame)
        glm::vec2 m_Pan{ 0.0f };              // screen px from the canvas centre to the picture centre
        ImVec2 m_CanvasMin{ 0, 0 }, m_CanvasMax{ 0, 0 };
        bool m_Drawing = false;               // dragging out a new frame
        glm::ivec2 m_DrawStart{ 0 }, m_DrawEnd{ 0 };

        std::optional<std::string> m_PendingOpen; // waiting for the unsaved popup
        bool m_PendingClose = false;
        bool m_OpenUnsavedPopup = false;
        bool m_FocusRequested = false;
        std::string m_Status;
        bool m_StatusError = false;
        double m_StatusTime = -100.0;
    };

}
