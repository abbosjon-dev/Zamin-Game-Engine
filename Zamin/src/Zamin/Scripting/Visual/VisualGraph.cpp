#include "VisualGraph.h"
#include <cstdio>
#include "Zamin/Utils/FileSystem.h"
#include <algorithm>
#include <chrono>
#include <optional>
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
        Pin En(const char* n) { return { n, Type::Enum, Value(0) }; }
        Pin St(const char* n) { return { n, Type::Struct, Value::object() }; }
        Pin Cu(const char* n) { return { n, Type::Curve, DefaultValue(Type::Curve) }; }
        Pin EntityPin(const char* n = "Target") { return { n, Type::Entity, std::string(n) == "Target" ? UINT64_MAX : 0ull }; }
        const Pin* FindPin(const std::vector<Pin>& pins, std::string_view name)
        {
            for (auto& p : pins)
                if (p.Name == name) return &p;
            return nullptr;
        }
        void Require(bool ok, const std::string& message)
        {
            if (!ok) throw std::runtime_error(message);
        }
        // A literal message makes no string unless the check fails (the VM checks every step).
        void Require(bool ok, const char* message)
        {
            if (!ok) throw std::runtime_error(message);
        }
        bool SafeValue(const Value& v)
        {
            if (v.is_string()) return v.get_ref<const std::string&>().size() <= 65536;
            if (ValueBytes(v) > 65536) return false;
            if (v.is_number()) return std::isfinite(v.get<double>());
            if (v.is_string() || v.is_boolean() || v.is_null()) return true;
            if (v.is_object()) return v.size() <= 32 && std::all_of(v.begin(), v.end(), [](const auto& item) { return SafeValue(item); }); // a struct
            if (!v.is_array() || v.size() > 1024) return false;
            return std::all_of(v.begin(), v.end(), [](const auto& item) { return SafeValue(item); });
        }
    }
    const char* TypeName(Type t)
    {
        static const char* names[] = { "Exec", "Bool", "Number", "String", "Vec2", "Vec3", "Vec4", "Entity", "Enum", "Struct", "ArrayBool", "ArrayNumber", "ArrayString", "ArrayVec2", "ArrayVec3", "ArrayVec4", "ArrayEntity", "ArrayEnum", "ArrayStruct", "Curve" };
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
            case Type::Struct: return v.is_object() && v.size() <= 32 && ValueBytes(v) <= 65536;
            case Type::Curve:  return ValueBytes(v) <= 65536 && IsCurve(v);
            case Type::Enum:   return v.is_number_integer() && !v.is_number_unsigned() ? std::abs(v.get<int64_t>()) <= (int64_t(1) << 53) : v.is_number_unsigned() && v.get<uint64_t>() <= (uint64_t(1) << 53);
            default:
                if (!v.is_array() || v.size() != size_t(int(t) - int(Type::Vec2) + 2)) return false;
                return std::all_of(v.begin(), v.end(), [](const auto& item) { return Matches(Type::Number, item); });
        }
    }

    bool SameType(const Pin& a, const Pin& b) { return a.Kind == b.Kind && (!IsNamed(a.Kind) || a.Enum == b.Enum); }

    const std::vector<const char*>* PinChoices(const std::string& kind, const std::string& pin)
    {
        static const std::vector<const char*> payload{ "Zarar", "Davolash", "Qalqon" };
        static const std::vector<const char*> ability{ "Zarar", "Davolash", "Qalqon", "Holat effekti" };
        static const std::vector<const char*> tick{ "Tik yo'q", "Zarar", "Davolash", "Qalqon" };
        static const std::vector<const char*> stacking{ "Yangilash", "Uzaytirish", "Mustaqil", "Eng kuchlisi" };
        static const std::vector<const char*> falloff{ "Kamaymaydi", "Chiziqli kamayish" };
        static const std::vector<const char*> hud{ "Sog'liq", "Qalqon", "Buyum", "Sumka", "O'limlar", "Effekt", "Zarba belgisi", "Energiya", "Qobiliyat", "Tajriba darajasi" };
        static const std::vector<const char*> slot{ "Qurol", "Qo'shimcha qo'l", "Badan", "Tumor" };
        static const std::vector<const char*> curveMode{ "Konstant", "Chiziqli", "Kubik" };
        static const std::map<std::pair<std::string, std::string>, const std::vector<const char*>*> choices{
            { { "Gameplay.AbilityDefine", "Kind" }, &ability },
            { { "Gameplay.ProjectileDefine", "Kind" }, &payload },
            { { "Gameplay.AreaDefine", "Kind" }, &payload },
            { { "Gameplay.AreaDefine", "Falloff" }, &falloff },
            { { "Gameplay.EffectDefine", "Tick" }, &tick },
            { { "Gameplay.EffectDefine", "Stacking" }, &stacking },
            { { "Gameplay.UIBindHUD", "Kind" }, &hud },
            { { "Gameplay.ItemDefine", "Slot" }, &slot },
            { { "Gameplay.EquipmentUnequip", "Slot" }, &slot },
            { { "Gameplay.EquipmentState", "Slot" }, &slot },
            { { "Curve.AddKey", "Mode" }, &curveMode },
        };
        auto found = choices.find({ kind, pin });
        return found == choices.end() ? nullptr : found->second;
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
    // An Enum/Struct local names its enum/struct (empty: the type of the local with that name).
    const bool structured = type == Type::Struct || type == Type::ArrayStruct;
    const bool enumerated = type == Type::Enum || type == Type::ArrayEnum;
    std::vector<Pin> getInputs{ S("Name") }, setInputs{ E("In"), S("Name") };
    if (enumerated || structured)
    {
        getInputs.push_back(S(structured ? "Struct" : "Enum"));
        setInputs.push_back(S(structured ? "Struct" : "Enum"));
    }
    // Struct/enum array nodes name their struct/enum in a literal Struct/Enum input (after In, before the data pins).
    auto named = [&](std::vector<Pin> pins) {
        if (structured || enumerated) pins.insert(pins.begin() + (!pins.empty() && pins.front().Kind == Type::Exec ? 1 : 0), S(structured ? "Struct" : "Enum"));
        return pins;
    };
    setInputs.push_back({ "Value", type, value });
    add("Local.Get." + name, "Function locals", getInputs, { { "Value", type, value } }, true);
    add("Local.Set." + name, "Function locals", setInputs, { E("Then") });
    if (IsArray(type))
    {
        auto element = ElementType(type);
        std::string suffix = TypeName(element);
        auto item = DefaultValue(element);
        add("Constant." + name, "Collections", named({ { "Value", type, value } }), { { "Value", type, value } }, true);
        add("Get." + name, "Variables", getInputs, { { "Value", type, value } }, true);
        add("Set." + name, "Variables", named({ E("In"), S("Name"), { "Value", type, value } }), { E("Then") });
        add("Array.Empty." + suffix, "Collections", named({}), { { "Value", type, value } }, true);
        add("Array.Append." + suffix, "Collections", named({ { "Array", type, value }, { "Value", element, item } }), { { "Value", type, value } }, true);
        add("Array.Set." + suffix, "Collections", named({ { "Array", type, value }, N("Index"), { "Value", element, item } }), { { "Value", type, value } }, true);
        add("Array.Remove." + suffix, "Collections", named({ { "Array", type, value }, N("Index") }), { { "Value", type, value } }, true);
        add("Array.Get." + suffix, "Collections", named({ { "Array", type, value }, N("Index") }), { { "Value", element, item } }, true);
        add("Array.Length." + suffix, "Collections", named({ { "Array", type, value } }), { N("Value") }, true);
        add("Array.Contains." + suffix, "Collections", named({ { "Array", type, value }, { "Value", element, item } }), { B("Value") }, true);
        add("Array.Find." + suffix, "Collections", named({ { "Array", type, value }, { "Value", element, item } }), { N("Value", -1) }, true);
        add("Array.Slice." + suffix, "Collections", named({ { "Array", type, value }, N("Start"), N("Count") }), { { "Value", type, value } }, true);
        add("Array.Concat." + suffix, "Collections", named({ { "Array", type, value }, { "Other", type, value } }), { { "Value", type, value } }, true);
        add("ForEach." + suffix, "Collections", named({ E("In"), { "Array", type, value } }), { E("Body"), E("Completed"), N("Index", -1), { "Value", element, item } });
    }
}
// Enums: every node names its enum in the literal Enum input (Get/Set.Enum may leave it empty: the variable's enum).
add("Constant.Enum", "Enums", { S("Enum"), En("Value") }, { En("Value") }, true);
add("Get.Enum", "Variables", { S("Name"), S("Enum") }, { En("Value") }, true);
add("Set.Enum", "Variables", { E("In"), S("Name"), S("Enum"), En("Value") }, { E("Then") });
add("Enum.Equal", "Enums", { S("Enum"), En("A"), En("B") }, { B("Value") }, true);
add("Enum.ToNumber", "Enums", { S("Enum"), En("Value") }, { N("Value") }, true);
add("Enum.ToText", "Enums", { S("Enum"), En("Value") }, { S("Value") }, true);
add("Enum.FromNumber", "Enums", { S("Enum"), N("Number") }, { En("Value") }, true);
add("Enum.FromText", "Enums", { S("Enum"), S("Text") }, { En("Value") }, true);
// One execution output per entry, named after it (Graph::Describe).
add("Enum.Switch", "Enums", { E("In"), S("Enum"), En("Value") }, {});
// Structs: every node names its struct in the literal Struct input (Get/Set.Struct may leave it empty: the variable's
// struct). Make/Break take one pin per field, GetField/SetField the field named in the literal Field (Graph::Describe).
add("Get.Struct", "Variables", { S("Name"), S("Struct") }, { St("Value") }, true);
add("Set.Struct", "Variables", { E("In"), S("Name"), S("Struct"), St("Value") }, { E("Then") });
add("Struct.Make", "Structs", { S("Struct") }, { St("Value") }, true);
add("Struct.Break", "Structs", { S("Struct"), St("Value") }, {}, true);
add("Struct.GetField", "Structs", { S("Struct"), S("Field"), St("Value") }, {}, true);
add("Struct.SetField", "Structs", { S("Struct"), S("Field"), St("Value") }, { St("Value") }, true);
// Inline macros: an instance takes the macro's pins after its Name literal; Entry/Exit live in the macro's body and
// give/take the macro's inputs/outputs (Graph::Describe). Instances are expanded before the VM runs (Graph::Expand).
add("Macro.Instance", "Macros", { S("Name", "MyMacro") }, {});
add("Macro.Entry", "Macros", {}, {});
add("Macro.Exit", "Macros", {}, {});
// Curves: a float curve value evaluated at a time; built from literals (Constant.Curve) or key by key (AddKey).
add("Constant.Curve", "Curves", { Cu("Value") }, { Cu("Value") }, true);
add("Get.Curve", "Variables", { S("Name") }, { Cu("Value") }, true);
add("Set.Curve", "Variables", { E("In"), S("Name"), Cu("Value") }, { E("Then") });
add("Curve.Evaluate", "Curves", { Cu("Curve"), N("Time") }, { N("Value") }, true);
add("Curve.KeyCount", "Curves", { Cu("Curve") }, { N("Value") }, true);
add("Curve.Length", "Curves", { Cu("Curve") }, { N("Value"), N("Start"), N("End") }, true);
// Mode: 0 Constant, 1 Linear, 2 Cubic (PinChoices); a key at the same time is replaced.
add("Curve.AddKey", "Curves", { Cu("Curve"), N("Time"), N("Value"), N("Mode", 1), N("InTangent"), N("OutTangent") }, { Cu("Value") }, true);
// Inheritance: Parent.Call runs the parent's version of the overridden function Name (its pins: the signature);
// Parent.Event runs the parent's handlers of the overridden event Name ("Start", "Custom.MyEvent" ...).
add("Parent.Call", "Inheritance", { E("In"), S("Name", "MyFunction") }, { E("Then") });
add("Parent.Event", "Inheritance", { E("In"), S("Name", "Start") }, { E("Then") });
// Native methods (format 6): a reflected C++ method or free function named in the literal Method ("Class.Method",
// "Library.Function"); its Target (instance methods), parameters and result come from the graph's signature of it
// (Graph::Natives, Graph::Describe).
add("Native.Call", "C++ methods", { E("In"), S("Method") }, { E("Then") });
add("Native.Pure", "C++ methods", { S("Method") }, {}, true);
add("ReadField.Enum", "C++ bridge", { EntityPin(), S("Name"), S("Enum") }, { En("Value") }, true);
add("WriteField.Enum", "C++ bridge", { E("In"), EntityPin(), S("Name"), S("Enum"), En("Value") }, { E("Then") });
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
add("FormatNumber","Text",{N("Number"),N("Decimals")},{S("Value")},true);
add("FormatTime","Text",{N("Seconds")},{S("Value")},true);
for(int dim=2;dim<=4;++dim){std::vector<Pin> p;for(int i=0;i<dim;++i)p.push_back(N(std::string(1,"XYZW"[i]).c_str()));add("MakeVec"+std::to_string(dim),"Vectors",p,{V("Value",dim)},true);add("BreakVec"+std::to_string(dim),"Vectors",{V("Vector",dim)},p,true);add("ScaleVec"+std::to_string(dim),"Vectors",{V("Vector",dim),N("Scale",1)},{V("Value",dim)},true);add("AddVec"+std::to_string(dim),"Vectors",{V("A",dim),V("B",dim)},{V("Value",dim)},true);
 add("SubtractVec"+std::to_string(dim),"Vectors",{V("A",dim),V("B",dim)},{V("Value",dim)},true);add("DotVec"+std::to_string(dim),"Vectors",{V("A",dim),V("B",dim)},{N("Value")},true);add("DistanceVec"+std::to_string(dim),"Vectors",{V("A",dim),V("B",dim)},{N("Value")},true);
 add("LengthVec"+std::to_string(dim),"Vectors",{V("Vector",dim)},{N("Value")},true);add("NormalizeVec"+std::to_string(dim),"Vectors",{V("Vector",dim)},{V("Value",dim)},true);}
