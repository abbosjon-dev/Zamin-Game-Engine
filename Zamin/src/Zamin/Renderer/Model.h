#pragma once

#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Material.h"
#include "Zamin/Renderer/Animation.h"

#include <filesystem>
#include <vector>

namespace Zamin {

    // A model asset made of several meshes, each with its own material (glTF / OBJ).
    class Model
    {
    public:
        struct SubMesh
        {
            Ref<Mesh> MeshRef;
            Material Mat;
            std::string Name;
        };

        const std::vector<SubMesh>& GetSubMeshes() const { return m_SubMeshes; }
        const AABB& GetBounds() const { return m_Bounds; }
        const std::filesystem::path& GetPath() const { return m_Path; }

        const Skeleton& GetSkeleton() const { return m_Skeleton; }
        const std::vector<AnimationClip>& GetAnimations() const { return m_Animations; }
        bool IsSkinned() const { return !m_Skeleton.Empty(); }
        int FindAnimation(const std::string& name) const
        {
            for (size_t i = 0; i < m_Animations.size(); i++)
                if (m_Animations[i].Name == name) return (int)i;
            return -1;
        }

        // .gltf / .glb (meshes, node transforms, PBR materials + textures) or .obj
        static Ref<Model> Load(const std::filesystem::path& path);
        static void ClearCache();

    private:
        std::vector<SubMesh> m_SubMeshes;
        AABB m_Bounds;
        std::filesystem::path m_Path;
        Skeleton m_Skeleton;
        std::vector<AnimationClip> m_Animations;
        friend class ModelLoader;
    };

}
