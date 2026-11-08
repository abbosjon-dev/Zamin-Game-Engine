#include "Zamin/Scene/EntityClipboard.h"

#include "Zamin/Asset/AssetRegistry.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Math/Math.h"
#include "Zamin/Project/Project.h"
#include "Zamin/Scene/Components.h"
#include "Zamin/Scene/Prefab.h"
#include "Zamin/Scene/PrefabDatabase.h"
#include "Zamin/Scene/Scene.h"
#include "Zamin/Scene/SceneSerializerJson.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace Zamin {

    using json = nlohmann::json;

    namespace {
        void CollectSubtree(Scene& scene, Entity root, std::vector<Entity>& out, std::unordered_set<uint64_t>& seen)
        {
            if (!seen.insert((uint64_t)root.GetUUID()).second)
                return;
            out.push_back(root);
            for (UUID child : root.GetComponent<RelationshipComponent>().Children)
                if (Entity c = scene.GetEntityByUUID(child))
                    CollectSubtree(scene, c, out, seen);
        }

        json MatrixToJson(const glm::mat4& m)
        {
            json out = json::array();
            for (int c = 0; c < 4; c++)
                for (int r = 0; r < 4; r++)
                    out.push_back(m[c][r]);
            return out;
        }

        bool MatrixFromJson(const json& j, glm::mat4& out)
        {
            if (!j.is_array() || j.size() != 16)
                return false;
            for (int i = 0; i < 16; i++)
            {
                if (!j[i].is_number())
                    return false;
                const float v = j[i].get<float>();
                if (!std::isfinite(v))
                    return false;
                out[i / 4][i % 4] = v;
            }
            return true;
        }

        // 0.99999994 -> 1 (a world transform taken apart again): no noise in the pasted values.
        float Clean(float v)
        {
            const float r = std::round(v * 10000.0f) / 10000.0f;
            return std::abs(v - r) < 1e-5f ? r : v;
        }

        std::string PrefabHandle(const std::string& path)
        {
            if (!Project::GetActive())
                return {};
            AssetRegistry* registry = Project::GetAssetRegistry();
            const AssetMetadata* meta = registry ? registry->Find(std::filesystem::path(path)) : nullptr;
            return meta && meta->HasMetaFile ? meta->Handle.ToString() : std::string();
        }

        // The Origin's prefab: its path, or where its UUID is now (moved since it was copied).
        std::string ResolveOrigin(const json& origin, PrefabDatabase& db)
        {
            if (!origin.is_object())
                return {};
            std::string path = PrefabDatabase::NormalizePath(origin.value("Prefab", std::string()));
            if (const PrefabDatabase::Template* t = path.empty() ? nullptr : db.Get(path); t && t->Readable)
                return path;
            if (Project::GetActive())
                if (AssetRegistry* registry = Project::GetAssetRegistry())
                    if (auto handle = AssetHandle::FromString(origin.value("PrefabUUID", std::string())))
                        if (std::string moved = registry->GetPath(*handle); !moved.empty())
                            if (const PrefabDatabase::Template* t = db.Get(moved); t && t->Readable)
                                return PrefabDatabase::NormalizePath(moved);
            return {};
        }

        json ParseObject(const std::string& text)
        {
            json root = json::parse(text, nullptr, false);
            return root.is_object() ? root : json();
        }
    }

    std::string EntityClipboard::Copy(Scene& scene, const std::vector<Entity>& roots)
    {
        std::vector<Entity> entities;
        std::unordered_set<uint64_t> seen;
        for (Entity root : roots)
            if (root)
                CollectSubtree(scene, root, entities, seen);
        if (entities.empty())
            return {};
        SceneSerializer::EntitySetWriteOptions options; // everything the editor knows (Kind: Clipboard)
        json doc = SceneSerializerJson::EntitySetToJson(scene, entities, options);
        // Roots below a parent: their place in the world (a paste somewhere else keeps it).
        json world = json::object();
        for (Entity e : entities)
        {
            const UUID parent = e.GetComponent<RelationshipComponent>().Parent;
            if ((uint64_t)parent && !seen.contains((uint64_t)parent) && scene.GetEntityByUUID(parent))
                world[std::to_string((uint64_t)e.GetUUID())] = { { "Parent", (uint64_t)parent }, { "Matrix", MatrixToJson(scene.GetWorldTransform(e)) } };
        }
        if (!world.empty())
            doc["World"] = std::move(world);
        return doc.dump(2);
    }

    std::string EntityClipboard::CopyPrefab(PrefabDatabase& db, std::string_view relPath, std::string* error)
    {
        const std::string path = PrefabDatabase::NormalizePath(relPath);
        const PrefabDatabase::Template* t = path.empty() ? nullptr : db.Get(path);
        if (!t || !t->Readable || !t->Entities)
        {
            if (error)
                *error = t && !t->Issues.empty() ? t->Issues.front() : std::string("prefab o'qilmadi");
            return {};
        }
        std::vector<Entity> ordered;
        for (uint64_t id : t->Order)
            if (Entity e = t->Entities->GetEntityByUUID(UUID(id)))
                ordered.push_back(e);
        SceneSerializer::EntitySetWriteOptions options;
        options.HiddenLocked = false;
        json doc = SceneSerializerJson::EntitySetToJson(*t->Entities, ordered, options);
        json origin = { { "Prefab", path } };
        if (std::string handle = PrefabHandle(path); !handle.empty())
            origin["PrefabUUID"] = handle;
        doc["Origin"] = std::move(origin);
        return doc.dump(2);
    }

    bool EntityClipboard::IsEntityText(std::string_view text) { return text.find("\"ZaminEntities\"") != std::string_view::npos; }

    std::string EntityClipboard::GetOrigin(const std::string& text)
    {
        if (!IsEntityText(text) || text.find("\"Origin\"") == std::string::npos)
            return {};
        const json root = ParseObject(text);
        const auto it = root.is_object() ? root.find("Origin") : root.end();
        if (!root.is_object() || it == root.end() || !it->is_object())
            return {};
        return PrefabDatabase::NormalizePath(it->value("Prefab", std::string()));
    }

    EntityClipboard::PasteResult EntityClipboard::Paste(Scene& scene, PrefabDatabase* db, const std::string& text, Entity sibling)
    {
        PasteResult result;
        if (!IsEntityText(text))
        {
            result.Error = "nusxalangan obyektlar yo'q";
            return result;
        }
        Entity parent = sibling ? scene.GetParent(sibling) : Entity();
        int index = sibling ? scene.GetSiblingIndex(sibling) + 1 : -1;

        // The clipboard's own keys (a prefab file or an older clipboard has none).
        struct WorldPlace
        {
            uint64_t Parent = 0;   // the parent it was copied from below
            glm::mat4 Matrix{ 1.0f };
        };
        std::unordered_map<uint64_t, WorldPlace> world;
        std::string origin;
        if (text.find("\"World\"") != std::string::npos || text.find("\"Origin\"") != std::string::npos)
        {
            const json root = ParseObject(text);
            if (auto it = root.is_object() ? root.find("World") : root.end(); root.is_object() && it != root.end() && it->is_object())
                for (const auto& [key, value] : it->items())
                {
                    WorldPlace place;
                    char* end = nullptr;
                    const uint64_t id = std::strtoull(key.c_str(), &end, 10);
                    if (!id || !end || *end != '\0' || !value.is_object())
                        continue;
                    if (auto p = value.find("Parent"); p != value.end() && p->is_number_unsigned())
                        place.Parent = p->get<uint64_t>();
                    if (auto m = value.find("Matrix"); m != value.end() && MatrixFromJson(*m, place.Matrix))
                        world[id] = place;
                }
            if (auto it = root.is_object() ? root.find("Origin") : root.end(); db && root.is_object() && it != root.end())
            {
                origin = ResolveOrigin(*it, *db);
                if (origin.empty() && it->is_object())
                    ZM_CORE_WARN("Nusxalangan prefab topilmadi ({}): obyektlari bog'lanmagan holda joylandi", it->value("Prefab", std::string("?")));
            }
        }

        // A prefab copied from the content browser: a linked instance of it, as a drop makes.
        if (!origin.empty())
        {
            Entity root = PrefabUtility::InstantiateLinked(scene, *db, origin, parent);
            if (root)
            {
                result.Origin = origin;
                result.Roots.push_back(root);
                result.Entities.push_back(root);
                for (Entity e : scene.GetDescendants(root))
                    result.Entities.push_back(e);
                if (index >= 0)
                    scene.SetSiblingIndex(root, index);
                return result;
            }
        }

        std::unordered_map<uint64_t, UUID> ids;
        std::string error;
        SceneSerializer::EntitySetReadOptions options;
        options.GenerateNewUUIDs = true;
        options.KeepPrefabLinks = true;
        options.IDsOut = &ids;
        options.Error = &error;
        result.Entities = SceneSerializer::DeserializeEntities(scene, text, options);
        if (result.Entities.empty())
        {
            result.Error = error.empty() ? std::string("obyekt yo'q") : error;
            return result;
        }
        // A pasted instance stays one; a part of one (outside it now) is plain.
        result.Unlinked = PrefabUtility::NormalizeLinks(scene, &result.Entities);

        std::unordered_map<uint64_t, uint64_t> fileOf; // new UUID -> its ID in the text
        for (const auto& [file, id] : ids)
            fileOf[(uint64_t)id] = file;
        for (Entity e : result.Entities)
            if (!scene.GetParent(e))
                result.Roots.push_back(e);
        for (Entity root : result.Roots)
        {
            auto& transform = root.GetComponent<TransformComponent>();
            auto clean = [&]() {
                transform.Translation = { Clean(transform.Translation.x), Clean(transform.Translation.y), Clean(transform.Translation.z) };
                transform.Rotation = { Clean(transform.Rotation.x), Clean(transform.Rotation.y), Clean(transform.Rotation.z) };
                transform.Scale = { Clean(transform.Scale.x), Clean(transform.Scale.y), Clean(transform.Scale.z) };
            };
            // Where it was in the world (a root copied from below a parent); without that its own
            // values are its place in the world already (it was a top-level entity).
            const WorldPlace* place = nullptr;
            if (auto file = fileOf.find((uint64_t)root.GetUUID()); file != fileOf.end())
                if (auto w = world.find(file->second); w != world.end())
                    place = &w->second;
            if (place && parent && place->Parent == (uint64_t)parent.GetUUID())
            {
                scene.SetParent(root, parent, false); // back below the same parent: its values as they were
            }
            else
            {
                if (place)
                {
                    glm::vec3 t, r, s;
                    if (Math::DecomposeTransform(place->Matrix, t, r, s))
                    {
                        transform.Translation = t;
                        transform.Rotation = r;
                        transform.Scale = s;
                        clean();
                    }
                }
                if (parent)
                {
                    scene.SetParent(root, parent, true);
                    clean();
                }
            }
            if (index >= 0)
                scene.SetSiblingIndex(root, index++);
        }
        return result;
    }

}
