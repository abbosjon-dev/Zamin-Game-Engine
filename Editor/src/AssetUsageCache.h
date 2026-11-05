#pragma once

#include <Zamin/Zamin.h>

#include <imgui.h>

#include <functional>
#include <string>
#include <unordered_map>

namespace Zamin {

    // The editor's shared "who uses which asset" index (asset inspector, unused assets window).
    // Rebuilt on demand after something changed - files on disk, the open scene - at most a couple
    // of times a second, so editing a scene does not rescan the project every frame.
    class AssetUsageCache
    {
    public:
        // Unsaved state that should count instead of the files on disk (the open scene), by path.
        std::function<std::unordered_map<std::string, std::string>()> GetOverrides;

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
            if (m_Dirty && (force || !m_Index.IsBuilt() || now - m_BuildTime >= 0.5))
            {
                m_Dirty = false;
                m_BuildTime = now;
                if (m_Index.BuildForActiveProject(GetOverrides ? GetOverrides() : std::unordered_map<std::string, std::string>{}))
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
