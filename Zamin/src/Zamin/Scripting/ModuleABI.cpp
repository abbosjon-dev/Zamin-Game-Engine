#include "Zamin/Scripting/ModuleABI.h"
#include "Zamin/Scripting/ModuleABIRecord.h"
#include "ZaminModuleABI.inc"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
#ifdef __linux__
    #include <elf.h>
#endif

namespace Zamin {
    const std::string& ModuleABI::EngineFingerprint()
    {
        static const std::string fingerprint = ZAMIN_SDK_ABI_FINGERPRINT;
        return fingerprint;
    }
    ModuleABIReport ModuleABI::Inspect(const std::filesystem::path& module)
    {
        ModuleABIReport report;
        report.EngineFingerprint = EngineFingerprint();
        auto fail = [&](ModuleABIStatus status, const char* message) {report.Status=status;report.Message=message;return report; };
#ifdef __linux__
        std::ifstream file(module, std::ios::binary | std::ios::ate);
        if (!file) return fail(ModuleABIStatus::MissingFile, "Could not open the module file.");
        const auto length = file.tellg();
        constexpr uint64_t maxFile = 512ull * 1024 * 1024;
        if (length < static_cast<std::streamoff>(sizeof(Elf64_Ehdr)) || uint64_t(length) > maxFile)
            return fail(ModuleABIStatus::InvalidELF, "The module size is outside the ELF limit (512 MiB).");
        const uint64_t size = uint64_t(length);
        auto range = [&](uint64_t offset, uint64_t bytes) { return offset <= size && bytes <= size - offset; };
        auto read = [&](uint64_t offset, void* destination, size_t bytes) {
            if (!range(offset, bytes)) return false;
            file.clear();
            file.seekg(static_cast<std::streamoff>(offset));
            return bool(file.read(static_cast<char*>(destination), static_cast<std::streamsize>(bytes)));
        };
        Elf64_Ehdr header{};
        if (!read(0, &header, sizeof(header)) || std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0)
            return fail(ModuleABIStatus::InvalidELF, "The module is not a valid ELF file.");
        if (header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB || header.e_machine != EM_X86_64)
            return fail(ModuleABIStatus::Unsupported, "The module must be in the Linux x86-64 little-endian ELF format.");
        if (header.e_ident[EI_VERSION] != EV_CURRENT || header.e_version != EV_CURRENT || header.e_type != ET_DYN ||
            header.e_ehsize != sizeof(header) || header.e_shentsize != sizeof(Elf64_Shdr) || header.e_shnum == 0 || header.e_shnum > 4096 ||
            header.e_shstrndx == SHN_UNDEF || header.e_shstrndx >= header.e_shnum)
            return fail(ModuleABIStatus::InvalidELF, "The module's ELF section table is invalid or not supported.");
        std::vector<Elf64_Shdr> sections(header.e_shnum);
        if (!read(header.e_shoff, sections.data(), sections.size() * sizeof(Elf64_Shdr)))
            return fail(ModuleABIStatus::InvalidELF, "The ELF section table is outside the file.");
        const auto& namesSection = sections[header.e_shstrndx];
        if (namesSection.sh_type != SHT_STRTAB || namesSection.sh_size == 0 || namesSection.sh_size > 1024 * 1024)
            return fail(ModuleABIStatus::InvalidELF, "The ELF section name table is invalid.");
        std::vector<char> names(size_t(namesSection.sh_size));
        if (!read(namesSection.sh_offset, names.data(), names.size()))
            return fail(ModuleABIStatus::InvalidELF, "The ELF section names are outside the file.");
        const Elf64_Shdr* found = nullptr;
        constexpr char sectionName[] = ".zamin_abi";
        for (const auto& section : sections)
        {
            if (section.sh_name > names.size() || sizeof(sectionName) > names.size() - section.sh_name) continue;
            if (std::memcmp(names.data() + section.sh_name, sectionName, sizeof(sectionName)) != 0) continue;
            if (found) return fail(ModuleABIStatus::InvalidRecord, "The module has a repeated SDK ABI record.");
            found = &section;
        }
        if (!found) return fail(ModuleABIStatus::MissingRecord, "The module has no SDK ABI mark. Rebuild the scripts with the new Zamin SDK.");
        Detail::ModuleABIRecord record;
        if (found->sh_type != SHT_PROGBITS || (found->sh_flags & SHF_COMPRESSED) || found->sh_size != sizeof(record) || !read(found->sh_offset, &record, sizeof(record)))
            return fail(ModuleABIStatus::InvalidRecord, "The module's SDK ABI record is broken.");
        constexpr auto expected = Detail::MakeModuleABIRecord(ZAMIN_SDK_ABI_FINGERPRINT);
        if (std::memcmp(record.Magic, expected.Magic, sizeof(record.Magic)) != 0 || record.Format != 1 || record.Fingerprint[64] != 0 ||
            !std::all_of(record.Fingerprint, record.Fingerprint + 64, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
            std::any_of(std::begin(record.Reserved), std::end(record.Reserved), [](uint8_t b) { return b != 0; }))
            return fail(ModuleABIStatus::InvalidRecord, "The module's SDK ABI format is invalid.");
        report.ModuleFingerprint = std::string(record.Fingerprint, 64);
        if (std::memcmp(&record, &expected, sizeof(record)) != 0)
            return fail(ModuleABIStatus::Incompatible, "The module and the engine SDK/ABI do not match. Rebuild the scripts with the current engine SDK.");
        // Every compiled file of the module stamps the SDK fingerprint it was compiled against (.zamin_abi_tu).
        const Elf64_Shdr* stamps = nullptr;
        constexpr char stampSectionName[] = ".zamin_abi_tu";
        for (const auto& section : sections)
        {
            if (section.sh_name > names.size() || sizeof(stampSectionName) > names.size() - section.sh_name) continue;
            if (std::memcmp(names.data() + section.sh_name, stampSectionName, sizeof(stampSectionName)) != 0) continue;
            if (stamps) return fail(ModuleABIStatus::InvalidRecord, "The module has a repeated translation unit stamp section.");
            stamps = &section;
        }
        if (!stamps)
            return fail(ModuleABIStatus::Incompatible, "The module's object files carry no SDK stamps (an older SDK built them). Rebuild the scripts with the current engine SDK.");
        constexpr uint64_t stampSize = sizeof(Detail::ModuleTranslationUnitStamp), maxStamps = 65536;
        if (stamps->sh_type != SHT_PROGBITS || (stamps->sh_flags & SHF_COMPRESSED) || stamps->sh_size == 0 ||
            stamps->sh_size % stampSize != 0 || stamps->sh_size / stampSize > maxStamps || !range(stamps->sh_offset, stamps->sh_size))
            return fail(ModuleABIStatus::InvalidRecord, "The module's translation unit stamps are broken.");
        std::vector<Detail::ModuleTranslationUnitStamp> entries(size_t(stamps->sh_size / stampSize));
        if (!read(stamps->sh_offset, entries.data(), size_t(stamps->sh_size)))
            return fail(ModuleABIStatus::InvalidRecord, "The module's translation unit stamps are outside the file.");
        for (const auto& entry : entries)
        {
            if (std::memcmp(entry.Magic, Detail::ModuleTranslationUnitStampMagic, sizeof(entry.Magic)) != 0 || entry.Fingerprint[64] != 0 ||
                !std::all_of(entry.Fingerprint, entry.Fingerprint + 64, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
                std::any_of(std::begin(entry.Reserved), std::end(entry.Reserved), [](uint8_t b) { return b != 0; }))
                return fail(ModuleABIStatus::InvalidRecord, "The module's translation unit stamp format is invalid.");
            ++report.TranslationUnits;
            if (std::memcmp(entry.Fingerprint, expected.Fingerprint, 64) != 0)
            {
                if (report.StaleFingerprint.empty()) report.StaleFingerprint = std::string(entry.Fingerprint, 64);
                ++report.StaleTranslationUnits;
            }
        }
        if (report.StaleTranslationUnits)
        {
            report.Status = ModuleABIStatus::StaleObjects;
            report.Message = std::to_string(report.StaleTranslationUnits) + " of " + std::to_string(report.TranslationUnits) +
                " compiled files of the module were built against other engine headers (SDK " + report.StaleFingerprint.substr(0, 12) +
                ", the engine is " + report.EngineFingerprint.substr(0, 12) +
                "): stale object files. Delete the module's object files (its build folder) and compile the scripts again.";
            return report;
        }
        report.Status = ModuleABIStatus::Compatible;
        report.Message = "The module SDK/ABI matches.";
        return report;
#else
        (void)module;
        return fail(ModuleABIStatus::Unsupported, "The SDK ABI preflight is not available on this platform yet.");
#endif
    }
}
