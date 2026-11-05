#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    class FileSystem
    {
    public:
        static std::string ReadTextFile(const std::filesystem::path& path);
        static bool WriteTextFile(const std::filesystem::path& path, const std::string& content);
        static std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& path);
        // Writes the bytes (the parent folders are made). False when the file cannot be written.
        static bool WriteBinaryFile(const std::filesystem::path& path, const void* data, size_t size);
        static std::filesystem::path GetExecutablePath();
        static std::filesystem::path GetExecutableDirectory() { return GetExecutablePath().parent_path(); }
    };

}
