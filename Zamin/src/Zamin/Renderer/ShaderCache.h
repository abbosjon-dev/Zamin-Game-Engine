#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // Linked shader programs on disk (GL_ARB_get_program_binary, core in OpenGL 4.1): a program
    // the driver linked once is given back to it as its binary the next time instead of being
    // compiled and linked again - most of the engine's startup shader work (task 488).
    //
    // A file is keyed by the program's name, both sources and the driver (vendor, renderer,
    // version): a changed shader or another GPU / driver update never reads an old binary. Files are
    // written atomically; a broken, foreign or refused one (the driver may reject a binary after an
    // update) is deleted and the program compiled from source as without the cache. A driver with no
    // binary formats (GL_NUM_PROGRAM_BINARY_FORMATS 0) simply compiles every time.
    namespace ShaderCache {

        // "": off (always compiled). Default: <user cache>/zamin/ShaderBinaries
        // (ZAMIN_SHADER_CACHE=0 switches it off).
        void SetDirectory(const std::filesystem::path& directory);
        const std::filesystem::path& GetDirectory();

        // What the driver offers (OpenGLShader asks it once per process): 0 - no binaries at all.
        void SetBinaryFormatCount(int count);
        int GetBinaryFormatCount();
        // The driver's identity in the key ("vendor|renderer|version").
        void SetDriver(std::string_view driver);
        const std::string& GetDriver();
        // A directory and at least one binary format: programs are read from / written to disk.
        bool IsAvailable();

        uint64_t MakeKey(std::string_view name, std::string_view vertexSource, std::string_view fragmentSource, std::string_view driver);

        // The binary stored for `key` and its driver format; false (nothing changed) when there is
        // none or the file is not a whole one of this key.
        bool Load(uint64_t key, uint32_t& format, std::vector<uint8_t>& binary);
        bool Store(uint64_t key, uint32_t format, const std::vector<uint8_t>& binary);
        // The driver refused the stored binary: its file goes (the next run stores a fresh one).
        void Remove(uint64_t key);
        std::filesystem::path GetFile(uint64_t key);

        // At most this many files are kept (the oldest go first when a new one is stored).
        constexpr size_t MaxFiles = 512;

        struct Stats
        {
            uint32_t Hits = 0;      // programs made from a stored binary
            uint32_t Misses = 0;    // compiled from source (nothing stored, or the cache unavailable)
            uint32_t Stores = 0;    // binaries written
            uint32_t Rejected = 0;  // stored binaries the driver refused (compiled then)
            double LoadMilliseconds = 0.0;    // making the programs from binaries (reading the file included)
            double CompileMilliseconds = 0.0; // compiling + linking the others (fetching their binary included)
        };
        const Stats& GetStats();
        void ResetStats();
        // The program-making functions add their time and outcome here.
        void RecordHit(double milliseconds);
        void RecordMiss(double milliseconds);
        void RecordRejected();
    }

}
