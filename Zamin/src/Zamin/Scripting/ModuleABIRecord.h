#pragma once
// Fixed-width, pointer-free metadata emitted into every SDK-built native module.
// The loader reads it from disk before dlopen can run a module's constructors.
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace Zamin::Detail {
    struct ModuleABIRecord
    {
        char Magic[16]{};
        uint32_t Format = 1, PointerSize = sizeof(void*);
        uint32_t StringSize = sizeof(std::string), StringAlign = alignof(std::string);
        uint32_t VectorSize = sizeof(std::vector<int>), VectorAlign = alignof(std::vector<int>);
        uint32_t Vec3Size = sizeof(glm::vec3), Vec3Align = alignof(glm::vec3);
        uint32_t Mat4Size = sizeof(glm::mat4), Mat4Align = alignof(glm::mat4);
        uint32_t Flags = 0;
        char Fingerprint[65]{};
        uint8_t Reserved[3]{};
    };
    static_assert(sizeof(ModuleABIRecord) == 128 && alignof(ModuleABIRecord) == 4);
    constexpr ModuleABIRecord MakeModuleABIRecord(const char (&fingerprint)[65])
    {
        ModuleABIRecord record;
        constexpr char magic[] = "ZAMIN-SDK-ABI-1";
        for (size_t i = 0; i < sizeof(magic); ++i) record.Magic[i] = magic[i];
        for (size_t i = 0; i < 65; ++i) record.Fingerprint[i] = fingerprint[i];
#ifdef _GLIBCXX_DEBUG
        record.Flags |= 1;
#endif
#if defined(_GLIBCXX_USE_CXX11_ABI) && _GLIBCXX_USE_CXX11_ABI
        record.Flags |= 2;
#endif
#ifdef GLM_FORCE_ALIGNED_GENTYPES
        record.Flags |= 4;
#endif
#ifdef GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
        record.Flags |= 8;
#endif
#ifdef GLM_FORCE_INTRINSICS
        record.Flags |= 16;
#endif
        return record;
    }
}
