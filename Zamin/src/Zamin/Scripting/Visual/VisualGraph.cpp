#include "VisualGraph.h"
#include "Zamin/Utils/FileSystem.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace Zamin::Visual {
    namespace {
        Pin E(const char* n) { return { n, Type::Exec, nullptr }; }
        Pin N(const char* n, double d = 0) { return { n, Type::Number, d }; }
        Pin B(const char* n, bool d = false) { return { n, Type::Bool, d }; }
        Pin S(const char* n, const char* d = "") { return { n, Type::String, d }; }
        Pin V(const char* n, int dims = 3) { return { n, dims == 2 ? Type::Vec2 : dims == 3 ? Type::Vec3
                                                                                            : Type::Vec4,
                                                      Value(std::vector<double>(dims, 0)) }; }
        Pin EntityPin(const char* n = "Target") { return { n, Type::Entity, std::string(n) == "Target" ? UINT64_MAX : 0ull }; }
        const Pin* FindPin(const std::vector<Pin>& pins, const std::string& name)
        {
            for (auto& p : pins)
                if (p.Name == name) return &p;
            return nullptr;
        }
        void Require(bool ok, const std::string& message)
        {
            if (!ok) throw std::runtime_error(message);
        }
        bool SafeValue(const Value& v)
        {
            if (v.is_string()) return v.get_ref<const std::string&>().size() <= 65536;
            if (ValueBytes(v) > 65536) return false;
            if (v.is_number()) return std::isfinite(v.get<double>());
            if (v.is_string() || v.is_boolean() || v.is_null()) return true;
            if (!v.is_array() || v.size() > 1024) return false;
            return std::all_of(v.begin(), v.end(), [](const auto& item) { return SafeValue(item); });
        }
    }
    const char* TypeName(Type t)
    {
        static const char* names[] = { "Exec", "Bool", "Number", "String", "Vec2", "Vec3", "Vec4", "Entity", "ArrayBool", "ArrayNumber", "ArrayString", "ArrayVec2", "ArrayVec3", "ArrayVec4", "ArrayEntity" };
        return t >= Type::Exec && t < Type::Count ? names[int(t)] : "Invalid";
    }
    bool Matches(Type t, const Value& v)
    {
        if (t < Type::Exec || t >= Type::Count) return false;
        if (IsArray(t)) return ValueBytes(v) <= 65536 && v.is_array() && v.size() <= 1024 && std::all_of(v.begin(), v.end(), [&](const auto& item) { return Matches(ElementType(t), item); });
        switch (t)
        {
            case Type::Exec:   return v.is_null();
            case Type::Bool:   return v.is_boolean();
            case Type::Number: return v.is_number() && std::isfinite(v.get<double>());
            case Type::String: return v.is_string() && v.get_ref<const std::string&>().size() <= 65536;
            case Type::Entity: return v.is_number_unsigned() || (v.is_number_integer() && v.get<int64_t>() >= 0);
            default:
                if (!v.is_array() || v.size() != size_t(int(t) - int(Type::Vec2) + 2)) return false;
                return std::all_of(v.begin(), v.end(), [](const auto& item) { return Matches(Type::Number, item); });
        }
    }

    const std::map<std::string, Definition>& Catalog()
    {
        static const auto catalog = [] {std::map<std::string,Definition> c;
auto add=[&](std::string key,std::string category,std::vector<Pin> in,std::vector<Pin> out,bool pure=false){c[key]={key,category,std::move(in),std::move(out),pure};};
for (auto name : { "Create", "Enable", "Start", "Disable", "Destroy", "Update", "LateUpdate", "CollisionBegin", "CollisionEnd", "TriggerBegin", "TriggerEnd", "Click", "PointerEnter", "PointerExit", "PointerDown", "PointerUp", "PointerClick", "Select", "Deselect", "Cancel", "ValueChanged", "ToggleChanged", "TextChanged", "TextSubmit", "TextEndEdit", "ScrollChanged", "ParticlesFinished", "TargetSpotted", "TargetLost", "PatrolPointReached", "Message", "AnimationEvent", "LanguageChanged", "SceneLoaded", "SceneUnloaded", "ActiveSceneChanged", "SoundFinished", "JointBreak", "JointBreak3D", "NavTraversalRequested" })
{
    std::string event = name;
    std::vector<Pin> outputs{ E("Then") };
    if (event == "Update" || event == "LateUpdate") outputs.push_back(N("Delta"));
    if (event.starts_with("Collision") || event.starts_with("Trigger") || event.starts_with("Target") || event.starts_with("Pointer") || event == "Message") outputs.push_back(EntityPin("Other"));
    if (event == "ValueChanged" || event == "PatrolPointReached" || event == "Message" || event == "AnimationEvent" || event.starts_with("Scene") || event.starts_with("Joint")) outputs.push_back(N("Number"));
    if (event == "ToggleChanged" || event.starts_with("Pointer") || event == "SoundFinished") outputs.push_back(B("Bool"));
    if (event.starts_with("Text") || event == "Message" || event == "AnimationEvent" || event == "LanguageChanged" || event.find("Scene") != std::string::npos || event == "SoundFinished") outputs.push_back(S("Text"));
    if (event == "Message") outputs.push_back(S("Payload"));
    if (event == "NavTraversalRequested")
    {
        outputs.push_back(S("Link"));
        outputs.push_back(S("Revision"));
        outputs.push_back(S("Ticket"));
        outputs.push_back(V("Entry"));
        outputs.push_back(V("Exit"));
    }
    if (event.starts_with("Pointer") || event == "ScrollChanged") outputs.push_back(V("Vector", 2));
    add("Event." + event, "Events", {}, outputs);
}
add("Function.Entry", "Typed functions", { S("Name", "MyFunction") }, { E("Then") });
add("Function.Call", "Typed functions", { E("In"), S("Name", "MyFunction") }, { E("Then") });
add("Function.Return", "Typed functions", { E("In"), S("Name", "MyFunction") }, {});
add("Event.Custom", "Events", { S("Name", "MyEvent") }, { E("Then"), N("Number"), S("Text"), B("Bool"), V("Vector"), EntityPin("Other") });
add("EmitEvent", "Events", { E("In"), S("Name", "MyEvent"), N("Number"), S("Text"), B("Bool"), V("Vector"), EntityPin("Other") }, { E("Then") });
for (int ti = 1; ti < int(Type::Count); ++ti)
{
    auto type = Type(ti);
    auto value = DefaultValue(type);
    std::string name = TypeName(type);
    add("Local.Get." + name, "Function locals", { S("Name") }, { { "Value", type, value } }, true);
    add("Local.Set." + name, "Function locals", { E("In"), S("Name"), { "Value", type, value } }, { E("Then") });
    if (IsArray(type))
    {
        auto element = ElementType(type);
        std::string suffix = TypeName(element);
        auto item = DefaultValue(element);
        add("Constant." + name, "Collections", { { "Value", type, value } }, { { "Value", type, value } }, true);
        add("Get." + name, "Variables", { S("Name") }, { { "Value", type, value } }, true);
        add("Set." + name, "Variables", { E("In"), S("Name"), { "Value", type, value } }, { E("Then") });
        add("Array.Empty." + suffix, "Collections", {}, { { "Value", type, value } }, true);
        add("Array.Append." + suffix, "Collections", { { "Array", type, value }, { "Value", element, item } }, { { "Value", type, value } }, true);
        add("Array.Set." + suffix, "Collections", { { "Array", type, value }, N("Index"), { "Value", element, item } }, { { "Value", type, value } }, true);
        add("Array.Remove." + suffix, "Collections", { { "Array", type, value }, N("Index") }, { { "Value", type, value } }, true);
        add("Array.Get." + suffix, "Collections", { { "Array", type, value }, N("Index") }, { { "Value", element, item } }, true);
        add("Array.Length." + suffix, "Collections", { { "Array", type, value } }, { N("Value") }, true);
        add("Array.Contains." + suffix, "Collections", { { "Array", type, value }, { "Value", element, item } }, { B("Value") }, true);
        add("Array.Find." + suffix, "Collections", { { "Array", type, value }, { "Value", element, item } }, { N("Value", -1) }, true);
        add("Array.Slice." + suffix, "Collections", { { "Array", type, value }, N("Start"), N("Count") }, { { "Value", type, value } }, true);
        add("Array.Concat." + suffix, "Collections", { { "Array", type, value }, { "Other", type, value } }, { { "Value", type, value } }, true);
        add("ForEach." + suffix, "Collections", { E("In"), { "Array", type, value } }, { E("Body"), E("Completed"), N("Index", -1), { "Value", element, item } });
    }
}
add("Function","Functions",{S("Name","MyFunction")},{E("Then")});
add("Call","Functions",{E("In"),S("Name","MyFunction")},{E("Then")});
add("Branch","Flow",{E("In"),B("Condition")},{E("True"),E("False")});
add("Sequence","Flow",{E("In")},{E("First"),E("Second"),E("Third")});
add("While","Flow",{E("In"),B("Condition")},{E("Body"),E("Completed")});
add("Break","Flow",{E("In")},{});
add("For","Flow",{E("In"),N("Count",10)},{E("Body"),E("Completed"),N("Index")});
add("Delay","Flow",{E("In"),N("Seconds",1)},{E("Then")});
for(auto t:{Type::Bool,Type::Number,Type::String,Type::Vec2,Type::Vec3,Type::Vec4,Type::Entity}){
 Value d=t==Type::Bool?Value(false):t==Type::String?Value(""):t==Type::Entity?Value(0ull):t==Type::Number?Value(0.0):Value(std::vector<double>(t==Type::Vec2?2:t==Type::Vec3?3:4,0));
 std::string suffix=TypeName(t);
 add("ReadField."+suffix,"C++ bridge",{EntityPin(),S("Name")},{{"Value",t,d}},true);
 add("WriteField."+suffix,"C++ bridge",{E("In"),EntityPin(),S("Name"),{"Value",t,d}},{E("Then")});
 add("Constant."+suffix,"Values",{{"Value",t,d}},{{"Value",t,d}},true);
 add("Get."+suffix,"Variables",{S("Name")},{{"Value",t,d}},true);
 add("Set."+suffix,"Variables",{E("In"),S("Name"),{"Value",t,d}},{E("Then")});
}
for(auto name:{"Add","Subtract","Multiply","Divide","Min","Max","Power","Modulo"})add(name,"Math",{N("A"),N("B",1)},{N("Value")},true);
for(auto name:{"Sin","Cos","Abs","Sqrt","Floor","Ceil","Negate"})add(name,"Math",{N("A")},{N("Value")},true);
for(auto name:{"Less","Greater","Equal"})add(name,"Logic",{N("A"),N("B")},{B("Value")},true);
for(auto name:{"And","Or"})add(name,"Logic",{B("A"),B("B")},{B("Value")},true);
add("Not","Logic",{B("A")},{B("Value")},true);
add("Clamp","Math",{N("Value"),N("Min"),N("Max",1)},{N("Value")},true);
add("Lerp","Math",{N("A"),N("B",1),N("T",0.5)},{N("Value")},true);
add("Concat","Text",{S("A"),S("B")},{S("Value")},true);
add("TextEqual", "Text", { S("A"), S("B") }, { B("Value") }, true);
add("NumberToText","Text",{N("Number")},{S("Value")},true);
for(int dim=2;dim<=4;++dim){std::vector<Pin> p;for(int i=0;i<dim;++i)p.push_back(N(std::string(1,"XYZW"[i]).c_str()));add("MakeVec"+std::to_string(dim),"Vectors",p,{V("Value",dim)},true);add("BreakVec"+std::to_string(dim),"Vectors",{V("Vector",dim)},p,true);add("ScaleVec"+std::to_string(dim),"Vectors",{V("Vector",dim),N("Scale",1)},{V("Value",dim)},true);add("AddVec"+std::to_string(dim),"Vectors",{V("A",dim),V("B",dim)},{V("Value",dim)},true);}
add("Self","Entities",{},{EntityPin("Value")},true);
add("FindEntity","Entities",{S("Name")},{EntityPin("Value")},true);
add("EntityValid","Entities",{EntityPin()},{B("Value")},true);
add("GetPosition","Transform",{EntityPin()},{V("Value")},true);
add("GetRotation","Transform",{EntityPin()},{V("Value")},true);
add("GetScale","Transform",{EntityPin()},{V("Value")},true);
for(auto name:{"SetPosition","SetRotation","SetScale","Translate"})add(name,"Transform",{E("In"),EntityPin(),V("Vector")},{E("Then")});
add("DestroyEntity","Entities",{E("In"),EntityPin()},{E("Then")});
add("SpawnPrefab","Entities",{E("In"),S("Path"),V("Position")},{E("Then"),EntityPin("Value")});
add("SetActive","Entities",{E("In"),EntityPin(),B("Active",true)},{E("Then")});
add("HasTag","Entities",{EntityPin(),S("Tag")},{B("Value")},true);
for(auto name:{"AddTag","RemoveTag"})add(name,"Entities",{E("In"),EntityPin(),S("Tag")},{E("Then")});
for(auto name:{"SetVelocity2D","ApplyForce2D","ApplyImpulse2D"})add(name,"Physics 2D",{E("In"),EntityPin(),V("Vector",2)},{E("Then")});
for(auto name:{"SetVelocity3D","ApplyForce3D","ApplyImpulse3D","ApplyTorque3D"})add(name,"Physics 3D",{E("In"),EntityPin(),V("Vector")},{E("Then")});
add("GetVelocity2D","Physics 2D",{EntityPin()},{V("Value",2)},true);add("GetVelocity3D","Physics 3D",{EntityPin()},{V("Value")},true);
add("Axis","Input",{S("Name")},{N("Value")},true);add("ActionPressed","Input",{S("Name")},{B("Value")},true);add("ActionJustPressed","Input",{S("Name")},{B("Value")},true);
add("Print","Debug",{E("In"),S("Text")},{E("Then")});
add("PlaySound","Audio",{E("In"),S("Path"),N("Volume",1),N("Pitch",1)},{E("Then")});

add("Gameplay.Input", "Gameplay input", { EntityPin(), S("Move", "Move"), S("Jump", "Jump"), S("Sprint", "Sprint"), B("Enabled", true) }, { V("Move", 2), V("Look", 2), B("Jump"), B("JumpHeld"), B("Sprint"), B("Enabled") }, true);
add("Gameplay.Motion", "Gameplay motion", { EntityPin(), B("TwoD"), B("TopDown"), N("GroundMask", 65535) }, { V("Velocity"), N("Speed"), B("Grounded"), V("Position") }, true);
for (bool twoD : { true, false })
{
    std::vector<Pin> inputs = { E("In"), EntityPin(), V("Move", 2), B("Jump"), B("JumpHeld", true), B("Sprint"), B("Enabled", true), N("Delta"), N("Speed", 5), N("Acceleration", 30), N("Deceleration", 35), N("AirControl", .35), N("SprintMultiplier", 1.6), N("JumpSpeed", 8), N("Coyote", .1), N("JumpBuffer", .12), N("JumpCut", .5), N("Probe", .12), N("GroundMask", 65535) };
    if (twoD) inputs.push_back(B("TopDown"));
    else inputs.push_back(EntityPin("Camera"));
    add(twoD ? "Gameplay.Motor2D" : "Gameplay.Motor3D", "Gameplay motor", inputs, { E("Then"), V("Velocity"), B("Jumped"), B("Grounded"), N("Facing", 1) });
}
add("Gameplay.BindAnimator", "Gameplay animation", { E("In"), EntityPin(), EntityPin("Source"), B("TwoD"), B("TopDown"), S("SpeedParameter", "Speed"), S("HorizontalParameter", "MoveX"), S("ForwardParameter", "MoveY"), S("VerticalParameter", "VerticalSpeed"), S("GroundedParameter", "Grounded"), N("ReferenceSpeed", 5), N("Damping", .1), N("Delta"), N("GroundMask", 65535) }, { E("Then"), B("Success"), N("Speed"), B("Grounded") });
add("Gameplay.Camera", "Gameplay camera", { E("In"), EntityPin(), EntityPin("Anchor"), { "Eye", Type::Vec3, Value::array({ 0., 1., 0. }) }, { "Offset", Type::Vec3, Value::array({ 0., 2., 6. }) }, V("Look", 2), B("Orbit"), N("Delta"), N("Damping", .15), N("Sensitivity", .003), N("SnapDistance", 5), B("Snap"), N("Radius", .2), N("Mask", 65535), { "BoundsMin", Type::Vec2, Value::array({ -1000000., -1000000. }) }, { "BoundsMax", Type::Vec2, Value::array({ 1000000., 1000000. }) } }, { E("Then"), V("Position"), B("Blocked") });
for (auto op : { "Capture", "Restore", "Clear", "Has" })
{
    const bool pure = std::string(op) == "Has";
    std::vector<Pin> inputs;
    if (!pure) inputs.push_back(E("In"));
    inputs.push_back(EntityPin());
    inputs.push_back(S("Slot", "Player"));
    inputs.push_back(B("Persistent", true));
    if (std::string(op) == "Restore")
    {
        inputs.push_back(N("Clearance", .1));
        inputs.push_back(N("Mask", 65535));
    }
    add("Gameplay.Checkpoint" + std::string(op), "Gameplay checkpoint", inputs, pure ? std::vector<Pin>{ B("Value") } : std::vector<Pin>{ E("Then"), B("Success"), S("Reason") }, pure);
}
add("Gameplay.HealthConfigure", "Gameplay health", { E("In"), EntityPin(), N("Maximum", 100), N("Shield"), N("HitGrace", .2), N("RespawnDelay", .5), N("Team"), B("Reset") }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.Health", "Gameplay health", { EntityPin() }, { B("Configured"), N("Current"), N("Maximum"), N("Shield"), N("MaximumShield"), B("Dead"), B("Invulnerable"), N("Deaths"), N("RespawnIn") }, true);
std::vector<Pin> damageOut = { E("Then"), B("Success"), N("Applied"), N("HealthDamage"), N("ShieldDamage"), B("Killed"), S("Reason") };
add("Gameplay.Damage", "Gameplay health", { E("In"), EntityPin(), EntityPin("Source"), N("Amount", 25), B("FriendlyFire"), B("PierceShield"), B("IgnoreGrace") }, damageOut);
add("Gameplay.Heal", "Gameplay health", { E("In"), EntityPin(), N("Amount", 25), B("Shield") }, { E("Then"), B("Success"), N("Applied") });
add("Gameplay.Respawn", "Gameplay health", { E("In"), EntityPin(), S("Slot", "Player"), B("Persistent", true), N("Clearance", .1), N("Mask", 65535), N("Grace", 1) }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.InventoryConfigure", "Gameplay inventory", { E("In"), EntityPin(), N("Slots", 32), N("Stack", 999), B("Reset") }, { E("Then"), B("Success"), S("Reason") });
for (auto op : { "Add", "Remove" }) add("Gameplay.Inventory" + std::string(op), "Gameplay inventory", { E("In"), EntityPin(), S("Item", "Coin"), N("Count", 1) }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.InventoryCount", "Gameplay inventory", { EntityPin(), S("Item", "Coin") }, { N("Count"), N("UsedSlots"), N("Slots"), N("Stack") }, true);
add("Gameplay.InteractionConfigure", "Gameplay interaction", { E("In"), EntityPin(), S("Prompt", "Interact"), S("RequiredItem"), N("RequiredCount", 1), B("Consume"), S("RewardItem"), N("RewardCount", 1), N("Heal"), N("MaxUses", 1), N("Cooldown", .2), B("Deactivate"), B("Enabled", true), V("Offset"), B("Reset") }, { E("Then"), B("Success"), S("Reason") });
std::vector<Pin> queryPins = { N("Range", 3), N("MinDot", -1), { "Facing", Type::Vec3, Value::array({ 1., 0., 0. }) }, { "Origin", Type::Vec3, Value::array({ 0., 1., 0. }) }, B("TwoD"), B("LineOfSight", true), N("Mask", 65535) };
std::vector<Pin> useOut = { B("Success"), EntityPin("Object"), N("Distance"), S("Prompt"), S("Reason"), N("Healed") };
for (auto op : { "InteractionFind", "Interact", "Attack" })
{
    const std::string name = op;
    const bool pure = name == "InteractionFind";
    std::vector<Pin> pins;
    if (!pure) pins.push_back(E("In"));
    pins.push_back(EntityPin());
    if (name == "Interact") pins.push_back(EntityPin("Actor"));
    pins.insert(pins.end(), queryPins.begin(), queryPins.end());
    auto outputs = useOut;
    if (name == "Attack")
    {
        pins.push_back(N("Amount", 25));
        pins.push_back(N("Cooldown", .4));
        pins.push_back(B("FriendlyFire"));
        outputs = damageOut;
        outputs.push_back(EntityPin("Victim"));
    }
    else if (pure) outputs.push_back(B("Found"));
    else outputs.insert(outputs.begin(), E("Then"));
    add("Gameplay." + name, name == "Attack" ? "Gameplay combat" : "Gameplay interaction", pins, outputs, pure);
}
add("Gameplay.RuleEvent", "Gameplay events", { E("In"), EntityPin() }, { E("Then"), B("Has"), S("Kind"), S("Item"), EntityPin("Source"), EntityPin("Subject"), N("Amount"), N("Dropped") });
add("Gameplay.LoadoutConfigure", "Gameplay loadout", { E("In"), EntityPin(), { "Stats", Type::ArrayNumber, Value::array({ 0, 0, 0, 100, 5 }) } }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.ItemDefine", "Gameplay loadout", { E("In"), EntityPin(), S("Item", "Sword"), N("Slot"), { "Modifiers", Type::ArrayNumber, Value::array({ 10, 0, 0, 0, 0 }) } }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.EquipmentEquip", "Gameplay loadout", { E("In"), EntityPin(), S("Item", "Sword") }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.EquipmentUnequip", "Gameplay loadout", { E("In"), EntityPin(), N("Slot") }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.EquipmentState", "Gameplay loadout", { EntityPin(), N("Slot") }, { B("Configured"), B("Equipped"), S("Item") }, true);
add("Gameplay.LoadoutStats", "Gameplay loadout", { EntityPin() }, { B("Configured"), N("Attack"), N("Armor"), N("Power"), N("Maximum"), N("Regen"), N("Energy"), N("Fraction") }, true);
add("Gameplay.AbilityDefine", "Gameplay abilities", { E("In"), EntityPin(), S("Ability", "Bolt"), N("Kind"), N("Amount", 20), N("Cost", 10), N("Cooldown", 1), N("Range", 5), S("Ingredient"), N("Count", 1), B("Unlocked", true) }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.AbilityCast", "Gameplay abilities", { E("In"), EntityPin(), EntityPin("Victim"), S("Ability", "Bolt"), B("TwoD"), B("LineOfSight", true), N("Mask", 65535) }, { E("Then"), B("Success"), N("Applied"), EntityPin("Victim"), S("Reason") });
add("Gameplay.AbilityUnlock", "Gameplay abilities", { E("In"), EntityPin(), S("Ability", "Bolt"), B("Unlocked", true) }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.AbilityState", "Gameplay abilities", { EntityPin(), S("Ability", "Bolt") }, { B("Configured"), B("Unlocked"), B("Ready"), N("Remaining"), N("Fraction"), N("Uses"), N("Cost") }, true);
add("Gameplay.LoadoutRefill", "Gameplay loadout", { E("In"), EntityPin(), N("Amount", 25) }, { E("Then"), N("Applied") });
add("Gameplay.LoadoutEvent", "Gameplay loadout", { E("In"), EntityPin() }, { E("Then"), B("Has"), S("Kind"), S("Key"), N("Amount"), N("Dropped") });
add("Gameplay.QuestConfigure", "Gameplay quest", { E("In"), EntityPin(), S("Quest", "Quest"), S("Title", "Field survey"), { "Objectives", Type::ArrayString, Value::array({ "Explore" }) }, { "Labels", Type::ArrayString, Value::array({ "Reach checkpoint" }) }, { "Kinds", Type::ArrayNumber, Value::array({ 0 }) }, { "Matches", Type::ArrayString, Value::array({ "" }) }, { "Goals", Type::ArrayNumber, Value::array({ 1 }) }, { "Requires", Type::ArrayString, Value::array() }, { "Rewards", Type::ArrayString, Value::array() }, { "RewardCounts", Type::ArrayNumber, Value::array() }, N("XP", 100) }, { E("Then"), B("Success"), S("Reason") });
for (auto op : { "Start", "Fail", "Claim" }) add("Gameplay.Quest" + std::string(op), "Gameplay quest", { E("In"), EntityPin(), S("Quest", "Quest") }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.QuestAdvance", "Gameplay quest", { E("In"), EntityPin(), S("Quest", "Quest"), S("Objective", "Explore"), N("Count", 1), S("Receipt", "Checkpoint") }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.QuestState", "Gameplay quest", { EntityPin(), S("Quest", "Quest") }, { B("Configured"), S("Title"), S("Mode"), N("Fraction"), N("Complete"), N("Objectives"), B("Ready"), B("Claimed") }, true);
add("Gameplay.QuestObjective", "Gameplay quest", { EntityPin(), S("Quest", "Quest"), S("Objective", "Explore") }, { B("Found"), S("Label"), N("Current"), N("Goal"), B("Complete") }, true);
add("Gameplay.QuestProgression", "Gameplay quest", { EntityPin() }, { N("XP"), N("Level", 1), N("Current"), N("Needed", 100), N("Fraction") }, true);
add("Gameplay.QuestEvent", "Gameplay quest", { E("In"), EntityPin() }, { E("Then"), B("Has"), S("Kind"), S("Quest"), S("Objective"), N("Amount"), N("Dropped") });
add("Gameplay.QuestBindHUD", "Gameplay quest", { E("In"), EntityPin(), EntityPin("Actor"), S("Quest", "Quest"), S("Objective") }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.QuestUnbindHUD", "Gameplay quest", { E("In"), EntityPin() }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.UIConfigure", "Gameplay UI", { E("In"), EntityPin(), S("Name", "Pause"), S("Action", "Pause"), EntityPin("Focus"), B("Pause", true), B("Block", true), B("Back", true) }, { E("Then"), B("Success"), S("Reason") });
for (auto op : { "Open", "Close", "Remove" }) add("Gameplay.UI" + std::string(op), "Gameplay UI", { E("In"), EntityPin(), S("Name", "Pause") }, { E("Then"), B("Success"), S("Reason") });
for (auto op : { "Back", "Clear" }) add("Gameplay.UI" + std::string(op), "Gameplay UI", { E("In"), EntityPin() }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.UIRunWhenPaused", "Gameplay UI", { E("In"), EntityPin(), B("Enabled", true) }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.UIState", "Gameplay UI", { EntityPin() }, { S("Top"), N("Depth"), B("Paused"), B("Blocked"), B("Fenced"), EntityPin("Selected"), N("Time"), N("GameTime"), N("Delta"), N("Dropped") }, true);
add("Gameplay.UIEvent", "Gameplay UI", { E("In"), EntityPin() }, { E("Then"), B("Has"), S("Kind"), S("Name"), S("Item"), EntityPin("Subject") });
add("Gameplay.UIFocus", "Gameplay UI", { E("In"), EntityPin(), EntityPin("Widget") }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.UIBindHUD", "Gameplay UI", { E("In"), EntityPin(), EntityPin("Actor"), N("Kind"), S("Item", "Coin"), S("Prefix", "HP "), B("Ratio", true) }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.UIUnbindHUD", "Gameplay UI", { E("In"), EntityPin() }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.UIInventoryConfigure", "Gameplay UI", { E("In"), EntityPin(), EntityPin("Actor"), S("Name", "Bag"), { "Rows", Type::ArrayEntity, Value::array() } }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.UIInventoryState", "Gameplay UI", { EntityPin(), S("Name", "Bag") }, { B("Configured"), B("Available"), N("Page"), N("Pages", 1), S("Item"), N("Count"), N("UsedSlots"), N("Slots") }, true);
add("Gameplay.UIInventoryPage", "Gameplay UI", { E("In"), EntityPin(), S("Name", "Bag"), N("Delta", 1) }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.UIInventoryRemove", "Gameplay UI", { E("In"), EntityPin(), S("Name", "Bag") }, { E("Then"), B("Success"), S("Reason") });
for (auto op : { "Bind", "Unbind" }) add("Gameplay.Save" + std::string(op), "Gameplay save", { E("In"), EntityPin(), S("Key", "Player") }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.SaveFind", "Gameplay save", { EntityPin(), S("Key", "Player") }, { B("Found"), B("Active"), EntityPin("Entity") }, true);
add("Gameplay.SaveSpawn", "Gameplay save", { E("In"), EntityPin(), S("Key", "NPC"), S("Prefab"), V("Position") }, { E("Then"), B("Success"), EntityPin("Entity"), S("Reason") });
for (auto op : { "Write", "Load", "Inspect" })
{
    std::vector<Pin> pins = { E("In"), EntityPin(), S("Slot", "Quick") };
    if (std::string(op) != "Inspect") pins.push_back(S("Schema", "V1"));
    add("Gameplay.Save" + std::string(op), "Gameplay save", pins, { E("Then"), B("Success"), B("Recovered"), N("Entities"), S("Reason") });
}
add("Gameplay.AIConfigure", "Gameplay AI", { E("In"), EntityPin(), B("TwoD"), S("Tag", "Player"), EntityPin("Victim"), { "Points", Type::ArrayVec3, Value::array() }, B("Relative", true), B("PingPong", true), N("View", 10), N("Lose", 14), N("Near", 2), N("FieldOfView", 180), N("Memory", 2), N("Leash", 20), N("AttackEnter", 1.5), N("AttackExit", 1.8), N("Damage", 10), N("AttackCooldown", .5), N("Wait", .5), N("Arrive", .25), N("Repath", .3), N("Speed", 3), N("Stall", .75), N("RecoveryCooldown", .5), N("Retries", 3), { "Eye", Type::Vec3, Value::array({ 0., 1., 0. }) }, { "Facing", Type::Vec3, Value::array({ 1., 0., 0. }) }, N("Mask", 65535), B("Enabled", true), B("Reset") }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.AIState", "Gameplay AI", { EntityPin() }, { B("Configured"), S("Mode"), EntityPin("Victim"), B("Visible"), B("Enabled"), V("Home"), V("Goal"), V("LastSeen"), V("Facing"), N("Memory"), N("Waypoint"), B("Waiting"), N("Arrivals"), N("Returns"), N("Sightings"), N("Losses"), N("Attacks"), N("Kills"), N("Transitions"), N("PathRequests"), N("Retries"), S("Status"), N("Dropped") }, true);
add("Gameplay.AIEnable", "Gameplay AI", { E("In"), EntityPin(), B("Enabled", true) }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.AIReset", "Gameplay AI", { E("In"), EntityPin(), B("Rehome") }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.AIRemove", "Gameplay AI", { E("In"), EntityPin() }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.AIEvent", "Gameplay AI", { E("In"), EntityPin() }, { E("Then"), B("Has"), S("Mode"), EntityPin("Victim") });
for (auto op : { "Sense", "Strike" })
{
    const bool pure = std::string(op) == "Sense";
    std::vector<Pin> pins;
    if (!pure) pins.push_back(E("In"));
    pins.push_back(EntityPin());
    pins.push_back(EntityPin("Victim"));
    pins.insert(pins.end(), queryPins.begin(), queryPins.end());
    if (pure)
    {
        pins.push_back(S("Tag", "Player"));
        pins.push_back(N("Near"));
    }
    else
    {
        pins.push_back(N("Amount", 25));
        pins.push_back(N("Cooldown", .4));
        pins.push_back(B("FriendlyFire"));
    }
    add("Gameplay." + std::string(op), "Gameplay AI", pins, pure ? std::vector<Pin>{ B("Visible"), EntityPin("Victim"), N("Distance"), V("Position") } : std::vector<Pin>{ E("Then"), B("Success"), N("Applied"), B("Killed"), S("Reason") }, pure);
}
add("PlayAnimation","Animation",{E("In"),EntityPin(),S("Clip"),N("Speed",1)},{E("Then")});
for (auto suffix : { "Float", "Bool", "Trigger" })
{
    Type type = std::string(suffix) == "Float" ? Type::Number : Type::Bool;
    add("Animator.Get" + std::string(suffix), "Animator", { EntityPin(), S("Name") }, { { "Value", type, DefaultValue(type) } }, true);
}
add("Animator.State", "Animator", { EntityPin() }, { S("Name"), N("NormalizedTime"), B("Fading"), N("FadeProgress", 1), V("RootVelocity"), N("TurnRate") }, true);
add("Animator.StateIs", "Animator", { EntityPin(), S("Name") }, { B("Value") }, true);
add("Animator.BlendWeight", "Animator", { EntityPin(), N("Index") }, { N("Value") }, true);
add("Animator.LayerWeight", "Animator", { EntityPin(), S("Name") }, { N("Value") }, true);
add("Animator.SetFloat", "Animator", { E("In"), EntityPin(), S("Name"), N("Value") }, { E("Then"), B("Success") });
add("Animator.SetFloatDamped", "Animator", { E("In"), EntityPin(), S("Name"), N("Value"), N("Damping", .1), N("Delta") }, { E("Then"), B("Success") });
add("Animator.SetBool", "Animator", { E("In"), EntityPin(), S("Name"), B("Value") }, { E("Then"), B("Success") });
for (auto name : { "SetTrigger", "ResetTrigger" }) add("Animator." + std::string(name), "Animator", { E("In"), EntityPin(), S("Name") }, { E("Then"), B("Success") });
add("Animator.SetState", "Animator", { E("In"), EntityPin(), S("Name"), N("Fade", .2) }, { E("Then"), B("Success") });
add("Animator.PlayClip", "Animator", { E("In"), EntityPin(), S("Clip"), N("Speed", 1), B("Loop", true), N("Fade", .2) }, { E("Then"), B("Success") });
add("Animator.PlayLayer", "Animator", { E("In"), EntityPin(), S("Name"), S("Clip"), N("Speed", 1), B("Loop", true), N("Fade", .2) }, { E("Then"), B("Success") });
add("Animator.SetLayerWeight", "Animator", { E("In"), EntityPin(), S("Name"), N("Weight", 1), N("Fade", .2) }, { E("Then"), B("Success") });

add("SendMessage","Entities",{E("In"),EntityPin(),S("Name"),S("Text")},{E("Then")});
add("SetUIText","UI",{E("In"),EntityPin(),S("Text")},{E("Then")});
add("SetSpriteColor","Rendering",{E("In"),EntityPin(),V("Color",4)},{E("Then")});

add("LoadScene","Scenes",{E("In"),S("Path")},{E("Then")});
for(auto name:{"SaveGameState","LoadGameState"})add(name,"Save",{E("In")},{E("Then")});
for(auto type:{Type::Number,Type::Bool,Type::String}){std::string suffix=TypeName(type);Value d=type==Type::Number?Value(0.):type==Type::Bool?Value(false):Value("");add("ReadState."+suffix,"Save",{S("Key"),{"Default",type,d}},{{"Value",type,d}},true);add("WriteState."+suffix,"Save",{E("In"),S("Key"),{"Value",type,d},B("Persistent",true)},{E("Then")});}
for(auto name:{"Raycast2D","Raycast3D"})add(name,"Physics queries",{V("Origin"),V("Direction"),N("Distance",100)},{B("Hit"),EntityPin("Entity"),V("Point"),V("Normal"),N("Distance")},true);
add("SetDestination","Navigation",{E("In"),EntityPin(),V("Position")},{E("Then")});
add("SetWorldDestination", "Navigation", { E("In"), EntityPin(), V("Position") }, { E("Then"), B("Success") });
add("NavRecoveryState", "Navigation", { EntityPin() }, { S("Phase"), N("Retries"), N("StalledSeconds"), N("Cooldown"), B("Exhausted") }, true);
add("RetryNavigation", "Navigation", { E("In"), EntityPin() }, { E("Then"), B("Success") });
add("NavSteeringState", "Navigation", { EntityPin() }, { V("Preferred"), V("Velocity"), N("Neighbors"), B("Blocked"), B("Limited") }, true);
add("NavQueryPending", "Navigation", { EntityPin() }, { B("Value") }, true);
add("NavTraversalState", "Navigation", { EntityPin() }, { B("Pending"), S("Link"), S("Revision"), S("Ticket"), V("Entry"), V("Exit"), S("Status") }, true);
add("CompleteNavTraversal", "Navigation", { E("In"), EntityPin(), S("Link"), S("Revision"), S("Ticket") }, { E("Then"), B("Success") });
add("CancelNavTraversal", "Navigation", { E("In"), EntityPin() }, { E("Then"), B("Success") });

add("StopMoving","Navigation",{E("In"),EntityPin(),B("Immediate",true)},{E("Then")});
for(auto name:{"IsMoving","HasArrived"})add(name,"Navigation",{EntityPin()},{B("Value")},true);
add("RemainingDistance","Navigation",{EntityPin()},{N("Value")},true);
for(auto name:{"PlayAudio","StopAudio","StopAnimation"})add(name,"Media",{E("In"),EntityPin()},{E("Then")});
add("EmitParticles","Rendering",{E("In"),EntityPin(),N("Count",10)},{E("Then")});
c.at("SetScale").Inputs.back().Default=Value::array({1,1,1});
c.at("SetSpriteColor").Inputs.back().Default=Value::array({1,1,1,1});
return c; }();
        return catalog;
    }
    const Node* Graph::Find(uint32_t id) const
    {
        for (auto& n : Nodes)
            if (n.ID == id) return &n;
        return nullptr;
    }
    std::vector<Diagnostic> Graph::Validate() const
    {
        std::vector<Diagnostic> d;
        auto error = [&](uint32_t n, std::string s) { d.push_back({ n, std::move(s) }); };
        if (Nodes.size() > 4096 || Links.size() > 8192 || Variables.size() > 256) error(0, "Graph size limit exceeded");
        auto extra = ValidateFunctions();
        d.insert(d.end(), extra.begin(), extra.end());
        std::map<uint32_t, const Node*> nodeMap;
        std::map<uint32_t, Definition> definitions;
        size_t payload = 0;
        for (const auto& node : Nodes)
        {
            nodeMap[node.ID] = &node;
            definitions[node.ID] = Describe(node);
            for (const auto& [key, value] : node.Inputs) payload += key.size() + ValueBytes(value);
        }
        for (const auto& [name, var] : Variables) payload += name.size() + ValueBytes(var.Default);
        if (payload > 4 * 1024 * 1024) error(0, "Graph literal payload exceeds 4 MiB");
        std::set<std::string> functions;
        for (auto& n : Nodes)
            if (n.Kind == "Function")
            {
                auto it = n.Inputs.find("Name");
                std::string name = it != n.Inputs.end() && it->second.is_string() ? it->second.get<std::string>() : "MyFunction";
                if (name.empty() || !functions.insert(name).second) error(n.ID, "Duplicate or empty function name");
            }
        for (auto& n : Nodes)
            if (n.Kind == "Call")
            {
                auto it = n.Inputs.find("Name");
                std::string name = it != n.Inputs.end() && it->second.is_string() ? it->second.get<std::string>() : "MyFunction";
                if (!functions.contains(name)) error(n.ID, "Function not found: " + name);
            }
        std::set<uint32_t> ids;
        for (auto& n : Nodes)
        {
            if (n.ID == 0 || !ids.insert(n.ID).second) error(n.ID, "Duplicate or zero node ID");
            auto it = Catalog().find(n.Kind);
            if (it == Catalog().end())
            {
                error(n.ID, "Unknown node: " + n.Kind);
                continue;
            }
            if (!std::isfinite(n.X) || !std::isfinite(n.Y)) error(n.ID, "Invalid position");
            for (auto& [key, value] : n.Inputs)
            {
                auto* p = FindPin(definitions.at(n.ID).Inputs, key);
                if (!p || p->Kind == Type::Exec || !Matches(p->Kind, value)) error(n.ID, "Invalid input: " + key);
            }
            if (n.Kind.starts_with("Get.") || n.Kind.starts_with("Set."))
            {
                auto name = n.Inputs.find("Name");
                std::string key = name != n.Inputs.end() && name->second.is_string() ? name->second.get<std::string>() : "";
                auto v = Variables.find(key);
                if (v == Variables.end()) error(n.ID, "Unknown variable: " + key);
                else
                {
                    auto& p = n.Kind.starts_with("Get.") ? definitions.at(n.ID).Outputs.back() : definitions.at(n.ID).Inputs.back();
                    if (v->second.Kind != p.Kind) error(n.ID, "Variable type differs from node");
                }
            }
        }
        for (auto& [name, v] : Variables)
            if (name.empty() || name.size() > 128 || v.Kind == Type::Exec || !Matches(v.Kind, v.Default)) error(0, "Invalid variable: " + name);
        std::set<std::pair<uint32_t, std::string>> inputs, execOutputs;
        std::map<uint32_t, std::vector<uint32_t>> dependencies;
        for (auto& l : Links)
        {
            auto* a = nodeMap.contains(l.From) ? nodeMap.at(l.From) : nullptr;
            auto* b = nodeMap.contains(l.To) ? nodeMap.at(l.To) : nullptr;
            if (!a || !b)
            {
                error(l.To, "Link references missing node");
                continue;
            }
            auto ai = Catalog().find(a->Kind), bi = Catalog().find(b->Kind);
            if (ai == Catalog().end() || bi == Catalog().end()) continue;
            auto* o = FindPin(definitions.at(a->ID).Outputs, l.Output);
            auto* i = FindPin(definitions.at(b->ID).Inputs, l.Input);
            if (!o || !i || o->Kind != i->Kind)
            {
                error(l.To, "Link pin types differ or pin missing");
                continue;
            }
            if (i->Kind != Type::Exec && !inputs.insert({ l.To, l.Input }).second) error(l.To, "Data input already linked");
            if (i->Kind == Type::Exec && !execOutputs.insert({ l.From, l.Output }).second) error(l.From, "Execution output already linked; use Sequence");
            if (l.Input == "Name" && (b->Kind == "Function" || b->Kind == "Call" || b->Kind.starts_with("Function.") || b->Kind == "Event.Custom" || b->Kind.starts_with("Local.") || b->Kind.starts_with("Get.") || b->Kind.starts_with("Set."))) error(l.To, "Variable name must be a literal");
            if (i->Kind != Type::Exec) dependencies[l.To].push_back(l.From);
        }
        std::map<uint32_t, int> color;
        std::function<void(uint32_t, int)> visit = [&](uint32_t id, int depth) {if(depth>128){error(id,"Data nesting exceeds 128");return;}if(color[id]==1){error(id,"Data cycle");return;}if(color[id]==2)return;color[id]=1;for(auto dep:dependencies[id])visit(dep,depth+1);color[id]=2; };
        for (auto& n : Nodes) visit(n.ID, 0);
        return d;
    }
    Value Graph::Serialize() const
    {
        Value j = { { "Format", "Zamin.VisualGraph" }, { "Version", 1 }, { "Nodes", Value::array() }, { "Links", Value::array() }, { "Variables", Value::object() } };
        for (auto& n : Nodes) j["Nodes"].push_back({ { "ID", n.ID }, { "Kind", n.Kind }, { "X", n.X }, { "Y", n.Y }, { "Inputs", n.Inputs }, { "Breakpoint", n.Breakpoint } });
        for (auto& l : Links) j["Links"].push_back({ { "From", l.From }, { "Output", l.Output }, { "To", l.To }, { "Input", l.Input } });
        for (auto& [name, v] : Variables) j["Variables"][name] = { { "Type", TypeName(v.Kind) }, { "Default", v.Default } };
        j["Functions"] = Value::object();
        for (const auto& [name, signature] : Functions)
        {
            Value function = { { "Inputs", Value::array() }, { "Outputs", Value::array() }, { "Locals", Value::object() } };
            for (const auto& p : signature.Inputs) function["Inputs"].push_back({ { "Name", p.Name }, { "Type", TypeName(p.Kind) }, { "Default", p.Default } });
            for (const auto& p : signature.Outputs) function["Outputs"].push_back({ { "Name", p.Name }, { "Type", TypeName(p.Kind) }, { "Default", p.Default } });
            for (const auto& [key, v] : signature.Locals) function["Locals"][key] = { { "Type", TypeName(v.Kind) }, { "Default", v.Default } };
            j["Functions"][name] = std::move(function);
        }
        return j;
    }
    Graph Graph::Parse(const Value& j)
    {
        Require(j.value("Format", "") == "Zamin.VisualGraph" && j.value("Version", 0) == 1, "Unsupported graph format/version");
        Graph g;
        auto readID = [](const Value& v) {Require(v.is_number_integer()&&(!v.is_number_integer()||v.is_number_unsigned()||v.get<int64_t>()>=0),"Node ID must be unsigned integer");uint64_t id=v.get<uint64_t>();Require(id>0&&id<=UINT32_MAX,"Node ID outside 1..UINT32_MAX");return (uint32_t)id; };
        auto& ns = j.at("Nodes");
        auto& ls = j.at("Links");
        auto& vs = j.at("Variables");
        Require(ns.is_array() && ns.size() <= 4096 && ls.is_array() && ls.size() <= 8192 && vs.is_object() && vs.size() <= 256, "Graph size/type limit");
        for (auto& n : ns) g.Nodes.push_back({ readID(n.at("ID")), n.at("Kind").get<std::string>(), n.value("X", 0.f), n.value("Y", 0.f), n.value("Inputs", std::map<std::string, Value>{}), n.value("Breakpoint", false) });
        for (auto& l : ls) g.Links.push_back({ readID(l.at("From")), l.at("Output").get<std::string>(), readID(l.at("To")), l.at("Input").get<std::string>() });
        for (auto it = vs.begin(); it != vs.end(); ++it)
        {
            bool found = false;
            for (int i = 1; i < int(Type::Count); ++i)
                if (it.value().at("Type") == TypeName((Type)i))
                {
                    g.Variables[it.key()] = { (Type)i, it.value().at("Default") };
                    found = true;
                }
            Require(found, "Unknown variable type");
        }
        auto type = [&](const Value& value) { for (int i=1;i<int(Type::Count);++i) if(value==TypeName(Type(i))) return Type(i); throw std::runtime_error("Unknown signature type"); };
        if (j.contains("Functions"))
        {
            const auto& functions = j.at("Functions");
            Require(functions.is_object() && functions.size() <= 64, "Function definition limit/type");
            for (auto it = functions.begin(); it != functions.end(); ++it)
            {
                FunctionSignature signature;
                for (const char* side : { "Inputs", "Outputs" })
                {
                    const auto& pins = it.value().at(side);
                    Require(pins.is_array() && pins.size() <= 32, "Function pin limit/type");
                    for (const auto& pin : pins) (std::string(side) == "Inputs" ? signature.Inputs : signature.Outputs).push_back({ pin.at("Name").get<std::string>(), type(pin.at("Type")), pin.at("Default") });
                }
                const auto locals = it.value().value("Locals", Value::object());
                Require(locals.is_object() && locals.size() <= 32, "Local variable limit/type");
                for (auto v = locals.begin(); v != locals.end(); ++v) signature.Locals[v.key()] = { type(v.value().at("Type")), v.value().at("Default") };
                g.Functions[it.key()] = std::move(signature);
            }
        }
        return g;
    }
    Graph Graph::Load(const std::filesystem::path& p)
    {
        Require(std::filesystem::file_size(p) <= 4 * 1024 * 1024, "Graph exceeds 4 MB");
        std::ifstream f(p);
        Require((bool)f, "Graph file could not be opened");
        Value j = Value::parse(f, [](int depth, Value::parse_event_t, Value&) {Require(depth<=64,"JSON nesting exceeds 64");return true; });
        return Parse(j);
    }
    bool Graph::Save(const std::filesystem::path& p) const
    {
        if (Nodes.size() > 4096 || Links.size() > 8192 || Variables.size() > 256 || Functions.size() > 64) return false;
        auto text = Serialize().dump(2);
        return text.size() <= 4 * 1024 * 1024 && FileSystem::WriteTextFileAtomic(p, text);
    }
    Graph Graph::Example(bool threeD)
    {
        Graph g;
        g.Nodes = { { 1, "Event.Update", 40, 100 }, { 2, "MakeVec3", 40, 350, { { "Y", 0.5 } } }, { 3, "ScaleVec3", 340, 320 }, { 4, "Translate", 650, 100 } };
        g.Links = { { 1, "Then", 4, "In" }, { 1, "Delta", 3, "Scale" }, { 2, "Value", 3, "Vector" }, { 3, "Value", 4, "Vector" } };
        if (!threeD) { g.Nodes[1].Inputs = { { "X", 1.0 } }; }
        return g;
    }
    bool Machine::Load(Graph graph, Bridge bridge, const Value& overrides)
    {
        Stop();
        m_Error.clear();
        m_Trace.clear();
        m_Graph = std::move(graph);
        m_Bridge = std::move(bridge);
        m_Variables.clear();
        auto errors = m_Graph.Validate();
        if (!errors.empty())
        {
            m_Error = "Node " + std::to_string(errors[0].Node) + ": " + errors[0].Message;
            return false;
        }
        m_NodeIndex.clear();
        m_InputLinks.clear();
        m_OutputLinks.clear();
        m_Functions.clear();
        m_Events.clear();
        m_TypedEntries.clear();
        m_Definitions.clear();
        m_CallStack.clear();
        std::map<std::string, std::shared_ptr<const Definition>> descriptions;
        for (size_t i = 0; i < m_Graph.Nodes.size(); ++i)
        {
            auto& n = m_Graph.Nodes[i];
            m_NodeIndex[n.ID] = i;
            std::string descriptionKey = n.Kind;
            std::string functionName = n.Inputs.contains("Name") && n.Inputs.at("Name").is_string() ? n.Inputs.at("Name").get<std::string>() : "MyFunction";
            descriptionKey = n.Kind + (n.Kind.starts_with("Function.") ? ":" + functionName : "");
            if (!descriptions.contains(descriptionKey)) descriptions[descriptionKey] = std::make_shared<Definition>(m_Graph.Describe(n));
            m_Definitions[n.ID] = descriptions.at(descriptionKey);
            if (n.Kind == "Function.Entry") m_TypedEntries[functionName] = n.ID;
            if (n.Kind == "Event.Custom") m_Events["Custom." + (n.Inputs.contains("Name") ? functionName : "MyEvent")].push_back(n.ID);
            else if (n.Kind.starts_with("Event.")) m_Events[n.Kind.substr(6)].push_back(n.ID);
            if (n.Kind == "Function") m_Functions[n.Inputs.contains("Name") ? n.Inputs.at("Name").get<std::string>() : "MyFunction"] = n.ID;
        }
        for (auto& l : m_Graph.Links)
        {
            const auto& def = *m_Definitions.at(l.From);
            auto* pin = FindPin(def.Outputs, l.Output);
            if (pin->Kind == Type::Exec) m_OutputLinks[{ l.From, l.Output }] = l;
            else m_InputLinks[{ l.To, l.Input }] = l;
        }
        for (auto& [name, v] : m_Graph.Variables)
        {
            auto it = overrides.is_object() ? overrides.find(name) : overrides.end();
            m_Variables[name] = it != overrides.end() && Matches(v.Kind, *it) ? *it : v.Default;
        }
        try
        {
            CheckStorage();
        }
        catch (const std::exception& e)
        {
            m_Error = e.what();
            m_Variables.clear();
            return false;
        }
        return true;
    }
    const Definition& Machine::Def(const Node& node) const { return *m_Definitions.at(node.ID); }
    const Node* Machine::Find(uint32_t id) const
    {
        auto it = m_NodeIndex.find(id);
        return it == m_NodeIndex.end() ? nullptr : &m_Graph.Nodes[it->second];
    }
    void Machine::Charge()
    {
        Require(m_Remaining > 0, "Instruction budget exceeded");
        --m_Remaining;
    }
    Value Machine::Input(const Node& n, const std::string& pin, Task& t, std::set<uint32_t>& active)
    {
        auto linked = m_InputLinks.find({ n.ID, pin });
        if (linked != m_InputLinks.end()) return Evaluate(linked->second.From, linked->second.Output, t, active);
        auto it = n.Inputs.find(pin);
        if (it != n.Inputs.end()) return it->second;
        auto* p = FindPin(Def(n).Inputs, pin);
        Require(p, "Unknown input");
        return p->Default;
    }
    Value Machine::Evaluate(uint32_t id, const std::string& output, Task& t, std::set<uint32_t>& active)
    {
        Charge();
        auto* n = Find(id);
        Require(n, "Missing node");
        Require(active.size() < 128 && active.insert(id).second, "Data cycle/depth limit");
        struct Guard
        {
            std::set<uint32_t>& s;
            uint32_t id;
            ~Guard() { s.erase(id); }
        } guard{ active, id };
        const auto& k = n->Kind;
        auto in = [&](const char* p) { return Input(*n, p, t, active); };
        auto num = [&](const char* p) { return in(p).get<double>(); };
        Value r;
        if (k.starts_with("Event."))
        {
            auto it = t.Event.find(output);
            r = it == t.Event.end() ? FindPin(Def(*n).Outputs, output)->Default : *it;
        }
        else if (k == "Function.Entry")
        {
            Require(!t.Calls.empty() && t.Calls.back().Entry == id, "Function parameter used outside its invocation");
            r = t.Calls.back().Arguments.at(output);
        }
        else if (k.starts_with("Local.Get."))
        {
            Require(!t.Calls.empty(), "Local read outside a typed function");
            r = t.Calls.back().Locals.at(in("Name").get<std::string>());
        }
        else if (k.starts_with("Array."))
        {
            auto part = k.substr(6, k.find('.', 6) - 6);
            Value array = part == "Empty" ? Value::array() : in("Array");
            auto index = [&](const char* key, bool end = false) {double value=num(key);Require(std::floor(value)==value && value>=0 && value<=double(array.size()) && (end || value<double(array.size())),"Array index outside range/integer");return size_t(value); };
            if (part == "Empty") r = array;
            else if (part == "Length") r = array.size();
            else if (part == "Get") r = array.at(index("Index"));
            else if (part == "Append")
            {
                Require(array.size() < 1024, "Array element limit");
                array.push_back(in("Value"));
                r = std::move(array);
            }
            else if (part == "Set")
            {
                auto i = index("Index");
                array[i] = in("Value");
                r = std::move(array);
            }
            else if (part == "Remove")
            {
                auto i = index("Index");
                array.erase(array.begin() + i);
                r = std::move(array);
            }
            else if (part == "Slice")
            {
                auto start = index("Start", true);
                double count = num("Count");
                Require(count >= 0 && count <= double(array.size() - start) && std::floor(count) == count, "Array slice count outside range/integer");
                r = Value::array();
                for (size_t i = start; i < start + size_t(count); ++i) r.push_back(array[i]);
            }
            else if (part == "Concat")
            {
                auto other = in("Other");
                Require(array.size() + other.size() <= 1024, "Array element limit");
                for (auto& item : other) array.push_back(item);
                r = std::move(array);
            }
            else
            {
                auto value = in("Value");
                auto it = std::find(array.begin(), array.end(), value);
                r = part == "Contains" ? Value(it != array.end()) : Value(it == array.end() ? -1 : int(it - array.begin()));
            }
        }
        else if (k.starts_with("Constant.")) r = in("Value");
        else if (k.starts_with("Get.")) r = m_Variables.at(in("Name").get<std::string>());
        else if (!Def(*n).Pure)
        {
            auto it = t.Results.find(id);
            Require(it != t.Results.end() && it->second.contains(output), "Execution result used before node ran");
            r = it->second.at(output);
        }
        else if (k == "Add") r = num("A") + num("B");
        else if (k == "Subtract") r = num("A") - num("B");
        else if (k == "Multiply") r = num("A") * num("B");
        else if (k == "Divide")
        {
            double b = num("B");
            Require(b != 0, "Division by zero");
            r = num("A") / b;
        }
        else if (k == "Modulo")
        {
            double b = num("B");
            Require(b != 0, "Modulo by zero");
            r = std::fmod(num("A"), b);
        }
        else if (k == "Power") r = std::pow(num("A"), num("B"));
        else if (k == "Min") r = std::min(num("A"), num("B"));
        else if (k == "Max") r = std::max(num("A"), num("B"));
        else if (k == "Sin") r = std::sin(num("A"));
        else if (k == "Cos") r = std::cos(num("A"));
        else if (k == "Abs") r = std::abs(num("A"));
        else if (k == "Sqrt") r = std::sqrt(num("A"));
        else if (k == "Floor") r = std::floor(num("A"));
        else if (k == "Ceil") r = std::ceil(num("A"));
        else if (k == "Negate") r = -num("A");
        else if (k == "Less") r = num("A") < num("B");
        else if (k == "Greater") r = num("A") > num("B");
        else if (k == "Equal") r = num("A") == num("B");
        else if (k == "And") r = in("A").get<bool>() && in("B").get<bool>();
        else if (k == "Or") r = in("A").get<bool>() || in("B").get<bool>();
        else if (k == "Not") r = !in("A").get<bool>();
        else if (k == "Clamp")
        {
            auto lo = num("Min"), hi = num("Max");
            Require(lo <= hi, "Clamp Min exceeds Max");
            r = std::clamp(num("Value"), lo, hi);
        }
        else if (k == "Lerp")
        {
            double a = num("A");
            r = a + (num("B") - a) * num("T");
        }
        else if (k == "TextEqual") r = in("A") == in("B");
        else if (k == "Concat") r = in("A").get<std::string>() + in("B").get<std::string>();
        else if (k == "NumberToText") r = in("Number").dump();
        else if (k.starts_with("MakeVec"))
        {
            r = Value::array();
            int dim = k.back() - '0';
            for (int i = 0; i < dim; ++i) r.push_back(Input(*n, std::string(1, "XYZW"[i]), t, active));
        }
        else if (k.starts_with("BreakVec"))
        {
            auto v = in("Vector");
            auto index = std::string("XYZW").find(output);
            Require(index < v.size(), "Invalid vector output");
            r = v[index];
        }
        else if (k.starts_with("ScaleVec"))
        {
            r = in("Vector");
            double scale = num("Scale");
            for (auto& x : r) x = x.get<double>() * scale;
        }
        else if (k.starts_with("AddVec"))
        {
            r = in("A");
            auto b = in("B");
            for (size_t i = 0; i < r.size(); ++i) r[i] = r[i].get<double>() + b[i].get<double>();
        }
        else
        {
            Require((bool)m_Bridge, "No engine bridge for " + k);
            std::map<std::string, Value> args;
            for (auto& p : Def(*n).Inputs) args[p.Name] = in(p.Name.c_str());
            r = m_Bridge(k, args);
            if (r.is_object())
            {
                Require(r.contains(output), "Engine bridge output missing");
                r = r.at(output);
            }
        }
        auto* p = FindPin(Def(*n).Outputs, output);
        Require(p && Matches(p->Kind, r) && SafeValue(r), "Invalid result from " + k + "." + output);
        return r;
    }
    void Machine::Push(Task& t, uint32_t from, const std::string& pin)
    {
        auto it = m_OutputLinks.find({ from, pin });
        if (it != m_OutputLinks.end())
        {
            Require(t.Stack.size() < 1024, "Execution stack limit");
            t.Stack.push_back({ it->second.To });
        }
    }
    void Machine::Event(const std::string& name, Value payload)
    {
        if (!m_Error.empty()) return;
        try
        {
            auto event = m_Events.find(name);
            if (event == m_Events.end()) return;
            if (payload.is_null()) payload = Value::object(); // Preserve no-payload host events.
            Require(payload.is_object() && payload.size() <= 32 && ValueBytes(payload) <= 65536, "Event payload limit/type");
            for (const auto& [key, value] : payload.items()) Require(key.size() <= 128 && SafeValue(value), "Invalid event payload");
            for (auto id : event->second)
            {
                Require(m_Tasks.size() < 128, "Pending event limit");
                Task t;
                t.Event = payload;
                t.Stack.push_back({ id });
                m_Tasks.push_back(std::move(t));
            }
            CheckStorage();
            if (!m_Running) Run();
        }
        catch (const std::exception& e)
        {
            m_Error = e.what();
            m_Tasks.clear();
        }
    }
    void Machine::Tick(double seconds)
    {
        if (!std::isfinite(seconds) || seconds < 0 || Paused) return;
        for (auto& t : m_Tasks) t.Wait = std::max(0.0, t.Wait - seconds);
        Run();
    }
    void Machine::Stop()
    {
        m_Tasks.clear();
        m_Active = nullptr;
        Paused = false;
        CurrentNode = 0;
    }
    void Machine::Step()
    {
        Paused = false;
        m_SkipBreakpoint = true;
        Run(true);
        Paused = true;
    }
    void Machine::Continue()
    {
        Paused = false;
        m_SkipBreakpoint = true;
        Run();
    }
    void Machine::Run(bool single)
    {
        if (m_Running || Paused || !m_Error.empty()) return;
        static thread_local size_t depth = 0;
        if (depth >= 32)
        {
            m_Error = "Cross-graph call depth exceeds 32";
            m_Tasks.clear();
            return;
        }
        ++depth;
        struct CallGuard
        {
            size_t& Depth;
            ~CallGuard() { --Depth; }
        } callGuard{ depth };
        m_Running = true;
        m_Remaining = Budget;
        try
        {
            size_t tasks = m_Tasks.size();
            for (size_t i = 0; i < tasks && !Paused; ++i)
            {
                Task t = std::move(m_Tasks.front());
                m_Tasks.pop_front();
                m_Active = &t;
                CaptureCallStack(t);
                struct ActiveGuard
                {
                    Task*& Pointer;
                    ~ActiveGuard() { Pointer = nullptr; }
                } activeGuard{ m_Active };
                while (!t.Stack.empty() && t.Wait <= 0 && !Paused)
                {
                    Frame f = t.Stack.back();
                    if (f.ReturnCall)
                    {
                        t.Stack.pop_back();
                        Require(!t.Calls.empty(), "Missing function frame");
                        Require(m_Graph.Functions.at(t.Calls.back().Name).Outputs.empty(), "Function ended without Return");
                        ReturnFunction(t, Value::object());
                        continue;
                    }
                    auto* n = Find(f.Node);
                    Require(n, "Missing execution node");
                    CurrentNode = f.Node;
                    if (Debug && n->Breakpoint && !m_SkipBreakpoint)
                    {
                        Paused = true;
                        break;
                    }
                    m_SkipBreakpoint = false;
                    Charge();
                    t.Stack.pop_back();
                    m_Trace.push_back(n->ID);
                    if (m_Trace.size() > 128) m_Trace.pop_front();
                    std::set<uint32_t> active;
                    auto in = [&](const char* p) { return Input(*n, p, t, active); };
                    const auto& k = n->Kind;
                    if (k == "Function.Call") TypedCall(t, *n);
                    else if (k == "Function.Return")
                    {
                        Require(!t.Calls.empty() && in("Name") == t.Calls.back().Name, "Return outside its function");
                        Value values = Value::object();
                        for (const auto& pin : m_Graph.Functions.at(t.Calls.back().Name).Outputs) values[pin.Name] = in(pin.Name.c_str());
                        ReturnFunction(t, std::move(values));
                    }
                    else if (k.starts_with("Local.Set."))
                    {
                        Require(!t.Calls.empty(), "Local write outside a typed function");
                        auto name = in("Name").get<std::string>();
                        auto value = in("Value");
                        Require(Matches(m_Graph.Functions.at(t.Calls.back().Name).Locals.at(name).Kind, value), "Local variable type mismatch");
                        t.Calls.back().Locals.at(name) = std::move(value);
                        CheckStorage();
                        Push(t, n->ID, "Then");
                    }
                    else if (k == "EmitEvent")
                    {
                        auto name = in("Name").get<std::string>();
                        Require(!name.empty() && name.size() <= 128, "Invalid custom event name");
                        Value payload = Value::object();
                        for (const char* pin : { "Number", "Text", "Bool", "Vector", "Other" }) payload[pin] = in(pin);
                        Event("Custom." + name, std::move(payload));
                        Require(m_Error.empty(), m_Error);
                        Push(t, n->ID, "Then");
                    }
                    else if (k.starts_with("ForEach."))
                    {
                        int index = f.Iteration;
                        if (index < 0)
                        {
                            auto array = in("Array");
                            t.Results[n->ID] = { { "__Items", array }, { "Index", -1 }, { "Value", Def(*n).Outputs.back().Default } };
                            index = 0;
                            CheckStorage();
                        }
                        auto& result = t.Results.at(n->ID);
                        auto& items = result.at("__Items");
                        if (size_t(index) < items.size())
                        {
                            result["Index"] = index;
                            result["Value"] = items[size_t(index)];
                            Require(t.Stack.size() < 1024, "Loop stack limit");
                            t.Stack.push_back({ n->ID, index + 1 });
                            Push(t, n->ID, "Body");
                        }
                        else
                        {
                            result.erase("__Items");
                            Push(t, n->ID, "Completed");
                        }
                    }
                    else if (k == "For")
                    {
                        int count = f.Count, index = f.Iteration;
                        if (index < 0)
                        {
                            double v = in("Count").get<double>();
                            Require(v >= 0 && v <= 100000 && std::floor(v) == v, "For Count must be an integer 0..100000");
                            count = (int)v;
                            index = 0;
                        }
                        if (index < count)
                        {
                            const bool fresh = !t.Results.contains(n->ID);
                            t.Results[n->ID] = { { "Index", index } };
                            if (fresh) CheckStorage();
                            t.Stack.push_back({ n->ID, index + 1, count });
                            Push(t, n->ID, "Body");
                        }
                        else Push(t, n->ID, "Completed");
                    }
                    else if (k == "While")
                    {
                        if (in("Condition").get<bool>())
                        {
                            Require(t.Stack.size() < 1024, "Loop stack limit");
                            t.Stack.push_back({ n->ID });
                            Push(t, n->ID, "Body");
                        }
                        else Push(t, n->ID, "Completed");
                    }
                    else if (k == "Break")
                    {
                        bool found = false;
                        while (!t.Stack.empty())
                        {
                            auto frame = t.Stack.back();
                            t.Stack.pop_back();
                            Require(!frame.ReturnCall, "Break cannot cross a function boundary");
                            auto* loop = Find(frame.Node);
                            if (loop && (loop->Kind == "For" || loop->Kind == "While" || loop->Kind.starts_with("ForEach.")))
                            {
                                if (loop->Kind.starts_with("ForEach.")) t.Results[loop->ID].erase("__Items");
                                Push(t, loop->ID, "Completed");
                                found = true;
                                break;
                            }
                        }
                        Require(found, "Break used outside a loop body");
                    }
                    else if (k == "Sequence")
                    {
                        Push(t, n->ID, "Third");
                        Push(t, n->ID, "Second");
                        Push(t, n->ID, "First");
                    }
                    else if (k == "Branch") Push(t, n->ID, in("Condition").get<bool>() ? "True" : "False");
                    else if (k == "Delay")
                    {
                        double v = in("Seconds").get<double>();
                        Require(std::isfinite(v) && v >= 0 && v <= 86400, "Delay must be 0..86400 seconds");
                        Push(t, n->ID, "Then");
                        t.Wait = v;
                    }
                    else if (k.starts_with("Set."))
                    {
                        std::string name = in("Name").get<std::string>();
                        Value value = in("Value");
                        Require(Matches(m_Graph.Variables.at(name).Kind, value), "Variable type mismatch");
                        m_Variables.at(name) = value;
                        CheckStorage();
                        Push(t, n->ID, "Then");
                    }
                    else if (k == "Call")
                    {
                        auto name = in("Name");
                        Push(t, n->ID, "Then");
                        Require(t.Stack.size() < 1024, "Function call stack limit");
                        t.Stack.push_back({ m_Functions.at(name.get<std::string>()) });
                    }
                    else if (k == "Function" || k == "Function.Entry" || k.starts_with("Event.")) Push(t, n->ID, "Then");
                    else
                    {
                        Require(!Def(*n).Pure, "Pure node in execution chain");
                        Require((bool)m_Bridge, "No engine bridge for " + k);
                        std::map<std::string, Value> args;
                        for (auto& p : Def(*n).Inputs)
                            if (p.Kind != Type::Exec) args[p.Name] = in(p.Name.c_str());
                        Value result = m_Bridge(k, args);
                        if (k == "SetWorldDestination" || k == "CompleteNavTraversal" || k == "CancelNavTraversal" || k == "RetryNavigation")
                        {
                            Require(Matches(Type::Bool, result), "Invalid navigation operation result");
                            t.Results[n->ID] = { { "Success", result } };
                        }
                        if (k.starts_with("Animator.") || k.starts_with("Gameplay."))
                        {
                            Require(result.is_object(), "Invalid Animator operation result");
                            for (const auto& pin : Def(*n).Outputs)
                                if (pin.Kind != Type::Exec) Require(result.contains(pin.Name) && Matches(pin.Kind, result.at(pin.Name)), "Invalid Animator result pin");
                            t.Results[n->ID] = std::move(result);
                        }
                        if (k == "SpawnPrefab")
                        {
                            Require(Matches(Type::Entity, result), "Invalid spawned entity");
                            t.Results[n->ID] = { { "Value", result } };
                        }
                        CheckStorage();
                        Push(t, n->ID, "Then");
                    }
                    if (single)
                    {
                        Paused = true;
                        break;
                    }
                }
                if (!t.Stack.empty())
                {
                    Require(m_Tasks.size() < 128, "Pending task limit");
                    m_Tasks.push_front(std::move(t)); // preserve the paused task for Step
                    if (!Paused) { std::rotate(m_Tasks.begin(), m_Tasks.begin() + 1, m_Tasks.end()); }
                }
            }
        }
        catch (const std::exception& e)
        {
            m_Error = "Node " + std::to_string(CurrentNode) + ": " + e.what();
            m_Tasks.clear();
        }
        m_Running = false;
    }
}
