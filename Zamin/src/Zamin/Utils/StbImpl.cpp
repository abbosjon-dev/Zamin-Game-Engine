#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "Zamin/Utils/Image.h"

namespace Zamin {

    bool Image::WritePNG(const std::string& path, uint32_t width, uint32_t height, uint32_t channels,
                         const void* data, bool flipVertically)
    {
        stbi_flip_vertically_on_write(flipVertically ? 1 : 0);
        int ok = stbi_write_png(path.c_str(), (int)width, (int)height, (int)channels, data, (int)(width * channels));
        stbi_flip_vertically_on_write(0);
        return ok != 0;
    }

}
