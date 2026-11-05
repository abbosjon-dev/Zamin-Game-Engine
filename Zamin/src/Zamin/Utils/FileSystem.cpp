#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Core/Log.h"

#include <fstream>
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
