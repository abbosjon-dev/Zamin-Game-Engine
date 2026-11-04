#pragma once

#include <cstdint>
#include <string>

namespace Zamin {

    class Image
    {
    public:
        static bool WritePNG(const std::string& path, uint32_t width, uint32_t height, uint32_t channels,
                             const void* data, bool flipVertically = false);
    };

}
