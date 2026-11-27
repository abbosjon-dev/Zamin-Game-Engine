#pragma once
#include "VisualGraph.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

/// @file
/// @brief Cooked Visual Script graphs: the export compiles every `.zgraph` to a compact, validated binary program
/// (written over the exported `.zgraph`, like the texture cook's ZTEX files) that the Player runs without parsing
/// JSON, validating, resolving the parent chain or expanding macros again.
///
/// Layout (little endian): "ZGBC", u32 format version, u64 engine catalogue hash (the node catalogue the program
/// was compiled against; another engine refuses it), u64 FNV-1a hash of the payload, u32 payload size, payload.
/// The payload is a string table followed by the Program (VisualGraph.h): the variables, enums, structs, typed
/// function signatures, native method signatures, the shared pin descriptions, the nodes (kind and literals,
/// a description index), links as string-table indices, the macro and parent origins of nodes (error messages)
/// and the parent event table. Values use a tagged encoding (null, bool, integers, doubles, strings, arrays,
/// objects). Positions and breakpoints are not kept.
namespace Zamin::Visual::Bytecode {
    constexpr char Magic[4] = { 'Z', 'G', 'B', 'C' };
    constexpr uint32_t FormatVersion = 1;
    constexpr size_t HeaderSize = 4 + 4 + 8 + 8 + 4;
    constexpr size_t MaxBytes = 8 * 1024 * 1024;

    // The node catalogue's hash (names, categories, pins and defaults of every node kind).
    uint64_t CatalogHash();
    bool IsCooked(const uint8_t* data, size_t size);
    bool IsCookedFile(const std::filesystem::path& path);

    struct CompileResult
    {
        bool Ok = false;
        std::vector<uint8_t> Bytes;
        // Errors: what Machine::Load rejects (the cook rejects the graph then); warnings: Graph::Warnings.
        std::vector<Diagnostic> Errors, Warnings;
        std::string Error; // the first error as Machine::Load words it
        size_t Nodes = 0, Links = 0, Definitions = 0;
    };
    // Validates and compiles a loaded graph (its parent chain loaded: Graph::Load).
    CompileResult Compile(const Graph& graph);
    // Decodes and checks a cooked program (header, version, catalogue, hash, every index and link); false with a
    // reason for a file that is not one, of another version or engine, or damaged.
    bool Read(const uint8_t* data, size_t size, Program& out, std::string& error);
    bool ReadFile(const std::filesystem::path& path, Program& out, std::string& error);
}
