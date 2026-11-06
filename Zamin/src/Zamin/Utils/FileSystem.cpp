#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Core/Log.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>

#if defined(__linux__)
    #include <unistd.h>
    #include <climits>
#endif

namespace Zamin {

    std::string FileSystem::ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        if (!in)
        {
            ZM_CORE_ERROR("Fayl ochilmadi: '{}'", path.string());
            return {};
        }
        // A folder opens as a stream on Linux (and seeking to its end gives a huge size - ext4:
        // INT64_MAX): there is no text in it, as before.
        std::error_code ec;
        const std::filesystem::file_status status = std::filesystem::status(path, ec);
        if (!ec && std::filesystem::is_directory(status))
            return {};
        // A regular file: read straight into the string at its size (large scene files: no
        // second copy through a stream).
        if (!ec && std::filesystem::is_regular_file(status))
        {
            const uintmax_t size = std::filesystem::file_size(path, ec);
            if (!ec && size > 0 && size <= (uintmax_t)std::numeric_limits<std::streamsize>::max() / 2)
            {
                std::string text;
                try
                {
                    text.resize((size_t)size);
                }
                catch (const std::exception&)
                {
                    ZM_CORE_ERROR("Fayl o'qilmadi (xotira yetmadi): '{}' ({} bayt)", path.string(), size);
                    return {};
                }
                in.read(text.data(), (std::streamsize)size);
                text.resize((size_t)std::max<std::streamsize>(in.gcount(), 0)); // it may have shrunk meanwhile
                return text;
            }
        }
        // Not a regular file of known size (a pipe ...): as a stream.
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    bool FileSystem::WriteTextFile(const std::filesystem::path& path, const std::string& content)
    {
        if (path.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out)
        {
            ZM_CORE_ERROR("Faylga yozib bo'lmadi: '{}'", path.string());
            return false;
        }
        out << content;
        return true;
    }

    bool FileSystem::WriteBinaryFile(const std::filesystem::path& path, const void* data, size_t size)
    {
        if (path.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out)
        {
            ZM_CORE_ERROR("Faylga yozib bo'lmadi: '{}'", path.string());
            return false;
        }
        out.write((const char*)data, (std::streamsize)size);
        return (bool)out;
    }

    std::vector<uint8_t> FileSystem::ReadBinaryFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in)
            return {};
        std::streamsize size = in.tellg();
        in.seekg(0, std::ios::beg);
        std::vector<uint8_t> data((size_t)size);
        in.read(reinterpret_cast<char*>(data.data()), size);
        return data;
    }

    std::filesystem::path FileSystem::GetExecutablePath()
    {
#if defined(__linux__)
        char buf[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0)
        {
            buf[len] = '\0';
            return std::filesystem::path(buf);
        }
#endif
        return std::filesystem::current_path() / "unknown";
    }

}
