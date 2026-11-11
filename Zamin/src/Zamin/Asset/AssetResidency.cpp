#include "Zamin/Asset/AssetResidency.h"
#include "Zamin/Renderer/Model.h"
#include "Zamin/Renderer/MaterialAsset.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Texture.h"
namespace Zamin {
    AssetResidency::Snapshot AssetResidency::Inspect()
    {
        Snapshot out;
        out.Caches = { Model::GetResidencyStats(), MaterialLibrary::GetResidencyStats(), MeshLibrary::GetResidencyStats(), TextureLibrary::GetResidencyStats() };
        out.Process = MemoryStats::GetProcessMemory();
        for (auto category : { MemoryCategory::Textures, MemoryCategory::Meshes, MemoryCategory::RenderTargets })
        {
            const auto bytes = MemoryStats::Get(category).Bytes;
            out.RequestedGPUBytes += std::min(bytes, UINT64_MAX - out.RequestedGPUBytes);
        }
        for (const auto& cache : out.Caches)
        {
            out.Entries += cache.Entries;
            out.Pinned += cache.Pinned;
            out.Loading += cache.Loading;
        }
        return out;
    }
    size_t AssetResidency::Trim(size_t target, size_t maxEvictions)
    {
        size_t count = Model::TrimUnused(target, maxEvictions);
        count += MaterialLibrary::TrimUnused(target, maxEvictions - count);
        count += MeshLibrary::TrimUnused(target, maxEvictions - count);
        count += TextureLibrary::TrimUnused(target, maxEvictions - count);
        return count;
    }
    const char* AssetResidency::Name(size_t kind)
    {
        static constexpr const char* names[] = { "Models", "Materials", "Meshes", "Textures" };
        return kind < Count ? names[kind] : "Unknown";
    }
}
