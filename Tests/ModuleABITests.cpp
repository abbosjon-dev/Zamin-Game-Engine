#include <Zamin/Scripting/ModuleABI.h>
#include <Zamin/Scripting/ModuleABIRecord.h>
#include <Zamin/Utils/FileSystem.h>
#include "TestFramework.h"
#include <elf.h>
#include <cstring>
#include <limits>
#include <random>
using namespace Zamin;
namespace {
    struct ABIRegistration
    {
        ABIRegistration(const char* name, std::function<void()> fn) { ZaminTest::Registry().push_back({ name, std::move(fn) }); }
    };
#define TEST(n)                     \
    void n();                       \
    ABIRegistration reg_##n(#n, n); \
    void n()
#define CHECK(v)                                                  \
    do {                                                          \
        if (!(v)) ZaminTest::CheckFailed(__FILE__, __LINE__, #v); \
    } while (false)
    struct Fixture
    {
        std::filesystem::path Path = std::filesystem::temp_directory_path() / ("zamin-abi-parser-" + std::to_string(uint64_t(std::random_device{}())) + ".so");
        std::vector<uint8_t> Bytes;
        static constexpr size_t NamesOffset = sizeof(Elf64_Ehdr) + 4 * sizeof(Elf64_Shdr), RecordOffset = NamesOffset + 64,
                                StampOffset = RecordOffset + 128, StampHeader = sizeof(Elf64_Ehdr) + 3 * sizeof(Elf64_Shdr);
        Fixture()
        {
            Bytes.resize(StampOffset + 2 * 128);
            Elf64_Ehdr header{};
            std::memcpy(header.e_ident, ELFMAG, SELFMAG);
            header.e_ident[EI_CLASS] = ELFCLASS64;
            header.e_ident[EI_DATA] = ELFDATA2LSB;
            header.e_ident[EI_VERSION] = EV_CURRENT;
            header.e_type = ET_DYN;
            header.e_machine = EM_X86_64;
            header.e_version = EV_CURRENT;
            header.e_ehsize = sizeof(header);
            header.e_shoff = sizeof(header);
            header.e_shentsize = sizeof(Elf64_Shdr);
            header.e_shnum = 4;
            header.e_shstrndx = 1;
            Put(0, header);
            Elf64_Shdr names{};
            names.sh_type = SHT_STRTAB;
            names.sh_offset = NamesOffset;
            names.sh_size = 64;
            Put(sizeof(header) + sizeof(Elf64_Shdr), names);
            constexpr char text[] = "\0.shstrtab\0.zamin_abi\0.zamin_abi_tu\0";
            std::memcpy(Bytes.data() + NamesOffset, text, sizeof(text));
            Elf64_Shdr record{};
            record.sh_name = 11;
            record.sh_type = SHT_PROGBITS;
            record.sh_offset = RecordOffset;
            record.sh_size = 128;
            Put(sizeof(header) + 2 * sizeof(Elf64_Shdr), record);
            char fingerprint[65];
            std::memcpy(fingerprint, ModuleABI::EngineFingerprint().c_str(), 65);
            Put(RecordOffset, Detail::MakeModuleABIRecord(fingerprint));
            // Two compiled files, both stamped with the engine's fingerprint.
            Elf64_Shdr stamps{};
            stamps.sh_name = 22;
            stamps.sh_type = SHT_PROGBITS;
            stamps.sh_offset = StampOffset;
            stamps.sh_size = 2 * 128;
            Put(StampHeader, stamps);
            Put(StampOffset, Detail::MakeModuleTranslationUnitStamp(fingerprint));
            Put(StampOffset + 128, Detail::MakeModuleTranslationUnitStamp(fingerprint));
        }
        ~Fixture()
        {
            std::error_code ec;
            std::filesystem::remove(Path, ec);
        }
        template<class T>
        void Put(size_t offset, const T& value) { std::memcpy(Bytes.data() + offset, &value, sizeof(value)); }
        template<class T>
        T Get(size_t offset)
        {
            T value;
            std::memcpy(&value, Bytes.data() + offset, sizeof(value));
            return value;
        }
        ModuleABIReport Inspect()
        {
            CHECK(FileSystem::WriteBinaryFile(Path, Bytes.data(), Bytes.size()));
            return ModuleABI::Inspect(Path);
        }
    };
    TEST(ModuleABIValidRecordAndFingerprintMismatch)
    {
        Fixture f;
        CHECK(f.Inspect().Compatible());
        auto r = f.Get<Detail::ModuleABIRecord>(Fixture::RecordOffset);
        r.Fingerprint[0] = r.Fingerprint[0] == 'a' ? 'b' : 'a';
        f.Put(Fixture::RecordOffset, r);
        CHECK(f.Inspect().Status == ModuleABIStatus::Incompatible);
    }
    TEST(ModuleABIRejectsLayoutAndFlagMismatch)
    {
        for (int kind = 0; kind < 3; ++kind)
        {
            Fixture f;
            auto r = f.Get<Detail::ModuleABIRecord>(Fixture::RecordOffset);
            if (kind == 0) r.StringSize++;
            else if (kind == 1) r.Flags ^= 1;
            else r.Vec3Align++;
            f.Put(Fixture::RecordOffset, r);
            CHECK(f.Inspect().Status == ModuleABIStatus::Incompatible);
        }
    }
    TEST(ModuleABIRejectsMissingOrMalformedRecord)
    {
        for (int kind = 0; kind < 5; ++kind)
        {
            Fixture f;
            auto r = f.Get<Detail::ModuleABIRecord>(Fixture::RecordOffset);
            if (kind == 0)
            {
                f.Bytes[Fixture::NamesOffset + 11] = '!';
                CHECK(f.Inspect().Status == ModuleABIStatus::MissingRecord);
                continue;
            }
            if (kind == 1) r.Format = 999;
            else if (kind == 2) r.Magic[0] = '!';
            else if (kind == 3) r.Fingerprint[64] = 'x';
            else r.Reserved[0] = 1;
            f.Put(Fixture::RecordOffset, r);
            CHECK(f.Inspect().Status == ModuleABIStatus::InvalidRecord);
        }
    }
    TEST(ModuleABIRejectsELFOverflowTruncationAndWrongArchitecture)
    {
        for (int kind = 0; kind < 6; ++kind)
        {
            Fixture f;
            auto h = f.Get<Elf64_Ehdr>(0);
            if (kind == 0) h.e_shoff = std::numeric_limits<uint64_t>::max() - 1;
            if (kind == 1) h.e_shnum = 65535;
            if (kind == 2) h.e_shstrndx = 65535;
            if (kind == 3) h.e_machine = EM_AARCH64;
            if (kind == 4) h.e_shentsize = 1;
            f.Put(0, h);
            if (kind == 5) f.Bytes.resize(10);
            CHECK(!f.Inspect().Compatible());
        }
    }
    TEST(ModuleABIRejectsMalformedSectionRanges)
    {
        for (int kind = 0; kind < 5; ++kind)
        {
            Fixture f;
            size_t offset = sizeof(Elf64_Ehdr) + (kind < 2 ? 1 : 2) * sizeof(Elf64_Shdr);
            auto s = f.Get<Elf64_Shdr>(offset);
            if (kind == 0) s.sh_size = 2 * 1024 * 1024;
            if (kind == 1 || kind == 2) s.sh_offset = UINT64_MAX;
            if (kind == 3) s.sh_size = 129;
            if (kind == 4) s.sh_flags |= SHF_COMPRESSED;
            f.Put(offset, s);
            CHECK(!f.Inspect().Compatible());
        }
    }
    TEST(ModuleABIMutatedRecordsAreBounded)
    {
        Fixture f;
        auto original = f.Bytes;
        std::mt19937 random(42);
        for (int attempt = 0; attempt < 128; ++attempt)
        {
            f.Bytes = original;
            f.Bytes[random() % f.Bytes.size()] ^= uint8_t(1 + random() % 255);
            auto report = f.Inspect();
            CHECK(report.EngineFingerprint == ModuleABI::EngineFingerprint());
        }
    }
    // A module whose ABI record matches but one of whose object files was compiled against other engine headers (it was
    // not rebuilt after a header change): refused with the count, never loaded.
    TEST(ModuleABIRefusesStaleObjectFiles)
    {
        Fixture f;
        auto report = f.Inspect();
        CHECK(report.Compatible() && report.TranslationUnits == 2 && report.StaleTranslationUnits == 0);
        auto stamp = f.Get<Detail::ModuleTranslationUnitStamp>(Fixture::StampOffset + 128);
        stamp.Fingerprint[3] = stamp.Fingerprint[3] == '0' ? '1' : '0';
        f.Put(Fixture::StampOffset + 128, stamp);
        report = f.Inspect();
        CHECK(report.Status == ModuleABIStatus::StaleObjects);
        CHECK(report.TranslationUnits == 2 && report.StaleTranslationUnits == 1);
        CHECK(report.StaleFingerprint == std::string(stamp.Fingerprint, 64));
        CHECK(report.Message.find("1 of 2 compiled files") != std::string::npos);
    }
    TEST(ModuleABIRejectsMissingOrBrokenStamps)
    {
        for (int kind = 0; kind < 5; ++kind)
        {
            Fixture f;
            auto s = f.Get<Elf64_Shdr>(Fixture::StampHeader);
            auto stamp = f.Get<Detail::ModuleTranslationUnitStamp>(Fixture::StampOffset);
            if (kind == 0) f.Bytes[Fixture::NamesOffset + 22] = '!';
            if (kind == 1) s.sh_size = 200;
            if (kind == 2) s.sh_offset = UINT64_MAX - 8;
            if (kind == 3) stamp.Magic[0] = '!';
            if (kind == 4) stamp.Reserved[5] = 1;
            f.Put(Fixture::StampHeader, s);
            f.Put(Fixture::StampOffset, stamp);
            auto report = f.Inspect();
            CHECK(report.Status == (kind == 0 ? ModuleABIStatus::Incompatible : ModuleABIStatus::InvalidRecord));
        }
    }
}
