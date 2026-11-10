#include "Zamin/Renderer/ShaderCache.h"
#include "Zamin/Utils/FileSystem.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <mutex>

namespace Zamin {

    namespace {
        struct ShaderCacheState
        {
            bool Initialized = false;
            std::filesystem::path Directory;
            int BinaryFormats = 0;
            std::string Driver;
            ShaderCache::Stats Stats;
        };

        std::mutex s_Mutex;

        ShaderCacheState& State()
        {
            static ShaderCacheState s_State;
            if (!s_State.Initialized)
            {
                s_State.Initialized = true;
                const char* env = std::getenv("ZAMIN_SHADER_CACHE");
                const std::filesystem::path cache = FileSystem::GetUserCacheDirectory();
                if (!(env && std::strcmp(env, "0") == 0) && !cache.empty())
                    s_State.Directory = cache / "zamin" / "ShaderBinaries";
            }
            return s_State;
        }

        constexpr uint32_t s_Magic = 0x4248535Au; // "ZSHB"
        constexpr uint32_t s_FileVersion = 1;     // bump when this file changes
        constexpr const char* s_Extension = ".zshb";

        uint64_t Fnv1a(const void* data, size_t size, uint64_t hash = 1469598103934665603ull)
        {
            const uint8_t* bytes = (const uint8_t*)data;
            for (size_t i = 0; i < size; i++)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
            return hash;
        }

        // A part of the key with its length first: ("ab", "c") and ("a", "bc") differ.
        uint64_t HashPart(std::string_view text, uint64_t hash)
        {
            const uint64_t size = text.size();
            hash = Fnv1a(&size, sizeof(size), hash);
            return Fnv1a(text.data(), text.size(), hash);
        }

        template<typename T>
        void Put(std::vector<uint8_t>& out, const T& value)
        {
            const size_t at = out.size();
            out.resize(at + sizeof(T));
            std::memcpy(out.data() + at, &value, sizeof(T));
        }

        template<typename T>
        bool Take(const std::vector<uint8_t>& in, size_t& at, T& value)
        {
            if (at + sizeof(T) > in.size())
                return false;
            std::memcpy(&value, in.data() + at, sizeof(T));
            at += sizeof(T);
            return true;
        }

        // The oldest files go until at most MaxFiles are left (every shader edit leaves one behind).
        void Prune(const std::filesystem::path& directory)
        {
            std::error_code ec;
            std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
            for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
                if (entry.is_regular_file(ec) && entry.path().extension() == s_Extension)
                    files.emplace_back(entry.last_write_time(ec), entry.path());
            if (files.size() <= ShaderCache::MaxFiles)
                return;
            std::sort(files.begin(), files.end());
            for (size_t i = 0; i + ShaderCache::MaxFiles < files.size(); i++)
                std::filesystem::remove(files[i].second, ec);
        }
    }

    namespace ShaderCache {

        void SetDirectory(const std::filesystem::path& directory)
        {
            std::lock_guard lock(s_Mutex);
            State().Directory = directory;
        }

        const std::filesystem::path& GetDirectory() { return State().Directory; }

        void SetBinaryFormatCount(int count) { State().BinaryFormats = std::max(0, count); }
        int GetBinaryFormatCount() { return State().BinaryFormats; }
        void SetDriver(std::string_view driver) { State().Driver = driver; }
        const std::string& GetDriver() { return State().Driver; }

        bool IsAvailable()
        {
            const ShaderCacheState& state = State();
            return !state.Directory.empty() && state.BinaryFormats > 0;
        }

        uint64_t MakeKey(std::string_view name, std::string_view vertexSource, std::string_view fragmentSource, std::string_view driver)
        {
            uint64_t hash = Fnv1a(&s_FileVersion, sizeof(s_FileVersion));
            hash = HashPart(name, hash);
            hash = HashPart(vertexSource, hash);
            hash = HashPart(fragmentSource, hash);
            return HashPart(driver, hash);
        }

        std::filesystem::path GetFile(uint64_t key)
        {
            return GetDirectory() / std::format("{:016x}{}", key, s_Extension);
        }

        bool Load(uint64_t key, uint32_t& format, std::vector<uint8_t>& binary)
        {
            if (GetDirectory().empty())
                return false;
            const std::vector<uint8_t> file = FileSystem::ReadBinaryFile(GetFile(key));
            if (file.empty())
                return false;
            size_t at = 0;
            uint32_t magic = 0, version = 0, storedFormat = 0, size = 0;
            uint64_t storedKey = 0, checksum = 0;
            if (!Take(file, at, magic) || magic != s_Magic || !Take(file, at, version) || version != s_FileVersion ||
                !Take(file, at, storedKey) || storedKey != key || !Take(file, at, storedFormat) || !Take(file, at, size) ||
                !Take(file, at, checksum) || size == 0 || at + size != file.size() || Fnv1a(file.data() + at, size) != checksum)
                return false;
            format = storedFormat;
            binary.assign(file.begin() + (std::ptrdiff_t)at, file.end());
            return true;
        }

        bool Store(uint64_t key, uint32_t format, const std::vector<uint8_t>& binary)
        {
            const std::filesystem::path directory = GetDirectory();
            if (directory.empty() || binary.empty() || binary.size() > 0x7FFFFFFFu)
                return false;
            std::vector<uint8_t> out;
            out.reserve(binary.size() + 40);
            Put(out, s_Magic);
            Put(out, s_FileVersion);
            Put(out, key);
            Put(out, format);
            Put(out, (uint32_t)binary.size());
            Put(out, Fnv1a(binary.data(), binary.size()));
            out.insert(out.end(), binary.begin(), binary.end());

            std::error_code ec;
            std::filesystem::create_directories(directory, ec);
            const std::filesystem::path file = GetFile(key);
            // Written next to it, then renamed: a reader never sees half a file.
            const std::filesystem::path temp =
                file.string() + std::format(".{}.tmp", (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count());
            {
                std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
                if (!stream)
                    return false;
                stream.write((const char*)out.data(), (std::streamsize)out.size());
                if (!stream)
                {
                    stream.close();
                    std::filesystem::remove(temp, ec);
                    return false;
                }
            }
            std::filesystem::rename(temp, file, ec);
            if (ec)
            {
                std::filesystem::remove(temp, ec);
                return false;
            }
            {
                std::lock_guard lock(s_Mutex);
                State().Stats.Stores++;
            }
            Prune(directory);
            return true;
        }

        void Remove(uint64_t key)
        {
            if (GetDirectory().empty())
                return;
            std::error_code ec;
            std::filesystem::remove(GetFile(key), ec);
        }

        const Stats& GetStats() { return State().Stats; }

        void ResetStats()
        {
            std::lock_guard lock(s_Mutex);
            State().Stats = {};
        }

        void RecordHit(double milliseconds)
        {
            std::lock_guard lock(s_Mutex);
            State().Stats.Hits++;
            State().Stats.LoadMilliseconds += milliseconds;
        }

        void RecordMiss(double milliseconds)
        {
            std::lock_guard lock(s_Mutex);
            State().Stats.Misses++;
            State().Stats.CompileMilliseconds += milliseconds;
        }

        void RecordRejected()
        {
            std::lock_guard lock(s_Mutex);
            State().Stats.Rejected++;
        }
    }

}
