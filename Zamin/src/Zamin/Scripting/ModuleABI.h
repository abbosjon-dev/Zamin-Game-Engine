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
        Unsupported
    };
    struct ModuleABIReport
    {
        ModuleABIStatus Status = ModuleABIStatus::InvalidELF;
        std::string EngineFingerprint, ModuleFingerprint, Message;
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
