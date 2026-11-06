#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

namespace Zamin {

    // Global C++ services of a game - a score keeper, an inventory, a quest log, a save manager -
    // registered once by type and found from any script of any scene. They live outside the scenes,
    // so every scene load (Single or additive) keeps them; unlike a DontDestroyOnLoad entity they
    // need no entity at all.
    //
    //     struct Inventory { std::vector<std::string> Items; };
    //     Services::Register<Inventory>();                 // or Register(std::make_shared<Inventory>())
    //     Services::Get<Inventory>()->Items.push_back("Kalit");
    //     auto& quests = Services::GetOrCreate<QuestLog>(); // made on first use
    //
    // One service per type (registering a type again replaces it). A service is destroyed when it is
    // unregistered or the services are cleared - in reverse registration order: the editor clears
    // them when Play starts and stops (every run starts fresh), and before the game's script module
    // is unloaded (a service made by the module's code must go while that code is there). Main
    // thread only.
    class Services
    {
    public:
        template<typename T, typename... Args>
        static T& Register(Args&&... args)
        {
            auto service = std::make_shared<T>(std::forward<Args>(args)...);
            T& ref = *service;
            Add(typeid(T), std::static_pointer_cast<void>(std::move(service)), typeid(T).name());
            return ref;
        }
        template<typename T>
        static T& Register(std::shared_ptr<T> service)
        {
            T& ref = *service;
            Add(typeid(T), std::static_pointer_cast<void>(std::move(service)), typeid(T).name());
            return ref;
        }
        // The registered T (null when there is none).
        template<typename T>
        static T* Get()
        {
            return static_cast<T*>(Find(typeid(T)));
        }
        // The registered T, made with T's default constructor when there is none.
        template<typename T>
        static T& GetOrCreate()
        {
            if (T* service = Get<T>())
                return *service;
            return Register<T>();
        }
        template<typename T>
        static bool Has() { return Find(typeid(T)) != nullptr; }
        // Destroys the T (false when there was none).
        template<typename T>
        static bool Unregister() { return Remove(typeid(T)); }

        // Destroys every service, the last registered first.
        static void Clear();
        static size_t GetCount();
        // The registered services' type names (readable: "Inventory", "Game::QuestLog"), in
        // registration order - the editor shows them while the game runs.
        static std::vector<std::string> GetNames();
        // Grows with every Register / Unregister / Clear (hosts notice changes).
        static uint64_t GetRevision();

    private:
        static void Add(std::type_index type, std::shared_ptr<void> service, const char* mangledName);
        static void* Find(std::type_index type);
        static bool Remove(std::type_index type);
    };

}
