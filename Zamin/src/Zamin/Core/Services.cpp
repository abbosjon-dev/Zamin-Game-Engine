#include "Zamin/Core/Services.h"
#include "Zamin/Core/Log.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>

#if defined(__GNUG__) || defined(__clang__)
#include <cxxabi.h>
#endif

namespace Zamin {

    namespace {

        struct ServiceEntry
        {
            std::shared_ptr<void> Instance;
            std::string Name;
            uint64_t Order = 0;
        };

        struct ServiceRegistry
        {
            std::unordered_map<std::type_index, ServiceEntry> Entries;
            uint64_t NextOrder = 1;
            uint64_t Revision = 0;
        };

        ServiceRegistry& Registry()
        {
            static ServiceRegistry s_Registry;
            return s_Registry;
        }

        std::string Demangle(const char* name)
        {
#if defined(__GNUG__) || defined(__clang__)
            int status = 0;
            char* readable = abi::__cxa_demangle(name, nullptr, nullptr, &status);
            if (status == 0 && readable)
            {
                std::string out(readable);
                std::free(readable);
                return out;
            }
            std::free(readable);
#endif
            return name;
        }

    }

    void Services::Add(std::type_index type, std::shared_ptr<void> service, const char* mangledName)
    {
        ServiceRegistry& registry = Registry();
        std::shared_ptr<void> previous;
        auto it = registry.Entries.find(type);
        if (it != registry.Entries.end())
        {
            previous = std::move(it->second.Instance); // destroyed after the new one is in
            ZM_CORE_WARN("Servis qayta ro'yxatdan o'tkazildi (eskisi o'chirildi): {}", it->second.Name);
            it->second.Instance = std::move(service);
            it->second.Order = registry.NextOrder++;
        }
        else
            registry.Entries.emplace(type, ServiceEntry{ std::move(service), Demangle(mangledName), registry.NextOrder++ });
        registry.Revision++;
    }

    void* Services::Find(std::type_index type)
    {
        ServiceRegistry& registry = Registry();
        auto it = registry.Entries.find(type);
        return it != registry.Entries.end() ? it->second.Instance.get() : nullptr;
    }

    bool Services::Remove(std::type_index type)
    {
        ServiceRegistry& registry = Registry();
        auto it = registry.Entries.find(type);
        if (it == registry.Entries.end())
            return false;
        std::shared_ptr<void> instance = std::move(it->second.Instance);
        registry.Entries.erase(it);
        registry.Revision++;
        instance.reset(); // its destructor may look up other services: the registry is consistent now
        return true;
    }

    void Services::Clear()
    {
        ServiceRegistry& registry = Registry();
        if (registry.Entries.empty())
            return;
        // The last registered first (it may use the ones before it), one at a time: a destructor
        // that looks up another service still finds the ones not destroyed yet.
        while (!registry.Entries.empty())
        {
            auto newest = std::max_element(registry.Entries.begin(), registry.Entries.end(),
                                           [](const auto& a, const auto& b) { return a.second.Order < b.second.Order; });
            std::shared_ptr<void> instance = std::move(newest->second.Instance);
            registry.Entries.erase(newest);
            instance.reset();
        }
        registry.Revision++;
    }

    size_t Services::GetCount()
    {
        return Registry().Entries.size();
    }

    std::vector<std::string> Services::GetNames()
    {
        std::vector<const ServiceEntry*> entries;
        for (const auto& [type, entry] : Registry().Entries)
            entries.push_back(&entry);
        std::sort(entries.begin(), entries.end(), [](const ServiceEntry* a, const ServiceEntry* b) { return a->Order < b->Order; });
        std::vector<std::string> names;
        names.reserve(entries.size());
        for (const ServiceEntry* entry : entries)
            names.push_back(entry->Name);
        return names;
    }

    uint64_t Services::GetRevision()
    {
        return Registry().Revision;
    }

}
