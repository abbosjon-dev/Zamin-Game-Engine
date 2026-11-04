#pragma once

#include <cstddef>

namespace Zamin {

    class ConsolePanel
    {
    public:
        void OnImGuiRender(bool* open);

    private:
        bool m_ShowInfo = true, m_ShowWarn = true, m_ShowError = true, m_ShowTrace = false;
        bool m_AutoScroll = true;
        size_t m_LastCount = 0;
    };

}
