#pragma once

#include <Zamin/Zamin.h>

#include <imgui.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Zamin {

    // The editor's shared "who uses which asset" index (asset inspector, unused assets window).
    // Rebuilt on demand after something changed - files on disk, the open scene - at most a couple
    // of times a second, so editing a scene does not rescan the project every frame.
    class AssetUsageCache
    {
    public:
        // Unsaved state that should count instead of the files on disk (the open scene), by path.
        std::function<std::unordered_map<std::string, std::string>()> GetOverrides;
        // Open things that have no file yet (a new scene that was never saved).
        std::function<std::vector<AssetReferenceIndex::UnsavedFile>()> GetUnsaved;

        void MarkDirty() { m_Dirty = true; }
        void Reset()
        {
            m_Index.Clear();
            m_Dirty = true;
            m_Revision++;
        }

        // The index, rebuilt first when it is out of date (and the last build is not too recent;
        // `force` skips that wait).
        const AssetReferenceIndex& Get(bool force = false)
        {
            double now = ImGui::GetTime();
            // Not while a mouse button is held (dragging a gizmo / slider marks the scene dirty every frame).
            bool busy = ImGui::IsAnyMouseDown();
            if (m_Dirty && (force || !m_Index.IsBuilt() || (now - m_BuildTime >= 0.75 && !busy)))
            {
                m_Dirty = false;
                m_BuildTime = now;
                if (m_Index.BuildForActiveProject(GetOverrides ? GetOverrides() : std::unordered_map<std::string, std::string>{},
                                                  GetUnsaved ? GetUnsaved() : std::vector<AssetReferenceIndex::UnsavedFile>{}))
                    m_Revision++;
            }
            return m_Index;
        }
        // Changes every time the index is rebuilt (panels refresh what they derived from it).
        uint64_t GetRevision() const { return m_Revision; }
        bool IsDirty() const { return m_Dirty; }

    private:
        AssetReferenceIndex m_Index;
        bool m_Dirty = true;
        double m_BuildTime = -100.0;
        uint64_t m_Revision = 0;
    };

}
