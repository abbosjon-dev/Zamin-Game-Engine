#pragma once
#include <filesystem>
#include <string>

namespace Zamin {
    enum class ModuleABIStatus
    {
        Compatible,
        MissingFile,
        InvalidELF,
        MissingRecord,
        InvalidRecord,
        Incompatible,
        Unsupported,
        // The module's ABI record matches, but some of its object files were compiled against other engine headers
        // (their translation unit stamps differ): an object file that was not rebuilt after an engine header change.
        StaleObjects
    };
    struct ModuleABIReport
    {
        ModuleABIStatus Status = ModuleABIStatus::InvalidELF;
        std::string EngineFingerprint, ModuleFingerprint, Message;
        // The translation unit stamps found in the module (.zamin_abi_tu) and how many differ from the engine.
        size_t TranslationUnits = 0, StaleTranslationUnits = 0;
        std::string StaleFingerprint; // the first stamp that differs
        bool Compatible() const { return Status == ModuleABIStatus::Compatible; }
    };
    class ModuleABI
    {
    public:
        static const std::string& EngineFingerprint();
        // Linux ELF64 little-endian x86-64 only; no module code is executed.
        // Compatibility metadata is not a signature or a sandbox for native code.
        static ModuleABIReport Inspect(const std::filesystem::path& module);
    };
}
