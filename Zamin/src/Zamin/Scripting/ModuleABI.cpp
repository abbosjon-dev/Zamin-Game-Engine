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
        if (!file) return fail(ModuleABIStatus::MissingFile, "Modul fayli ochilmadi.");
        const auto length = file.tellg();
        constexpr uint64_t maxFile = 512ull * 1024 * 1024;
        if (length < static_cast<std::streamoff>(sizeof(Elf64_Ehdr)) || uint64_t(length) > maxFile)
            return fail(ModuleABIStatus::InvalidELF, "Modul hajmi ELF chegarasidan tashqarida (512 MiB).");
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
            return fail(ModuleABIStatus::InvalidELF, "Modul to'g'ri ELF fayli emas.");
        if (header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB || header.e_machine != EM_X86_64)
            return fail(ModuleABIStatus::Unsupported, "Modul Linux x86-64 little-endian ELF formatida bo'lishi kerak.");
        if (header.e_ident[EI_VERSION] != EV_CURRENT || header.e_version != EV_CURRENT || header.e_type != ET_DYN ||
            header.e_ehsize != sizeof(header) || header.e_shentsize != sizeof(Elf64_Shdr) || header.e_shnum == 0 || header.e_shnum > 4096 ||
            header.e_shstrndx == SHN_UNDEF || header.e_shstrndx >= header.e_shnum)
            return fail(ModuleABIStatus::InvalidELF, "Modul ELF section jadvali noto'g'ri yoki qo'llab-quvvatlanmaydi.");
        std::vector<Elf64_Shdr> sections(header.e_shnum);
        if (!read(header.e_shoff, sections.data(), sections.size() * sizeof(Elf64_Shdr)))
            return fail(ModuleABIStatus::InvalidELF, "ELF section jadvali fayl chegarasidan tashqarida.");
        const auto& namesSection = sections[header.e_shstrndx];
        if (namesSection.sh_type != SHT_STRTAB || namesSection.sh_size == 0 || namesSection.sh_size > 1024 * 1024)
            return fail(ModuleABIStatus::InvalidELF, "ELF section nomlari jadvali noto'g'ri.");
        std::vector<char> names(size_t(namesSection.sh_size));
        if (!read(namesSection.sh_offset, names.data(), names.size()))
            return fail(ModuleABIStatus::InvalidELF, "ELF section nomlari fayl chegarasidan tashqarida.");
        const Elf64_Shdr* found = nullptr;
        constexpr char sectionName[] = ".zamin_abi";
        for (const auto& section : sections)
        {
            if (section.sh_name > names.size() || sizeof(sectionName) > names.size() - section.sh_name) continue;
            if (std::memcmp(names.data() + section.sh_name, sectionName, sizeof(sectionName)) != 0) continue;
            if (found) return fail(ModuleABIStatus::InvalidRecord, "Modulda takrorlangan SDK ABI yozuvi bor.");
            found = &section;
        }
        if (!found) return fail(ModuleABIStatus::MissingRecord, "Modulda SDK ABI belgisi yo'q. Skriptlarni yangi Zamin SDK bilan qayta build qiling.");
        Detail::ModuleABIRecord record;
        if (found->sh_type != SHT_PROGBITS || (found->sh_flags & SHF_COMPRESSED) || found->sh_size != sizeof(record) || !read(found->sh_offset, &record, sizeof(record)))
            return fail(ModuleABIStatus::InvalidRecord, "Modulning SDK ABI yozuvi buzilgan.");
        constexpr auto expected = Detail::MakeModuleABIRecord(ZAMIN_SDK_ABI_FINGERPRINT);
        if (std::memcmp(record.Magic, expected.Magic, sizeof(record.Magic)) != 0 || record.Format != 1 || record.Fingerprint[64] != 0 ||
            !std::all_of(record.Fingerprint, record.Fingerprint + 64, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
            std::any_of(std::begin(record.Reserved), std::end(record.Reserved), [](uint8_t b) { return b != 0; }))
            return fail(ModuleABIStatus::InvalidRecord, "Modulning SDK ABI formati noto'g'ri.");
        report.ModuleFingerprint = std::string(record.Fingerprint, 64);
        if (std::memcmp(&record, &expected, sizeof(record)) != 0)
            return fail(ModuleABIStatus::Incompatible, "Modul va engine SDK/ABI mos emas. Skriptlarni hozirgi engine SDK bilan qayta build qiling.");
        report.Status = ModuleABIStatus::Compatible;
        report.Message = "Modul SDK/ABI mos.";
        return report;
#else
        (void)module;
        return fail(ModuleABIStatus::Unsupported, "SDK ABI preflight bu platformada hali mavjud emas.");
#endif
    }
}
