#pragma once
#include "UIModel.h"
#include <glm/glm.hpp>
#include <map>
#include <deque>
#include <cstdint>
namespace Zamin {
    class Scene;
    class Entity;
}
namespace Zamin::Gameplay {
    struct UIPanelSpec
    {
        std::string Name = "Pause", Action = "Pause";
        uint64_t Focus = 0;
        bool Pause = true, Block = true, Back = true;
    };
    enum class HUDKind
    {
        Health,
        Shield,
        Item,
        Bag,
        Deaths,
        Effect,   // Item = a status effect key: Prefix + stacks (" / " + max with Ratio); bar = time left
        HitMarker // Prefix while the actor's newest direct outgoing hit is younger than 0.25 s; bar fades
    };
    struct HUDBindingSpec
    {
        HUDKind Kind = HUDKind::Health;
        std::string Item = "Coin", Prefix = "HP "; // Item: the item key (Item) or the effect key (Effect)
        bool Ratio = true;
    };
    // A combat feed: up to 6 Text rows show the actor's recent hit records (CombatWorld::Recent), newest first,
    // "-18  Bolt" / "+5  Mend", their colour alpha fading over Lifetime seconds (0.1..10).
    struct CombatFeedSpec
    {
        bool Outgoing = true, Incoming = false;
        double Lifetime = 1.2;
    };
    struct UIEvent
    {
        std::string Kind, Name, Item;
        uint64_t Subject = 0;
    };
    struct UIState
    {
        std::string Top;
        size_t Depth = 0;
        bool Paused = false, Blocked = false, Fenced = false;
        uint32_t Dropped = 0;
    };
    struct InventoryUIState
    {
        bool Configured = false, Available = false;
        int Page = 0, Pages = 1, Count = 0, Slots = 0, Used = 0;
        std::string Item;
    };
    class UIWorld
    {
    public:
        static UIWorld& For(Scene&);
        explicit UIWorld(Scene* scene) : m_Scene(scene) {}
        UIWorld(const UIWorld&) = delete;
        UIWorld& operator=(const UIWorld&) = delete;
        bool ConfigurePanel(Entity owner, Entity root, const UIPanelSpec&, std::string& error);
        bool Open(const std::string&, std::string& error);
        bool Close(const std::string&, std::string& error);
        bool Back(std::string& error);
        bool RemovePanel(const std::string&);
        void Clear();
        bool BindHUD(Entity owner, Entity actor, Entity widget, const HUDBindingSpec&, std::string& error);
        bool UnbindHUD(Entity widget);
        bool BindInventory(Entity owner, Entity actor, const std::string& name, const std::vector<Entity>& rows, std::string& error);
        bool RemoveInventory(const std::string&);
        static constexpr size_t MaxFeeds = 8, MaxFeedRows = 6;
        static constexpr double HitMarkerSeconds = .25;
        bool BindCombatFeed(Entity owner, Entity actor, const std::string& name, const std::vector<Entity>& rows, const CombatFeedSpec&, std::string& error);
        bool RemoveCombatFeed(const std::string&);
        bool Page(const std::string&, int delta);
        InventoryUIState InventoryState(const std::string&);
        UIState State() const;
        std::optional<UIEvent> Poll();
        bool Allows(Entity) const;
        bool Fenced() const { return m_Fenced; }
        bool BlocksGame() const { return m_Fenced || m_Stack.Blocks(); }
        void BeginFrame();
        void Refresh();
    private:
        friend class QuestWorld;
        struct Ref
        {
            uint64_t ID = 0;
            uint32_t Handle = ~0u;
        };
        struct Owner
        {
            Ref Entity;
            bool Script = false;
        };
        struct Panel
        {
            Owner Host;
            Ref Root, Focus, Previous, Last;
            UIPanelSpec Spec;
            bool OriginalActive = false;
            int OriginalOrder = 0;
        };
        struct Widget
        {
            Ref Target;
            std::string Text;
            glm::vec4 Color{ 1 };
            float Value = 0, Display = 0;
            bool Interactable = true, RestoreColor = false; // colour: only feed rows (which fade) change it
        };
        struct Binding
        {
            Owner Host;
            Ref Actor;
            Widget View;
            HUDBindingSpec Spec;
        };
        struct Inventory
        {
            Owner Host;
            Ref Actor;
            std::vector<Widget> Rows;
            int Page = 0;
            std::string Selected;
        };
        struct Feed
        {
            Owner Host;
            Ref Actor;
            std::vector<Widget> Rows;
            CombatFeedSpec Spec;
        };
        Ref Reference(Entity) const;
        Entity Resolve(Ref) const;
        Owner Own(Entity) const;
        bool Live(const Owner&) const;
        bool Enabled(const Owner&) const;
        bool Inside(Entity, Entity root) const;
        bool WidgetUsed(Entity) const;
        Widget Remember(Entity) const;
        void RestoreWidget(const Widget&);
        void Apply();
        void FocusTop();
        void Emit(UIEvent);
        void SelectInventory(const std::string&, Inventory&);
        void RefreshFeeds();
        Scene* m_Scene;
        MenuStack m_Stack;
        std::map<std::string, Panel> m_Panels;
        std::map<uint64_t, Binding> m_Bindings;
        std::map<std::string, Inventory> m_Inventories;
        std::map<std::string, Feed> m_Feeds;
        std::deque<UIEvent> m_Events;
        Ref m_Previous;
        float m_Baseline = 1;
        uint32_t m_Dropped = 0;
        bool m_OwnsTime = false, m_Fenced = false, m_Busy = false;
    };
    void ResetGameplayUI(Scene&);
    void BeginGameplayUI(Scene&);
    void RefreshGameplayUI(Scene&);
    bool GameplayUIAllows(Scene&, Entity);
    bool GameplayUIFenced(Scene&);
    bool GameplayUIBlocks(Scene&);
}