add("Atan2","Math",{N("Y"),N("X",1)},{N("Value")},true);
add("LerpAngle","Math",{N("A"),N("B"),N("T",0.5)},{N("Value")},true);
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
add("Gameplay.Steer", "Gameplay motion", { EntityPin(), V("Goal"), N("Arrive", .3), N("Slow", 1), B("TwoD"), EntityPin("Camera") }, { V("Move", 2), N("Distance"), B("Arrived"), N("Yaw") }, true);
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
add("Gameplay.AbilityDefine", "Gameplay abilities", { E("In"), EntityPin(), S("Ability", "Bolt"), N("Kind"), N("Amount", 20), N("Cost", 10), N("Cooldown", 1), N("Range", 5), S("Ingredient"), N("Count", 1), B("Unlocked", true), S("Projectile"), S("Area"), S("Effect") }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.AbilityCast", "Gameplay abilities", { E("In"), EntityPin(), EntityPin("Victim"), S("Ability", "Bolt"), B("TwoD"), B("LineOfSight", true), N("Mask", 65535), V("Direction"), V("Origin") }, { E("Then"), B("Success"), N("Applied"), EntityPin("Victim"), N("Projectile"), S("Reason") });
add("Gameplay.AbilityUnlock", "Gameplay abilities", { E("In"), EntityPin(), S("Ability", "Bolt"), B("Unlocked", true) }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.AbilityState", "Gameplay abilities", { EntityPin(), S("Ability", "Bolt") }, { B("Configured"), B("Unlocked"), B("Ready"), N("Remaining"), N("Fraction"), N("Uses"), N("Cost") }, true);
add("Gameplay.LoadoutRefill", "Gameplay loadout", { E("In"), EntityPin(), N("Amount", 25) }, { E("Then"), N("Applied") });
add("Gameplay.LoadoutEvent", "Gameplay loadout", { E("In"), EntityPin() }, { E("Then"), B("Has"), S("Kind"), S("Key"), N("Amount"), N("Dropped") });
add("Gameplay.ProjectileDefine", "Gameplay projectiles", { E("In"), EntityPin(), S("Projectile", "Bolt"), N("Kind"), N("Speed", 12), N("Lifetime", 2), N("MaxDistance"), N("Radius", .1), N("TargetRadius", .45), V("TargetOffset"), N("Damage", 10), N("Pierce"), V("Gravity"), N("Homing"), B("TwoD"), B("FriendlyFire"), B("PierceShield"), B("IgnoreGrace"), B("DetonateOnExpire"), N("BlockMask", 65535), S("Effect"), S("Area"), S("Prefab"), N("Pool", 8) }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.ProjectileFire", "Gameplay projectiles", { E("In"), EntityPin(), S("Projectile", "Bolt"), V("Origin"), V("Direction"), EntityPin("Victim"), N("Damage", -1) }, { E("Then"), B("Success"), N("Projectile"), S("Reason") });
add("Gameplay.ProjectileState", "Gameplay projectiles", { N("Projectile") }, { B("Alive"), S("Key"), V("Position"), V("Velocity"), N("Age"), N("Hits"), N("Live"), N("Capacity", 128) }, true);
add("Gameplay.ProjectileCancel", "Gameplay projectiles", { E("In"), N("Projectile"), B("Detonate") }, { E("Then"), B("Success") });
add("Gameplay.CombatEvent", "Gameplay events", { E("In"), EntityPin() }, { E("Then"), B("Has"), S("Kind"), S("Key"), EntityPin("Source"), EntityPin("Subject"), N("Amount"), V("Position"), N("Dropped") });
add("Gameplay.AreaDefine", "Gameplay area", { E("In"), EntityPin(), S("Area", "Blast"), N("Kind"), N("Amount", 10), N("Radius", 3), N("Falloff"), N("EdgeScale", .5), N("MaxTargets", 8), V("TargetOffset"), B("TwoD"), B("LineOfSight", true), N("Mask", 65535), B("AffectOwner"), B("FriendlyFire"), B("PierceShield"), B("IgnoreGrace"), S("Effect") }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.AreaApply", "Gameplay area", { E("In"), EntityPin(), S("Area", "Blast"), V("Center"), EntityPin("Victim"), N("Magnitude", -1) }, { E("Then"), B("Success"), N("Affected"), N("Applied"), S("Reason") });
add("Gameplay.EffectDefine", "Gameplay effects", { E("In"), EntityPin(), S("Effect", "Burn"), N("Duration", 3), N("Period"), N("Tick"), N("TickAmount"), { "Modifiers", Type::ArrayNumber, Value::array({ 0, 0, 0, 0, 0 }) }, N("Speed", 1), B("Stun"), B("Hostile", true), B("PierceShield"), N("Stacking"), N("MaxStacks", 1) }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.EffectApply", "Gameplay effects", { E("In"), EntityPin(), EntityPin("Source"), S("Effect", "Burn"), N("Magnitude", -1) }, { E("Then"), B("Success"), S("Outcome"), N("Stacks"), N("Remaining"), S("Reason") });
add("Gameplay.EffectRemove", "Gameplay effects", { E("In"), EntityPin(), S("Effect") }, { E("Then"), N("Removed") });
add("Gameplay.EffectState", "Gameplay effects", { EntityPin(), S("Effect", "Burn") }, { B("Active"), N("Stacks"), N("Instances"), N("Remaining"), N("Fraction"), N("Magnitude") }, true);
add("Gameplay.CombatModifiers", "Gameplay effects", { EntityPin() }, { B("Stunned"), N("Speed", 1), N("Attack"), N("Armor"), N("Power"), N("Maximum"), N("Regen"), N("Effects") }, true);
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
add("Gameplay.UICombatFeed", "Gameplay UI", { E("In"), EntityPin(), EntityPin("Actor"), S("Name", "Feed"), { "Rows", Type::ArrayEntity, Value::array() }, B("Outgoing", true), B("Incoming"), N("Lifetime", 1.2) }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.UICombatFeedRemove", "Gameplay UI", { E("In"), EntityPin(), S("Name", "Feed") }, { E("Then"), B("Success"), S("Reason") });
for (auto op : { "Bind", "Unbind" }) add("Gameplay.Save" + std::string(op), "Gameplay save", { E("In"), EntityPin(), S("Key", "Player") }, { E("Then"), B("Success"), S("Reason") });
add("Gameplay.SaveFind", "Gameplay save", { EntityPin(), S("Key", "Player") }, { B("Found"), B("Active"), EntityPin("Entity") }, true);
add("Gameplay.SaveSpawn", "Gameplay save", { E("In"), EntityPin(), S("Key", "NPC"), S("Prefab"), V("Position") }, { E("Then"), B("Success"), EntityPin("Entity"), S("Reason") });
for (auto op : { "Write", "Load", "Inspect" })
{
    std::vector<Pin> pins = { E("In"), EntityPin(), S("Slot", "Quick") };
    if (std::string(op) != "Inspect") pins.push_back(S("Schema", "V1"));
    add("Gameplay.Save" + std::string(op), "Gameplay save", pins, { E("Then"), B("Success"), B("Recovered"), N("Entities"), S("Reason") });
}
for (auto op : { "Write", "Load", "Inspect" })
{
    std::vector<Pin> pins = { E("In") };
    if (std::string(op) != "Inspect") pins.push_back(S("Key", "Hero"));
    pins.push_back(S("Slot", "Carry"));
    if (std::string(op) != "Inspect") pins.push_back(S("Schema", "V1"));
    add("Gameplay.Transfer" + std::string(op), "Gameplay save", pins, { E("Then"), B("Success"), B("Recovered"), S("Reason") });
}
add("Gameplay.AIConfigure", "Gameplay AI", { E("In"), EntityPin(), B("TwoD"), S("Tag", "Player"), EntityPin("Victim"), { "Points", Type::ArrayVec3, Value::array() }, B("Relative", true), B("PingPong", true), N("View", 10), N("Lose", 14), N("Near", 2), N("FieldOfView", 180), N("Memory", 2), N("Leash", 20), N("AttackEnter", 1.5), N("AttackExit", 1.8), N("Damage", 10), N("AttackCooldown", .5), B("Strike", true), N("Wait", .5), N("Arrive", .25), N("Repath", .3), N("Speed", 3), N("Stall", .75), N("RecoveryCooldown", .5), N("Retries", 3), { "Eye", Type::Vec3, Value::array({ 0., 1., 0. }) }, { "Facing", Type::Vec3, Value::array({ 1., 0., 0. }) }, N("Mask", 65535), B("Enabled", true), B("Reset") }, { E("Then"), B("Success"), S("Reason") });
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
add("Animator.PlayLayer", "Animator", { E("In"), EntityPin(), S("Name"), S("Clip"), N("Speed", 1), B("Loop", true), N("Fade", .2), B("Restart") }, { E("Then"), B("Success") });
add("Animator.SetLayerWeight", "Animator", { E("In"), EntityPin(), S("Name"), N("Weight", 1), N("Fade", .2) }, { E("Then"), B("Success") });

add("SendMessage","Entities",{E("In"),EntityPin(),S("Name"),S("Text")},{E("Then")});
add("SetUIText","UI",{E("In"),EntityPin(),S("Text")},{E("Then")});
add("SetSpriteColor","Rendering",{E("In"),EntityPin(),V("Color",4)},{E("Then")});

add("LoadScene","Scenes",{E("In"),S("Path"),N("FadeOut"),N("FadeIn",-1),V("Color",4)},{E("Then")});
add("QuitGame","Scenes",{E("In")},{E("Then")});
add("App.DemoMode","Scenes",{},{B("Value")},true);
add("Audio.SetGroupVolume","Audio",{E("In"),S("Group","Music"),N("Volume",1)},{E("Then"),B("Success")});
add("Audio.GroupVolume","Audio",{S("Group","Music")},{N("Value",1)},true);
add("FadeAudio","Audio",{E("In"),EntityPin(),N("Volume",1),N("Seconds",.5)},{E("Then"),B("Success")});
add("Localization.SetLanguage","Localization",{E("In"),S("Code","uz")},{E("Then"),B("Success")});
add("Localization.Language","Localization",{},{S("Value")},true);
add("Localization.Text","Localization",{S("Key"),S("Fallback")},{S("Value")},true);
add("Display.SetFullscreen","Display",{E("In"),B("Fullscreen",true)},{E("Then")});
add("Display.IsFullscreen","Display",{},{B("Value")},true);
add("Localization.SaveSettings","Localization",{E("In")},{E("Then"),B("Success")});
add("Display.SaveSettings","Display",{E("In")},{E("Then"),B("Success")});
add("Display.SetUIScale","Display",{E("In"),N("Scale",1)},{E("Then"),B("Success")});
add("Display.UIScale","Display",{},{N("Value",1)},true);
add("UIValue","UI",{EntityPin()},{B("Found"),N("Value"),B("On")},true);
add("SetUIValue","UI",{E("In"),EntityPin(),N("Value"),B("Notify")},{E("Then"),B("Success")});
add("PlaySpriteClip","Rendering",{E("In"),EntityPin(),S("Clip"),B("Restart")},{E("Then"),B("Success")});
add("ShakeCamera","Rendering",{E("In"),N("Intensity",.3),N("Duration",.4),N("Frequency",20)},{E("Then"),B("Success")});
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
c.at("LoadScene").Inputs.back().Default=Value::array({0,0,0,1});
return c; }();
        return catalog;
    }
    const Node* Graph::Find(uint32_t id) const
    {
        for (auto& n : Nodes)
            if (n.ID == id) return &n;
        return nullptr;
    }
    std::vector<Diagnostic> Graph::ValidateTypes() const
    {
        std::vector<Diagnostic> d;
        auto error = [&](uint32_t n, std::string s) { d.push_back({ n, std::move(s) }); };
        if (Enums.size() > 64) error(0, "Enum definition limit exceeds 64");
        for (const auto& [name, definition] : Enums)
        {
            if (name.empty() || name.size() > 64) error(0, "Invalid enum name");
            if (definition.Entries.empty() || definition.Entries.size() > 256) error(0, "Enum " + name + " needs 1..256 entries");
            if (definition.Source.size() > 256) error(0, "Enum " + name + " source is too long");
            std::set<std::string> names;
            std::set<int64_t> values;
            for (const auto& entry : definition.Entries)
            {
                if (entry.Name.empty() || entry.Name.size() > 64 || !names.insert(entry.Name).second) error(0, "Enum " + name + " has an empty, long or duplicate entry name");
                if (!values.insert(entry.Value).second || std::abs(entry.Value) > (int64_t(1) << 53)) error(0, "Enum " + name + " has a duplicate or out-of-range value");
            }
        }
        if (Structs.size() > 64) error(0, "Struct definition limit exceeds 64");
        // A struct that contains itself has no finite value; the field values are not checked until it is fixed.
        const auto cycle = StructCycle();
        if (!cycle.empty()) error(0, "Struct contains itself: " + cycle);
        // The JSON depth of a type's values (a struct value is an object, a vector/array one level more): values
        // deeper than 8 levels do not fit the VM's value checks, so a struct may nest to depth 7 (an array of it: 8).
        std::map<std::string, int> depths;
        std::function<int(Type, const std::string&, int)> depthOf = [&](Type kind, const std::string& named, int guard) -> int {
            if (guard > 64) return 64;
            if (kind == Type::ArrayStruct) return 1 + depthOf(Type::Struct, named, guard + 1);
            if (kind == Type::Struct)
            {
                if (auto known = depths.find(named); known != depths.end()) return known->second;
                int deepest = 0;
                if (auto it = Structs.find(named); it != Structs.end())
                    for (const auto& field : it->second.Fields) deepest = std::max(deepest, depthOf(field.Kind, field.Enum, guard + 1));
                return depths[named] = 1 + deepest;
            }
            if (kind == Type::ArrayVec2 || kind == Type::ArrayVec3 || kind == Type::ArrayVec4) return 2;
            if (kind == Type::Curve) return 3; // {Keys: [{...}]}
            return IsArray(kind) || kind == Type::Vec2 || kind == Type::Vec3 || kind == Type::Vec4 ? 1 : 0;
        };
        for (const auto& [name, definition] : Structs)
        {
            if (name.empty() || name.size() > 64) error(0, "Invalid struct name");
            if (definition.Fields.empty() || definition.Fields.size() > 32) error(0, "Struct " + name + " needs 1..32 fields");
            std::set<std::string> names;
            size_t bytes = 0;
            for (const auto& field : definition.Fields)
            {
                bytes += field.Name.size() + ValueBytes(field.Default);
                if (field.Name.empty() || field.Name.size() > 64 || field.Name == "Struct" || !names.insert(field.Name).second) error(0, "Struct " + name + " has an empty, long, reserved or duplicate field name");
                else if (field.Kind == Type::Exec || field.Kind >= Type::Count) error(0, "Struct " + name + " field " + field.Name + " has an unsupported type");
                else if (cycle.empty() && !Fits(field, field.Default)) error(0, "Struct " + name + " field " + field.Name + " has an invalid default or unknown enum or struct");
            }
            if (bytes > 65536) error(0, "Struct " + name + " defaults exceed 64 KiB");
            if (cycle.empty() && depthOf(Type::Struct, name, 0) > 7) error(0, "Struct " + name + " nests deeper than 7 levels");
        }
        return d;
    }
    namespace {
        // A graph with the definitions of another but no nodes, links or macros.
        Graph Shell(const Graph& g)
        {
            Graph shell;
            shell.SourceVersion = g.SourceVersion;
            shell.Variables = g.Variables;
            shell.Functions = g.Functions;
            shell.Enums = g.Enums;
            shell.Structs = g.Structs;
            shell.Natives = g.Natives;
            return shell;
        }
        bool UsesMacros(const Graph& g)
        {
            return !g.Macros.empty() || std::any_of(g.Nodes.begin(), g.Nodes.end(), [](const Node& n) { return !n.Macro.empty() || n.Kind.starts_with("Macro."); });
        }
        std::string NameLiteral(const Node& n)
        {
            auto it = n.Inputs.find("Name");
            return it != n.Inputs.end() && it->second.is_string() ? it->second.get<std::string>() : "";
        }
    }
    std::vector<Diagnostic> Graph::ValidateMacros() const
    {
        std::vector<Diagnostic> d;
        auto error = [&](uint32_t n, std::string s) { d.push_back({ n, std::move(s) }); };
        if (Macros.size() > 64) error(0, "Macro definition limit exceeds 64");
        std::map<std::string, int> entries, exits;
        std::map<uint32_t, std::string> owner;
        std::map<std::string, std::set<std::string>> uses;
        for (const auto& n : Nodes)
        {
            owner[n.ID] = n.Macro;
            if (!n.Macro.empty() && !Macros.contains(n.Macro)) error(n.ID, "Node belongs to unknown macro " + n.Macro);
            if ((n.Kind == "Macro.Entry" || n.Kind == "Macro.Exit") && n.Macro.empty()) error(n.ID, "Macro entry/exit outside a macro body");
            if (n.Kind == "Macro.Entry") ++entries[n.Macro];
            if (n.Kind == "Macro.Exit") ++exits[n.Macro];
            if (!n.Macro.empty() && (n.Kind.starts_with("Event.") || n.Kind == "Function.Entry" || n.Kind == "Function")) error(n.ID, "A macro cannot hold events or function entries");
            if (n.Kind == "Macro.Instance")
            {
                const auto name = NameLiteral(n);
                if (!Macros.contains(name)) error(n.ID, "Unknown macro: " + name);
                else uses[n.Macro].insert(name);
            }
        }
        for (const auto& [name, macro] : Macros)
        {
            if (name.empty() || name.size() > 64) error(0, "Invalid macro name");
            if (entries[name] != 1 || exits[name] != 1) error(0, "Macro " + name + " needs exactly one entry and one exit");
            for (const auto* pins : { &macro.Inputs, &macro.Outputs })
            {
                if (pins->size() > 32) error(0, "Macro " + name + " has more than 32 pins on one side");
                std::set<std::string> names;
                for (const auto& pin : *pins)
                    if (pin.Name.empty() || pin.Name.size() > 64 || pin.Name == "Name" || !names.insert(pin.Name).second || pin.Kind >= Type::Count || (pin.Kind == Type::Exec ? !pin.Default.is_null() : !Fits(pin, pin.Default)))
                        error(0, "Invalid/duplicate/reserved macro pin in " + name);
            }
        }
        for (const auto& l : Links)
            if (owner.contains(l.From) && owner.contains(l.To) && owner.at(l.From) != owner.at(l.To)) error(l.To, "Link crosses a macro boundary");
        // A macro that instances itself, directly or through other macros, would never finish expanding.
        std::map<std::string, int> state;
        std::function<bool(const std::string&)> recursive = [&](const std::string& name) {
            if (state[name] == 1) return true;
            if (state[name] == 2) return false;
            state[name] = 1;
            for (const auto& used : uses[name])
                if (recursive(used)) return true;
            state[name] = 2;
            return false;
        };
        for (const auto& [name, macro] : Macros)
            if (state[name] != 2 && recursive(name)) error(0, "Macro " + name + " uses itself");
        return d;
    }
    std::vector<Diagnostic> Graph::ValidateGraph() const
    {
        auto d = ValidateTypes();
        auto natives = ValidateNatives();
        d.insert(d.end(), natives.begin(), natives.end());
        if (!UsesMacros(*this))
        {
            auto nodes = ValidateNodes(true);
            d.insert(d.end(), nodes.begin(), nodes.end());
            return d;
        }
        const auto macros = ValidateMacros();
        d.insert(d.end(), macros.begin(), macros.end());
        // Each macro body on its own (its entry is the source of its inputs; locals and returns are checked where
        // the body is expanded).
        std::set<std::pair<uint32_t, std::string>> reported;
        std::map<uint32_t, std::string> owner;
        for (const auto& n : Nodes) owner[n.ID] = n.Macro;
        for (const auto& [name, macro] : Macros)
        {
            Graph body = Shell(*this);
            body.Macros = Macros;
            for (const auto& n : Nodes)
                if (n.Macro == name) body.Nodes.push_back(n);
            for (const auto& l : Links)
                if (owner.contains(l.From) && owner.contains(l.To) && owner.at(l.From) == name && owner.at(l.To) == name) body.Links.push_back(l);
            for (auto& found : body.ValidateNodes(false))
            {
                found.Message = "In macro " + name + ": " + found.Message;
                if (reported.insert({ found.Node, found.Message }).second) d.push_back(std::move(found));
            }
        }
        if (!macros.empty()) return d; // the expansion needs consistent macro definitions
        // The graph as the VM runs it: findings in copied macro nodes point at the macro instance.
        auto expansion = Expand();
        d.insert(d.end(), expansion.Errors.begin(), expansion.Errors.end());
        Graph expanded = Shell(*this);
        expanded.Nodes = std::move(expansion.Nodes);
        expanded.Links = std::move(expansion.Links);
        for (auto found : expanded.ValidateNodes(true))
        {
            if (auto origin = expansion.Origins.find(found.Node); origin != expansion.Origins.end())
            {
                const auto& [instance, source] = origin->second;
                found.Message = "In macro " + owner[source] + ": " + found.Message;
                if (reported.contains({ source, found.Message })) continue; // already found in the body itself
                found.Node = instance;
                found.Source = source;
            }
            if (reported.insert({ found.Node, found.Message }).second) d.push_back(std::move(found));
        }
        return d;
    }
    std::vector<Diagnostic> Graph::ValidateNodes(bool withFunctions) const
    {
        std::vector<Diagnostic> d;
        auto error = [&](uint32_t n, std::string s) { d.push_back({ n, std::move(s) }); };
        if (Nodes.size() > 4096 || Links.size() > 8192 || Variables.size() > 256) error(0, "Graph size limit exceeded");
        if (withFunctions)
        {
            auto extra = ValidateFunctions();
            d.insert(d.end(), extra.begin(), extra.end());
        }
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
            const bool typed = n.Kind == "Function.Call" || n.Kind == "Function.Entry" || n.Kind == "Function.Return" || n.Kind == "Parent.Call";
            for (auto& [key, value] : n.Inputs)
            {
                auto* p = FindPin(definitions.at(n.ID).Inputs, key);
                if (!p && typed) error(n.ID, "Argument " + key + " is not in the signature of " + (n.Inputs.contains("Name") && n.Inputs.at("Name").is_string() ? n.Inputs.at("Name").get<std::string>() : std::string("MyFunction")));
                else if (!p || p->Kind == Type::Exec || !Fits(*p, value)) error(n.ID, "Invalid input: " + key);
            }
            for (const auto* pins : { &definitions.at(n.ID).Inputs, &definitions.at(n.ID).Outputs })
            {
                auto unknown = std::find_if(pins->begin(), pins->end(), [&](const Pin& p) { return ((p.Kind == Type::Enum || p.Kind == Type::ArrayEnum) && !Enums.contains(p.Enum)) || ((p.Kind == Type::Struct || p.Kind == Type::ArrayStruct) && !Structs.contains(p.Enum)); });
                if (unknown != pins->end())
                {
                    error(n.ID, std::string(unknown->Kind == Type::Enum || unknown->Kind == Type::ArrayEnum ? "Unknown enum: " : "Unknown struct: ") + unknown->Enum);
                    break;
                }
            }
            if (n.Kind == "Struct.GetField" || n.Kind == "Struct.SetField")
            {
                auto field = n.Inputs.find("Field");
                const std::string key = field != n.Inputs.end() && field->second.is_string() ? field->second.get<std::string>() : "";
                const auto* structPin = FindPin(definitions.at(n.ID).Inputs, "Value");
                auto definition = structPin ? Structs.find(structPin->Enum) : Structs.end();
                if (definition != Structs.end() && !definition->second.Find(key)) error(n.ID, "Unknown struct field: " + key);
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
                    if (v->second.Kind != p.Kind || (IsNamed(p.Kind) && v->second.Enum != p.Enum)) error(n.ID, "Variable type differs from node");
                }
            }
        }
        for (auto& [name, v] : Variables)
            if (name.empty() || name.size() > 128 || v.Kind == Type::Exec || !Fits(v.Kind, v.Enum, v.Default)) error(0, "Invalid variable: " + name);
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
            if ((!o && (a->Kind == "Function.Call" || a->Kind == "Function.Entry")) || (!i && (b->Kind == "Function.Call" || b->Kind == "Function.Return")))
            {
                error(!o ? l.From : l.To, "Pin " + (!o ? l.Output : l.Input) + " is not in the function signature");
                continue;
            }
            if (!o || !i || !SameType(*o, *i))
            {
                error(l.To, "Link pin types differ or pin missing");
                continue;
            }
            if (i->Kind != Type::Exec && !inputs.insert({ l.To, l.Input }).second) error(l.To, "Data input already linked");
            if (i->Kind == Type::Exec && !execOutputs.insert({ l.From, l.Output }).second) error(l.From, "Execution output already linked; use Sequence");
            if (l.Input == "Name" && (b->Kind == "Function" || b->Kind == "Call" || b->Kind.starts_with("Function.") || b->Kind == "Macro.Instance" || b->Kind.starts_with("Parent.") || b->Kind == "Event.Custom" || b->Kind.starts_with("Local.") || b->Kind.starts_with("Get.") || b->Kind.starts_with("Set."))) error(l.To, "Variable name must be a literal");
            if (l.Input == "Enum" && i->Kind == Type::String && b->Kind.find("Enum") != std::string::npos) error(l.To, "Enum type must be a literal");
            if (l.Input == "Method" && b->Kind.starts_with("Native.")) error(l.To, "Native method name must be a literal");
            if ((l.Input == "Struct" || l.Input == "Field") && i->Kind == Type::String && b->Kind.find("Struct") != std::string::npos) error(l.To, "Struct type and field must be literals");
            if (i->Kind != Type::Exec) dependencies[l.To].push_back(l.From);
        }
        std::map<uint32_t, int> color;
        std::function<void(uint32_t, int)> visit = [&](uint32_t id, int depth) {if(depth>128){error(id,"Data nesting exceeds 128");return;}if(color[id]==1){error(id,"Data cycle");return;}if(color[id]==2)return;color[id]=1;for(auto dep:dependencies[id])visit(dep,depth+1);color[id]=2; };
        for (auto& n : Nodes) visit(n.ID, 0);
        return d;
    }
    Value Graph::Serialize() const
    {
        Value j = { { "Format", "Zamin.VisualGraph" }, { "Version", FormatVersion }, { "Nodes", Value::array() }, { "Links", Value::array() }, { "Variables", Value::object() } };
        auto typed = [](Type kind, const std::string& enumName, Value entry) {
            if (kind == Type::Enum || kind == Type::ArrayEnum) entry["Enum"] = enumName;
            if (kind == Type::Struct || kind == Type::ArrayStruct) entry["Struct"] = enumName;
            return entry;
        };
        for (auto& n : Nodes)
        {
            j["Nodes"].push_back({ { "ID", n.ID }, { "Kind", n.Kind }, { "X", n.X }, { "Y", n.Y }, { "Inputs", n.Inputs }, { "Breakpoint", n.Breakpoint } });
            if (!n.Macro.empty()) j["Nodes"].back()["Macro"] = n.Macro;
        }
        // Inheritance: the parent path and the rename log (only when there are; inherited members are not saved).
        if (!Parent.empty()) j["Parent"] = Parent;
        if (!Renames.empty())
        {
            j["Renames"] = Value::array();
            for (const auto& r : Renames) j["Renames"].push_back({ { "Kind", r.Kind }, { "From", r.From }, { "To", r.To } });
        }
        for (auto& l : Links) j["Links"].push_back({ { "From", l.From }, { "Output", l.Output }, { "To", l.To }, { "Input", l.Input } });
        for (auto& [name, v] : Variables)
            if (!Inherited.Variables.contains(name)) j["Variables"][name] = typed(v.Kind, v.Enum, { { "Type", TypeName(v.Kind) }, { "Default", v.Default } });
        j["Enums"] = Value::object();
        for (const auto& [name, definition] : Enums)
        {
            if (Inherited.Enums.contains(name)) continue;
            Value entries = Value::array();
            for (const auto& entry : definition.Entries) entries.push_back({ { "Name", entry.Name }, { "Value", entry.Value } });
            j["Enums"][name] = { { "Entries", std::move(entries) }, { "Source", definition.Source } };
        }
        j["Structs"] = Value::object();
        for (const auto& [name, definition] : Structs)
        {
            if (Inherited.Structs.contains(name)) continue;
            Value fields = Value::array();
            for (const auto& f : definition.Fields) fields.push_back(typed(f.Kind, f.Enum, { { "Name", f.Name }, { "Type", TypeName(f.Kind) }, { "Default", f.Default } }));
            j["Structs"][name] = { { "Fields", std::move(fields) } };
        }
        j["Functions"] = Value::object();
        for (const auto& [name, signature] : Functions)
        {
            if (Inherited.Functions.contains(name)) continue;
            Value function = { { "Inputs", Value::array() }, { "Outputs", Value::array() }, { "Locals", Value::object() } };
            for (const auto& p : signature.Inputs) function["Inputs"].push_back(typed(p.Kind, p.Enum, { { "Name", p.Name }, { "Type", TypeName(p.Kind) }, { "Default", p.Default } }));
            for (const auto& p : signature.Outputs) function["Outputs"].push_back(typed(p.Kind, p.Enum, { { "Name", p.Name }, { "Type", TypeName(p.Kind) }, { "Default", p.Default } }));
            for (const auto& [key, v] : signature.Locals) function["Locals"][key] = typed(v.Kind, v.Enum, { { "Type", TypeName(v.Kind) }, { "Default", v.Default } });
            j["Functions"][name] = std::move(function);
        }
        j["Macros"] = Value::object();
        for (const auto& [name, macro] : Macros)
        {
            if (Inherited.Macros.contains(name)) continue;
            Value entry = { { "Inputs", Value::array() }, { "Outputs", Value::array() } };
            for (const auto& p : macro.Inputs) entry["Inputs"].push_back(typed(p.Kind, p.Enum, { { "Name", p.Name }, { "Type", TypeName(p.Kind) }, { "Default", p.Default } }));
            for (const auto& p : macro.Outputs) entry["Outputs"].push_back(typed(p.Kind, p.Enum, { { "Name", p.Name }, { "Type", TypeName(p.Kind) }, { "Default", p.Default } }));
            j["Macros"][name] = std::move(entry);
        }
        // Native method signatures: the ones this graph's own nodes call (format 6; written only when there are).
        const auto used = NativesUsed();
        for (const auto& [key, signature] : Natives)
        {
            if (!used.contains(key)) continue;
            Value entry = { { "Static", signature.Static }, { "Pure", signature.Pure }, { "Inputs", Value::array() }, { "Outputs", Value::array() } };
            if (!signature.Tooltip.empty()) entry["Tooltip"] = signature.Tooltip;
            for (const auto& p : signature.Inputs) entry["Inputs"].push_back(typed(p.Kind, p.Enum, { { "Name", p.Name }, { "Type", TypeName(p.Kind) }, { "Default", p.Default } }));
            for (const auto& p : signature.Outputs) entry["Outputs"].push_back(typed(p.Kind, p.Enum, { { "Name", p.Name }, { "Type", TypeName(p.Kind) }, { "Default", p.Default } }));
            if (!j.contains("Natives")) j["Natives"] = Value::object();
            j["Natives"][key] = std::move(entry);
        }
        return j;
    }
    Graph Graph::Parse(const Value& j)
    {
        Require(j.is_object() && j.value("Format", "") == "Zamin.VisualGraph", "Unsupported graph format/version");
        const auto version = j.contains("Version") && j.at("Version").is_number_integer() ? j.at("Version").get<int64_t>() : 0;
        Require(version >= 1 && version <= FormatVersion, "Unsupported graph format/version");
        Graph g;
        g.SourceVersion = int(version);
        // Version 1-3 -> 4 migration: version 1 has no enum type, versions 1-2 no struct types and versions 1-3 no
        // macros, enum arrays or nested structs, so their types, nodes, legacy Function/Call and typed functions read
        // unchanged; a newer type in an older file is rejected rather than guessed.
        auto enumName = [&](Type kind, const Value& entry) {
            Require(version >= 5 || kind != Type::Curve, "Curve types need graph format version 5");
            if (kind == Type::ArrayEnum)
            {
                Require(version >= 4, "Enum array types need graph format version 4");
                Require(entry.contains("Enum") && entry.at("Enum").is_string(), "Enum type without an enum name");
                return entry.at("Enum").get<std::string>();
            }
            if (kind == Type::Struct || kind == Type::ArrayStruct)
            {
                Require(version >= 3, "Struct types need graph format version 3");
                Require(entry.contains("Struct") && entry.at("Struct").is_string(), "Struct type without a struct name");
                return entry.at("Struct").get<std::string>();
            }
            if (kind != Type::Enum) return std::string();
            Require(version >= 2, "Enum types need graph format version 2");
            Require(entry.contains("Enum") && entry.at("Enum").is_string(), "Enum type without an enum name");
            return entry.at("Enum").get<std::string>();
        };
        auto readID = [](const Value& v) {Require(v.is_number_integer()&&(!v.is_number_integer()||v.is_number_unsigned()||v.get<int64_t>()>=0),"Node ID must be unsigned integer");uint64_t id=v.get<uint64_t>();Require(id>0&&id<=UINT32_MAX,"Node ID outside 1..UINT32_MAX");return (uint32_t)id; };
        auto& ns = j.at("Nodes");
        auto& ls = j.at("Links");
        auto& vs = j.at("Variables");
        Require(ns.is_array() && ns.size() <= 4096 && ls.is_array() && ls.size() <= 8192 && vs.is_object() && vs.size() <= 256, "Graph size/type limit");
        for (auto& n : ns)
        {
            g.Nodes.push_back({ readID(n.at("ID")), n.at("Kind").get<std::string>(), n.value("X", 0.f), n.value("Y", 0.f), n.value("Inputs", std::map<std::string, Value>{}), n.value("Breakpoint", false) });
            Require(version >= 5 || !g.Nodes.back().Kind.starts_with("Parent."), "Parent nodes need graph format version 5");
            if (n.contains("Macro"))
            {
                Require(version >= 4, "Macros need graph format version 4");
                g.Nodes.back().Macro = n.at("Macro").get<std::string>();
            }
        }
        for (auto& l : ls) g.Links.push_back({ readID(l.at("From")), l.at("Output").get<std::string>(), readID(l.at("To")), l.at("Input").get<std::string>() });
        for (auto it = vs.begin(); it != vs.end(); ++it)
        {
            bool found = false;
            for (int i = 1; i < int(Type::Count); ++i)
                if (it.value().at("Type") == TypeName((Type)i))
                {
                    g.Variables[it.key()] = { (Type)i, it.value().at("Default"), enumName(Type(i), it.value()) };
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
                    for (const auto& pin : pins)
                    {
                        const auto kind = type(pin.at("Type"));
                        (std::string(side) == "Inputs" ? signature.Inputs : signature.Outputs).push_back({ pin.at("Name").get<std::string>(), kind, pin.at("Default"), enumName(kind, pin) });
                    }
                }
                const auto locals = it.value().value("Locals", Value::object());
                Require(locals.is_object() && locals.size() <= 32, "Local variable limit/type");
                for (auto v = locals.begin(); v != locals.end(); ++v)
                {
                    const auto kind = type(v.value().at("Type"));
                    signature.Locals[v.key()] = { kind, v.value().at("Default"), enumName(kind, v.value()) };
                }
                g.Functions[it.key()] = std::move(signature);
            }
        }
        if (j.contains("Enums"))
        {
            Require(version >= 2, "Enums need graph format version 2");
            const auto& enums = j.at("Enums");
            Require(enums.is_object() && enums.size() <= 64, "Enum definition limit/type");
            for (auto it = enums.begin(); it != enums.end(); ++it)
            {
                EnumDefinition definition;
                const auto& entries = it.value().at("Entries");
                Require(entries.is_array() && entries.size() <= 256, "Enum entry limit/type");
                for (const auto& entry : entries)
                {
                    Require(entry.at("Value").is_number_integer(), "Enum value must be an integer");
                    definition.Entries.push_back({ entry.at("Name").get<std::string>(), entry.at("Value").get<int64_t>() });
                }
                definition.Source = it.value().value("Source", "");
                g.Enums[it.key()] = std::move(definition);
            }
        }
        if (j.contains("Structs"))
        {
            Require(version >= 3, "Structs need graph format version 3");
            const auto& structs = j.at("Structs");
            Require(structs.is_object() && structs.size() <= 64, "Struct definition limit/type");
            for (auto it = structs.begin(); it != structs.end(); ++it)
            {
                StructDefinition definition;
                const auto& fields = it.value().at("Fields");
                Require(fields.is_array() && fields.size() <= 32, "Struct field limit/type");
                for (const auto& field : fields)
                {
                    const auto kind = type(field.at("Type"));
                    Require(version >= 4 || (kind != Type::Struct && kind != Type::ArrayStruct), "Struct fields of struct type need graph format version 4");
                    definition.Fields.push_back({ field.at("Name").get<std::string>(), kind, field.at("Default"), enumName(kind, field) });
                }
                g.Structs[it.key()] = std::move(definition);
            }
        }
        if (j.contains("Macros"))
        {
            Require(version >= 4, "Macros need graph format version 4");
            const auto& macros = j.at("Macros");
            Require(macros.is_object() && macros.size() <= 64, "Macro definition limit/type");
            for (auto it = macros.begin(); it != macros.end(); ++it)
            {
                MacroDefinition macro;
                for (const char* side : { "Inputs", "Outputs" })
                {
                    const auto& pins = it.value().at(side);
                    Require(pins.is_array() && pins.size() <= 32, "Macro pin limit/type");
                    for (const auto& pin : pins)
                    {
                        // A macro pin may also be an execution pin.
                        const auto kind = pin.at("Type") == TypeName(Type::Exec) ? Type::Exec : type(pin.at("Type"));
                        (std::string(side) == "Inputs" ? macro.Inputs : macro.Outputs).push_back({ pin.at("Name").get<std::string>(), kind, pin.value("Default", Value()), enumName(kind, pin) });
                    }
                }
                g.Macros[it.key()] = std::move(macro);
            }
        }
        if (j.contains("Natives"))
        {
            Require(version >= 6, "Native method calls need graph format version 6");
            const auto& natives = j.at("Natives");
            Require(natives.is_object() && natives.size() <= MaxNatives, "Native method limit/type");
            for (auto it = natives.begin(); it != natives.end(); ++it)
            {
                NativeSignature signature;
                signature.Static = it.value().value("Static", false);
                signature.Pure = it.value().value("Pure", false);
                signature.Tooltip = it.value().value("Tooltip", "");
                for (const char* side : { "Inputs", "Outputs" })
                {
                    const auto& pins = it.value().at(side);
                    Require(pins.is_array() && pins.size() <= 32, "Native method pin limit/type");
                    for (const auto& pin : pins)
                    {
                        const auto kind = type(pin.at("Type"));
                        (std::string(side) == "Inputs" ? signature.Inputs : signature.Outputs).push_back({ pin.at("Name").get<std::string>(), kind, pin.at("Default"), enumName(kind, pin) });
                    }
                }
                g.Natives[it.key()] = std::move(signature);
            }
        }
        for (const auto& n : g.Nodes) Require(version >= 6 || !n.Kind.starts_with("Native."), "Native method nodes need graph format version 6");
        if (j.contains("Parent"))
        {
            Require(version >= 5, "Graph inheritance needs graph format version 5");
            Require(j.at("Parent").is_string() && j.at("Parent").get_ref<const std::string&>().size() <= 512, "Parent must be a path");
            g.Parent = j.at("Parent").get<std::string>();
        }
        if (j.contains("Renames"))
        {
            Require(version >= 5, "Renames need graph format version 5");
            const auto& renames = j.at("Renames");
            Require(renames.is_array() && renames.size() <= MaxRenames, "Rename limit/type");
            for (const auto& r : renames)
            {
                Rename rename{ r.at("Kind").get<std::string>(), r.at("From").get<std::string>(), r.at("To").get<std::string>() };
                Require(rename.Kind == "Variable" || rename.Kind == "Function" || rename.Kind == "Enum" || rename.Kind == "Struct" || rename.Kind == "Macro", "Unknown rename kind");
                g.Renames.push_back(std::move(rename));
            }
        }
        return g;
    }
    Value ReadGraphJson(const std::filesystem::path& p)
    {
        Require(std::filesystem::file_size(p) <= 4 * 1024 * 1024, "Graph exceeds 4 MB");
        std::ifstream f(p, std::ios::binary);
        Require((bool)f, "Graph file could not be opened");
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        // The nesting depth on the text (outside strings), so the parser needs no per-value callback.
        int depth = 0;
        bool inString = false, escaped = false;
        for (const char c : text)
        {
            if (inString)
            {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
            }
            else if (c == '"') inString = true;
            else if (c == '[' || c == '{') Require(++depth <= 64, "JSON nesting exceeds 64");
            else if (c == ']' || c == '}') --depth;
        }
        return Value::parse(text);
    }
    Graph Graph::Load(const std::filesystem::path& p, const std::filesystem::path& assets)
    {
        Value j = ReadGraphJson(p);
        auto g = Parse(j);
        if (!g.Parent.empty())
        {
            auto root = assets;
            // Without an Assets folder: the nearest folder named Assets above the file, else the file's folder.
            for (auto dir = p.parent_path(); root.empty() && !dir.empty(); dir = dir.parent_path())
            {
                if (dir.filename() == "Assets") root = dir;
                if (dir == dir.parent_path()) break;
            }
            g.LoadParent(root.empty() ? p.parent_path() : root, p);
        }
        return g;
    }
    bool Graph::Save(const std::filesystem::path& p) const
    {
        if (Nodes.size() > 4096 || Links.size() > 8192 || Variables.size() > 256 || Functions.size() > 64 || Enums.size() > 64 || Structs.size() > 64 || Macros.size() > 64 || Natives.size() > MaxNatives) return false;
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
    bool Machine::Prepare(Graph graph, Program& out, std::string& error, std::vector<Diagnostic>* diagnostics)
    {
        out = {};
        auto errors = graph.Validate();
        if (diagnostics) *diagnostics = errors;
        if (!errors.empty())
        {
            error = "Node " + std::to_string(errors[0].Node) + ": " + errors[0].Message;
            return false;
        }
        Graph& g = out.Runnable;
        g = graph.Inherited.Base ? graph.Resolved() : std::move(graph);
        // The VM runs the child with its parent chain: inherited nodes remember their file and ID for errors
        // (Inherited.Origins); the base itself is not needed any more.
        g.Inherited.Base.reset();
        if (!g.Macros.empty())
        {
            // The VM runs the expanded graph; a copied node remembers its instance and body node for errors.
            for (const auto& n : g.Nodes)
                if (!n.Macro.empty()) out.SourceMacros[n.ID] = n.Macro;
            auto expansion = g.Expand();
            g.Nodes = std::move(expansion.Nodes);
            g.Links = std::move(expansion.Links);
            g.Macros.clear();
            out.MacroOrigins = std::move(expansion.Origins);
        }
        // Typed function, enum, struct and native nodes take their pins from the graph (signature, enum, struct,
        // field, method) through their literal Name/Enum/Struct/Field/Method inputs: one shared description per
        // distinct literal set.
        std::map<std::string, std::shared_ptr<const Definition>> descriptions;
        out.Definitions.reserve(g.Nodes.size());
        for (const auto& n : g.Nodes)
        {
            std::string key = n.Kind;
            for (const char* literal : { "Name", "Enum", "Struct", "Field", "Method" })
                if (auto it = n.Inputs.find(literal); it != n.Inputs.end() && it->second.is_string()) key += std::string("|") + literal + "=" + it->second.get<std::string>();
            auto& description = descriptions[key];
            if (!description) description = std::make_shared<Definition>(g.Describe(n));
            out.Definitions.push_back(description);
        }
        return true;
    }
    bool Machine::Load(Graph graph, Bridge bridge, const Value& overrides)
    {
        Program program;
        std::string error;
        if (!Prepare(std::move(graph), program, error))
        {
            Load(Program{}, std::move(bridge)); // nothing of an earlier graph stays
            m_Error = error;
            return false;
        }
        return Load(std::move(program), std::move(bridge), overrides);
    }
    bool Machine::Load(Program program, Bridge bridge, const Value& overrides)
    {
        Stop();
        m_Error.clear();
        m_Trace.clear();
        m_Graph = std::move(program.Runnable);
        m_Bridge = std::move(bridge);
        m_Variables.clear();
        m_Origins = std::move(program.MacroOrigins);
        m_SourceMacros = std::move(program.SourceMacros);
        m_Inherited = m_Graph.Inherited.Origins;
        m_NodeIndex.clear();
        m_InputsOf.assign(m_Graph.Nodes.size(), {});
        m_ExecOf.assign(m_Graph.Nodes.size(), {});
        m_Functions.clear();
        m_Events.clear();
        m_TypedEntries.clear();
        m_Definitions.clear();
        m_CallStack.clear();
        if (program.Definitions.size() != m_Graph.Nodes.size())
        {
            m_Error = "Program definitions do not match its nodes";
            m_Graph = {};
            return false;
        }
        for (size_t i = 0; i < m_Graph.Nodes.size(); ++i)
        {
            auto& n = m_Graph.Nodes[i];
            m_NodeIndex[n.ID] = i;
            std::string functionName = n.Inputs.contains("Name") && n.Inputs.at("Name").is_string() ? n.Inputs.at("Name").get<std::string>() : "MyFunction";
            if (n.Kind == "Function.Entry") m_TypedEntries[functionName] = n.ID;
            // A parent handler of an event the child handles too runs only through the child's Parent.Event.
            if (m_Graph.Inherited.OverriddenEvents.contains(n.ID)) continue;
            if (n.Kind == "Event.Custom") m_Events["Custom." + (n.Inputs.contains("Name") ? functionName : "MyEvent")].push_back(n.ID);
            else if (n.Kind.starts_with("Event.")) m_Events[n.Kind.substr(6)].push_back(n.ID);
            if (n.Kind == "Function") m_Functions[n.Inputs.contains("Name") ? n.Inputs.at("Name").get<std::string>() : "MyFunction"] = n.ID;
        }
        m_Definitions = std::move(program.Definitions);
        for (auto& l : m_Graph.Links)
        {
            auto from = m_NodeIndex.find(l.From), to = m_NodeIndex.find(l.To);
            const auto* pin = from != m_NodeIndex.end() ? FindPin(m_Definitions[from->second]->Outputs, l.Output) : nullptr;
            if (!pin || to == m_NodeIndex.end())
            {
                m_Error = "Link from a missing pin " + l.Output;
                return false;
            }
            // (a later wire into the same input wins, as before)
            if (pin->Kind == Type::Exec)
            {
                auto& links = m_ExecOf[from->second];
                std::erase_if(links, [&](const ExecLink& e) { return e.Output == l.Output; });
                links.push_back({ l.Output, l.To });
            }
            else
            {
                auto& links = m_InputsOf[to->second];
                std::erase_if(links, [&](const InputLink& e) { return e.Input == l.Input; });
                links.push_back({ l.Input, l.From, l.Output });
            }
        }
        m_VariableBytes = 0;
        for (auto& [name, v] : m_Graph.Variables)
        {
            auto it = overrides.is_object() ? overrides.find(name) : overrides.end();
            const auto& value = m_Variables[name] = it != overrides.end() && m_Graph.Fits(v.Kind, v.Enum, *it) ? *it : v.Default;
            m_VariableBytes += 64 + name.size() + ValueBytes(value);
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
    // One measured node run (the profiler): its inclusive time, minus the measured nodes it ran inside it, is its
    // exclusive time; an execution step's time also goes to the typed functions on the task's call stack.
    struct Machine::Measure
    {
        Machine& M;
        uint32_t Node;
        Task* Owner = nullptr;
        size_t Depth = 0;        // the typed function calls on the task when the step began
        std::string Function;    // the innermost of them
        std::chrono::steady_clock::time_point Start;
        Measure(Machine& m, uint32_t node, Task* owner = nullptr) : M(m), Node(node)
        {
            if (owner && !owner->Calls.empty())
            {
                Owner = owner;
                Depth = owner->Calls.size();
                Function = owner->Calls.back().Name;
            }
            M.m_ProfileChildren.push_back(0);
            Start = std::chrono::steady_clock::now();
        }
        Measure(const Measure&) = delete;
        Measure& operator=(const Measure&) = delete;
        ~Measure()
        {
            const double inclusive = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
            const double children = M.m_ProfileChildren.back();
            M.m_ProfileChildren.pop_back();
            if (!M.m_ProfileChildren.empty()) M.m_ProfileChildren.back() += inclusive;
            auto& p = M.m_Profile.Nodes[M.Origin(Node)];
            ++p.Calls;
            p.Inclusive += inclusive;
            p.Exclusive += std::max(0.0, inclusive - children);
            if (!Owner) return;
            // Exclusive: the function the step ran in. Inclusive: every call still on the stack that was there when
            // the step began (ReturnFunction adds a call's total when it returns).
            M.m_Profile.Functions[Function].Exclusive += inclusive;
            for (size_t i = 0, n = std::min(Depth, Owner->Calls.size()); i < n; ++i) Owner->Calls[i].ProfileTime += inclusive;
            // The step returned from its function (a Return): the call is gone, its total went to the profile.
            if (Owner->Calls.size() < Depth) M.m_Profile.Functions[Function].Inclusive += inclusive;
        }
    };
    void Machine::Profile::Merge(const Profile& other)
    {
        for (const auto& [id, p] : other.Nodes)
        {
            auto& mine = Nodes[id];
            mine.Calls += p.Calls;
            mine.Inclusive += p.Inclusive;
            mine.Exclusive += p.Exclusive;
        }
        for (const auto& [name, p] : other.Functions)
        {
            auto& mine = Functions[name];
            mine.Calls += p.Calls;
            mine.Inclusive += p.Inclusive;
            mine.Exclusive += p.Exclusive;
        }
        for (const auto& [edge, count] : other.Edges) Edges[edge] += count;
        Runs += other.Runs;
        Seconds += other.Seconds;
    }
    std::vector<uint32_t> Machine::Profile::Hottest(size_t count) const
    {
        std::vector<uint32_t> ids;
        for (const auto& [id, p] : Nodes) ids.push_back(id);
        std::sort(ids.begin(), ids.end(), [&](uint32_t a, uint32_t b) { return Nodes.at(a).Exclusive > Nodes.at(b).Exclusive || (Nodes.at(a).Exclusive == Nodes.at(b).Exclusive && a < b); });
        if (ids.size() > count) ids.resize(count);
        return ids;
    }
    std::vector<uint32_t> Machine::Profile::HotPath(size_t limit) const
    {
        // Successors by count; an entry is a node that execution reaches without a transition into it.
        std::map<uint32_t, std::vector<std::pair<uint64_t, uint32_t>>> next;
        std::set<uint32_t> targets, sources;
        for (const auto& [edge, count] : Edges)
        {
            next[edge.first].push_back({ count, edge.second });
            targets.insert(edge.second);
            sources.insert(edge.first);
        }
        // The time downstream of a node (its own and what follows it, each node once).
        auto downstream = [&](uint32_t start) {
            double total = 0;
            std::set<uint32_t> seen;
            std::vector<uint32_t> stack{ start };
            while (!stack.empty())
            {
                const uint32_t id = stack.back();
                stack.pop_back();
                if (!seen.insert(id).second) continue;
                if (auto it = Nodes.find(id); it != Nodes.end()) total += it->second.Exclusive;
                if (auto it = next.find(id); it != next.end())
                    for (const auto& [count, to] : it->second) stack.push_back(to);
            }
            return total;
        };
        uint32_t best = 0;
        double bestTime = -1;
        for (const auto& [id, p] : Nodes)
            if (!targets.contains(id) && (sources.contains(id) || Edges.empty()))
                if (const double time = downstream(id); time > bestTime) best = id, bestTime = time;
        if (!best && !Nodes.empty()) best = Hottest(1).front();
        std::vector<uint32_t> path;
        std::set<uint32_t> seen;
        for (uint32_t id = best; id && path.size() < limit && seen.insert(id).second;)
        {
            path.push_back(id);
            auto it = next.find(id);
            if (it == next.end()) break;
            id = std::max_element(it->second.begin(), it->second.end())->second;
        }
        return path;
    }
    const Definition& Machine::Def(const Node& node) const
    {
        const size_t index = IndexOf(node);
        Require(index < m_Definitions.size() && &m_Graph.Nodes[index] == &node, "Node outside the loaded graph");
        return *m_Definitions[index];
    }
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
    Value Machine::Input(const Node& n, std::string_view pin, Task& t, Active& active)
    {
        if (const size_t index = IndexOf(n); index < m_InputsOf.size())
            for (const auto& link : m_InputsOf[index])
                if (link.Input == pin) return Evaluate(link.From, link.Output, t, active);
        auto it = n.Inputs.find(std::string(pin));
        if (it != n.Inputs.end()) return it->second;
        auto* p = FindPin(Def(n).Inputs, pin);
        Require(p, "Unknown input");
        return p->Default;
    }
    Value Machine::Evaluate(uint32_t id, const std::string& output, Task& t, Active& active)
    {
        Charge();
        auto* n = Find(id);
        Require(n, "Missing node");
        Require(active.size() < 128 && active.insert(id).second, "Data cycle/depth limit");
        struct Guard
        {
            Active& s;
            uint32_t id;
            ~Guard() { s.erase(id); }
        } guard{ active, id };
        // (reading the stored result of an execution node, an event's or a parameter is no node run)
        std::optional<Measure> measure;
        if (ProfilingNow() && Def(*n).Pure) measure.emplace(*this, id);
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
        else if (k.starts_with("Enum."))
        {
            const auto& def = Def(*n);
            const auto& enumName = (k == "Enum.FromNumber" || k == "Enum.FromText" ? def.Outputs : def.Inputs).back().Enum;
            const auto& definition = m_Graph.Enums.at(enumName);
            if (k == "Enum.Equal") r = in("A") == in("B");
            else if (k == "Enum.ToNumber") r = double(in("Value").get<int64_t>());
            else if (k == "Enum.ToText")
            {
                const auto* entry = definition.Find(in("Value").get<int64_t>());
                Require(entry, "Value is not an entry of enum " + enumName);
                r = entry->Name;
            }
            else if (k == "Enum.FromNumber")
            {
                const double number = num("Number");
                const auto* entry = std::floor(number) == number && std::abs(number) <= 9007199254740992.0 ? definition.Find(int64_t(number)) : nullptr;
                Require(entry, "Number is not a value of enum " + enumName);
                r = entry->Value;
            }
            else
            {
                const auto* entry = definition.Find(in("Text").get<std::string>());
                Require(entry, "Text is not an entry of enum " + enumName);
                r = entry->Value;
            }
        }
        else if (k.starts_with("Struct."))
        {
            if (k == "Struct.Make")
            {
                r = Value::object();
                for (const auto& p : Def(*n).Inputs)
                    if (p.Name != "Struct") r[p.Name] = Input(*n, p.Name, t, active);
            }
            else
            {
                auto value = in("Value");
                const std::string field = k == "Struct.Break" ? output : in("Field").get<std::string>();
                Require(value.is_object() && value.contains(field), "Struct field missing: " + field);
                if (k == "Struct.SetField")
                {
                    value[field] = in("FieldValue");
                    r = std::move(value);
                }
                else r = value.at(field);
            }
        }
        else if (k.starts_with("Curve."))
        {
            auto keys = CurveKeys(in("Curve"));
            if (k == "Curve.Evaluate") r = EvaluateCurve(keys, num("Time"));
            else if (k == "Curve.KeyCount") r = double(keys.size());
            else if (k == "Curve.Length")
            {
                const double start = keys.empty() ? 0 : keys.front().Time, end = keys.empty() ? 0 : keys.back().Time;
                r = output == "Start" ? start : output == "End" ? end : end - start;
            }
            else
            {
                // AddKey: a key at the same time is replaced; a curve holds at most MaxCurveKeys keys.
                const double mode = num("Mode");
                Require(mode == 0 || mode == 1 || mode == 2, "Curve key Mode must be 0, 1 or 2");
                CurveKey key{ num("Time"), num("Value"), CurveMode(int(mode)), num("InTangent"), num("OutTangent") };
                std::erase_if(keys, [&](const CurveKey& existing) { return existing.Time == key.Time; });
                Require(keys.size() < MaxCurveKeys, "Curve key limit");
                keys.push_back(key);
                r = MakeCurve(std::move(keys));
            }
        }
        else if (k == "Native.Pure") r = CallNative(*n, t, active);
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
        else if (k == "FormatNumber")
        {
            // Fixed decimals 0..6, rounded half away from zero; "-0" never appears.
            const double decimals = num("Decimals");
            Require(decimals >= 0 && decimals <= 6 && std::floor(decimals) == decimals, "FormatNumber Decimals must be 0..6");
            const double value = num("Number");
            Require(std::abs(value) < 1e15, "FormatNumber value out of range");
            const double scale = std::pow(10.0, decimals);
            double rounded = std::round(value * scale) / scale;
            if (rounded == 0) rounded = 0;
            char text[64];
            std::snprintf(text, sizeof(text), "%.*f", int(decimals), rounded);
            r = std::string(text);
        }
        else if (k == "FormatTime")
        {
            // "m:ss" below an hour, "h:mm:ss" above; negative times count as 0 (whole seconds, floored).
            const double seconds = num("Seconds");
            Require(seconds < 1e9, "FormatTime value out of range");
            const long long total = seconds > 0 ? (long long)std::floor(seconds) : 0;
            char text[64];
            if (total >= 3600) std::snprintf(text, sizeof(text), "%lld:%02lld:%02lld", total / 3600, total / 60 % 60, total % 60);
            else std::snprintf(text, sizeof(text), "%02lld:%02lld", total / 60, total % 60);
            r = std::string(text);
        }
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
        else if (k.starts_with("SubtractVec"))
        {
            r = in("A");
            auto b = in("B");
            for (size_t i = 0; i < r.size(); ++i) r[i] = r[i].get<double>() - b[i].get<double>();
        }
        else if (k.starts_with("DotVec") || k.starts_with("DistanceVec"))
        {
            auto a = in("A"), b = in("B");
            double sum = 0;
            for (size_t i = 0; i < a.size(); ++i)
            {
                const double x = a[i].get<double>(), y = b[i].get<double>();
                sum += k.starts_with("DotVec") ? x * y : (x - y) * (x - y);
            }
            r = k.starts_with("DotVec") ? sum : std::sqrt(sum);
        }
        else if (k.starts_with("LengthVec") || k.starts_with("NormalizeVec"))
        {
            r = in("Vector");
            double sum = 0;
            for (auto& x : r) sum += x.get<double>() * x.get<double>();
            const double length = std::sqrt(sum);
            if (k.starts_with("LengthVec")) r = length;
            else
                for (auto& x : r) x = length > 1e-12 ? x.get<double>() / length : 0.0; // zero-safe
        }
        else if (k == "Atan2") r = std::atan2(num("Y"), num("X"));
        else if (k == "LerpAngle")
        {
            // The shortest way round from A to B (radians); T is not clamped.
            const double a = num("A"), pi = 3.14159265358979323846;
            double delta = std::fmod(num("B") - a, 2 * pi);
            if (delta > pi) delta -= 2 * pi;
            if (delta < -pi) delta += 2 * pi;
            r = a + delta * num("T");
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
        Require(p && m_Graph.Fits(*p, r) && SafeValue(r), "Invalid result from " + k + "." + output);
        return r;
    }
    Value Machine::CallNative(const Node& n, Task& t, Active& active)
    {
        const auto key = NativeKey(n);
        Require((bool)m_Bridge, "No engine bridge for native method " + key);
        std::map<std::string, Value> args;
        for (const auto& p : Def(n).Inputs)
            if (p.Kind != Type::Exec) args[p.Name] = Input(n, p.Name, t, active);
        Value result = m_Bridge(n.Kind, args);
        const auto& outputs = Def(n).Outputs;
        const Pin* pin = outputs.empty() || outputs.back().Kind == Type::Exec ? nullptr : &outputs.back();
        if (!pin) return nullptr;
        Require(m_Graph.Fits(*pin, result) && SafeValue(result), "Invalid result from native method " + key + ": not a " + TypeName(pin->Kind) + (pin->Enum.empty() ? "" : " " + pin->Enum));
        return result;
    }
    void Machine::Push(Task& t, uint32_t from, const std::string& pin)
    {
        auto index = m_NodeIndex.find(from);
        if (index == m_NodeIndex.end()) return;
        for (const auto& link : m_ExecOf[index->second])
            if (link.Output == pin)
            {
                Require(t.Stack.size() < 1024, "Execution stack limit");
                t.Stack.push_back({ link.To });
                return;
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
        struct RunTime
        {
            Machine& M;
            bool On;
            std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
            ~RunTime()
            {
                if (!On) return;
                ++M.m_Profile.Runs;
                M.m_Profile.Seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
            }
        } runTime{ *this, ProfilingNow() };
        m_ProfileChildren.clear();
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
                    std::optional<Measure> measure;
                    if (ProfilingNow())
                    {
                        if (t.Last) ++m_Profile.Edges[{ Origin(t.Last), Origin(n->ID) }];
                        t.Last = n->ID;
                        measure.emplace(*this, n->ID, &t);
                    }
                    m_Trace.push_back(n->ID);
                    if (m_Trace.size() > 128) m_Trace.pop_front();
                    Active active;
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
                        const auto& local = m_Graph.Functions.at(t.Calls.back().Name).Locals.at(name);
                        Require(m_Graph.Fits(local.Kind, local.Enum, value), "Local variable type mismatch");
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
                    else if (k == "Enum.Switch")
                    {
                        const auto& pin = Def(*n).Inputs.back();
                        const auto* entry = m_Graph.Enums.at(pin.Enum).Find(in("Value").get<int64_t>());
                        Require(entry, "Value is not an entry of enum " + pin.Enum);
                        Push(t, n->ID, entry->Name);
                    }
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
                        const auto& variable = m_Graph.Variables.at(name);
                        Require(m_Graph.Fits(variable.Kind, variable.Enum, value), "Variable type mismatch");
                        auto& stored = m_Variables.at(name);
                        m_VariableBytes = m_VariableBytes - ValueBytes(stored) + ValueBytes(value);
                        stored = std::move(value);
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
                    else if (k == "Parent.Event")
                    {
                        // Then continues after the parent's handlers of the event, which run first, in order, with the
                        // same event data (like the outputs of a Sequence).
                        Push(t, n->ID, "Then");
                        auto handlers = m_Graph.Inherited.ParentEvents.find(n->ID);
                        Require(handlers != m_Graph.Inherited.ParentEvents.end(), "Parent.Event without a parent handler");
                        Require(t.Stack.size() + handlers->second.size() < 1024, "Execution stack limit");
                        for (auto it = handlers->second.rbegin(); it != handlers->second.rend(); ++it) t.Stack.push_back({ *it });
                    }
                    else if (k == "Parent.Call") Require(false, "Parent.Call without an overridden parent function");
                    else if (k == "Native.Call")
                    {
                        Value result = CallNative(*n, t, active);
                        if (!Def(*n).Outputs.empty() && Def(*n).Outputs.back().Kind != Type::Exec) t.Results[n->ID] = { { Def(*n).Outputs.back().Name, std::move(result) } };
                        CheckStorage();
                        Push(t, n->ID, "Then");
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
                        else if (k.starts_with("Animator.") || k.starts_with("Gameplay."))
                        {
                            Require(result.is_object(), "Invalid Animator operation result");
                            for (const auto& pin : Def(*n).Outputs)
                                if (pin.Kind != Type::Exec) Require(result.contains(pin.Name) && Matches(pin.Kind, result.at(pin.Name)), "Invalid Animator result pin");
                            t.Results[n->ID] = std::move(result);
                        }
                        else if (k == "SpawnPrefab")
                        {
                            Require(Matches(Type::Entity, result), "Invalid spawned entity");
                            t.Results[n->ID] = { { "Value", result } };
                        }
                        // The other engine operations with data outputs (Success of Localization.SaveSettings,
                        // Audio.SetGroupVolume, SetUIValue ...): the pins their result names, type-checked.
                        else if (result.is_object())
                        {
                            Value pins = Value::object();
                            for (const auto& pin : Def(*n).Outputs)
                                if (pin.Kind != Type::Exec && result.contains(pin.Name))
                                {
                                    Require(Matches(pin.Kind, result.at(pin.Name)), "Invalid engine operation result pin");
                                    pins[pin.Name] = result.at(pin.Name);
                                }
                            if (!pins.empty()) t.Results[n->ID] = std::move(pins);
                        }
                        CheckStorage();
                        Push(t, n->ID, "Then");
                    }
                    measure.reset();
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
            // A node copied from a macro reports its macro instance (and the macro's body node).
            uint32_t source = 0;
            const uint32_t node = Origin(CurrentNode, &source);
            // An inherited node names its parent file and its ID there.
            std::string parent;
            if (auto inherited = m_Inherited.find(source ? source : node); inherited != m_Inherited.end()) parent = "In parent " + inherited->second.first + " (node " + std::to_string(inherited->second.second) + "): ";
            m_Error = "Node " + std::to_string(node) + ": " + parent + (source ? "In macro " + m_SourceMacros[source] + " (node " + std::to_string(source) + "): " : std::string()) + e.what();
            m_Tasks.clear();
        }
        m_Running = false;
    }
}
