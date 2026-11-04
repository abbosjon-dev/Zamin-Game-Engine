#pragma once

#include "Zamin/Core/UUID.h"
#include "Zamin/Renderer/Texture.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Renderer/Material.h"
#include "Zamin/Renderer/Font.h"
#include "Zamin/Scene/SceneCamera.h"
#include "Zamin/Scripting/ScriptField.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>

#include <string>
#include <unordered_map>

namespace Zamin {

    class ScriptableEntity;

    struct IDComponent
    {
        UUID ID;
        IDComponent() = default;
        IDComponent(UUID id) : ID(id) {}
    };

    struct TagComponent
    {
        std::string Tag;
        TagComponent() = default;
        TagComponent(const std::string& tag) : Tag(tag) {}
    };

    struct TransformComponent
    {
        glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f }; // euler angles, radians
        glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

        TransformComponent() = default;
        TransformComponent(const glm::vec3& translation) : Translation(translation) {}

        glm::mat4 GetTransform() const
        {
            glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));
            return glm::translate(glm::mat4(1.0f), Translation) * rotation * glm::scale(glm::mat4(1.0f), Scale);
        }

        glm::vec3 GetForward() const { return glm::rotate(glm::quat(Rotation), glm::vec3(0, 0, -1)); }
        glm::vec3 GetRight() const { return glm::rotate(glm::quat(Rotation), glm::vec3(1, 0, 0)); }
        glm::vec3 GetUp() const { return glm::rotate(glm::quat(Rotation), glm::vec3(0, 1, 0)); }
    };

    // ------------------------------------------------------------------ rendering
    struct SpriteRendererComponent
    {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        Ref<Texture2D> Texture;
        std::string TexturePath; // relative to project assets
        float TilingFactor = 1.0f;
        int SortingOrder = 0;

        SpriteRendererComponent() = default;
        SpriteRendererComponent(const glm::vec4& color) : Color(color) {}
    };

    struct CircleRendererComponent
    {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float Thickness = 1.0f;
        float Fade = 0.005f;
        int SortingOrder = 0;
    };

    struct TextComponent
    {
        std::string Text = "Matn";
        glm::vec4 Color{ 1.0f };
        float Kerning = 0.0f;
        float LineSpacing = 0.0f;
        bool Centered = false;
        std::string FontPath; // empty = default engine font
        Ref<Font> FontAsset;
    };

    struct MeshRendererComponent
    {
        PrimitiveMesh Type = PrimitiveMesh::Cube;
        std::string ModelPath;    // .obj (when Type == Model)
        Ref<Mesh> MeshAsset;      // runtime cache
        Material Mat;
        std::string AlbedoPath;   // texture path relative to assets
        bool CastShadows = true;
    };

    struct CameraComponent
    {
        SceneCamera Camera;
        bool Primary = true;
        bool FixedAspectRatio = false;
        glm::vec4 ClearColor{ 0.08f, 0.09f, 0.11f, 1.0f };
    };

    struct DirectionalLightComponent
    {
        glm::vec3 Color{ 1.0f, 0.96f, 0.9f };
        float Intensity = 3.0f;
        bool CastShadows = true;
    };

    struct PointLightComponent
    {
        glm::vec3 Color{ 1.0f };
        float Intensity = 5.0f;
        float Radius = 10.0f;
    };

    // ------------------------------------------------------------------ physics 2D
    struct Rigidbody2DComponent
    {
        enum class BodyType { Static = 0, Dynamic, Kinematic };
        BodyType Type = BodyType::Dynamic;
        bool FixedRotation = false;
        float GravityScale = 1.0f;
        float LinearDamping = 0.0f;
        float AngularDamping = 0.01f;
        bool Bullet = false;

        void* RuntimeBody = nullptr; // b2Body*
    };

    struct BoxCollider2DComponent
    {
        glm::vec2 Offset = { 0.0f, 0.0f };
        glm::vec2 Size = { 0.5f, 0.5f }; // half extents (multiplied by transform scale)

        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        float RestitutionThreshold = 0.5f;
        bool IsSensor = false;

        void* RuntimeFixture = nullptr;
    };

    struct CircleCollider2DComponent
    {
        glm::vec2 Offset = { 0.0f, 0.0f };
        float Radius = 0.5f;

        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        float RestitutionThreshold = 0.5f;
        bool IsSensor = false;

        void* RuntimeFixture = nullptr;
    };

    // ------------------------------------------------------------------ audio
    struct AudioSourceComponent
    {
        std::string Path;     // relative to assets (.wav / .mp3 / .flac / .ogg)
        float Volume = 1.0f;
        float Pitch = 1.0f;
        bool Loop = false;
        bool PlayOnStart = true;

        uint32_t RuntimeHandle = 0;
    };

    // ------------------------------------------------------------------ scripting (C++)
    struct NativeScriptComponent
    {
        std::string ClassName;  // registered with ZM_REGISTER_SCRIPT
        std::unordered_map<std::string, ScriptFieldValue> Fields; // inspector-edited values
        ScriptableEntity* Instance = nullptr;

        NativeScriptComponent() = default;
        NativeScriptComponent(const NativeScriptComponent& other)
            : ClassName(other.ClassName), Fields(other.Fields), Instance(nullptr) {}
        NativeScriptComponent& operator=(const NativeScriptComponent& other)
        {
            ClassName = other.ClassName;
            Fields = other.Fields;
            Instance = nullptr;
            return *this;
        }
    };

    template<typename... Component>
    struct ComponentGroup {};

    // Every component that is copied/serialized with an entity (IDComponent & TagComponent are handled separately).
    using AllComponents = ComponentGroup<TransformComponent, SpriteRendererComponent, CircleRendererComponent,
        TextComponent, MeshRendererComponent, CameraComponent, DirectionalLightComponent, PointLightComponent,
        Rigidbody2DComponent, BoxCollider2DComponent, CircleCollider2DComponent, AudioSourceComponent,
        NativeScriptComponent>;

}
