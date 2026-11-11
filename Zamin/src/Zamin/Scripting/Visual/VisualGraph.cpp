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
            if (v.is_number()) return std::isfinite(v.get<double>());
            if (v.is_string()) return v.get_ref<const std::string&>().size() <= 65536;
            if (v.is_array())
            {
                if (v.size() > 4) return false;
                for (auto& x : v)
                    if (!x.is_number() || !std::isfinite(x.get<double>())) return false;
                return true;
            }
            return v.is_boolean() || v.is_null();
        }
    }
    const char* TypeName(Type t)
    {
        static const char* n[] = { "Exec", "Bool", "Number", "String", "Vec2", "Vec3", "Vec4", "Entity" };
        return n[(int)t];
    }
    bool Matches(Type t, const Value& v)
    {
        switch (t)
        {
            case Type::Exec:   return v.is_null();
            case Type::Bool:   return v.is_boolean();
            case Type::Number: return v.is_number() && std::isfinite(v.get<double>());
            case Type::String: return v.is_string() && v.get_ref<const std::string&>().size() <= 65536;
            case Type::Entity: return v.is_number_unsigned() || (v.is_number_integer() && v.get<int64_t>() >= 0);
            default:
            {
                size_t n = t == Type::Vec2 ? 2 : t == Type::Vec3 ? 3
                                                                 : 4;
                if (!v.is_array() || v.size() != n) return false;
                for (auto& x : v)
                    if (!Matches(Type::Number, x)) return false;
                return true;
            }
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
add("PlayAnimation","Animation",{E("In"),EntityPin(),S("Clip"),N("Speed",1)},{E("Then")});
add("SendMessage","Entities",{E("In"),EntityPin(),S("Name"),S("Text")},{E("Then")});
add("SetUIText","UI",{E("In"),EntityPin(),S("Text")},{E("Then")});
add("SetSpriteColor","Rendering",{E("In"),EntityPin(),V("Color",4)},{E("Then")});

add("LoadScene","Scenes",{E("In"),S("Path")},{E("Then")});
for(auto name:{"SaveGameState","LoadGameState"})add(name,"Save",{E("In")},{E("Then")});
for(auto type:{Type::Number,Type::Bool,Type::String}){std::string suffix=TypeName(type);Value d=type==Type::Number?Value(0.):type==Type::Bool?Value(false):Value("");add("ReadState."+suffix,"Save",{S("Key"),{"Default",type,d}},{{"Value",type,d}},true);add("WriteState."+suffix,"Save",{E("In"),S("Key"),{"Value",type,d},B("Persistent",true)},{E("Then")});}
for(auto name:{"Raycast2D","Raycast3D"})add(name,"Physics queries",{V("Origin"),V("Direction"),N("Distance",100)},{B("Hit"),EntityPin("Entity"),V("Point"),V("Normal"),N("Distance")},true);
add("SetDestination","Navigation",{E("In"),EntityPin(),V("Position")},{E("Then")});
add("SetWorldDestination", "Navigation", { E("In"), EntityPin(), V("Position") }, { E("Then"), B("Success") });
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
                auto* p = FindPin(it->second.Inputs, key);
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
                    auto& p = n.Kind.starts_with("Get.") ? it->second.Outputs.back() : it->second.Inputs.back();
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
            auto* a = Find(l.From);
            auto* b = Find(l.To);
            if (!a || !b)
            {
                error(l.To, "Link references missing node");
                continue;
            }
            auto ai = Catalog().find(a->Kind), bi = Catalog().find(b->Kind);
            if (ai == Catalog().end() || bi == Catalog().end()) continue;
            auto* o = FindPin(ai->second.Outputs, l.Output);
            auto* i = FindPin(bi->second.Inputs, l.Input);
            if (!o || !i || o->Kind != i->Kind)
            {
                error(l.To, "Link pin types differ or pin missing");
                continue;
            }
            if (i->Kind != Type::Exec && !inputs.insert({ l.To, l.Input }).second) error(l.To, "Data input already linked");
            if (i->Kind == Type::Exec && !execOutputs.insert({ l.From, l.Output }).second) error(l.From, "Execution output already linked; use Sequence");
            if (l.Input == "Name" && (b->Kind == "Function" || b->Kind == "Call" || b->Kind.starts_with("Get.") || b->Kind.starts_with("Set."))) error(l.To, "Variable name must be a literal");
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
            for (int i = 1; i <= 7; ++i)
                if (it.value().at("Type") == TypeName((Type)i))
                {
                    g.Variables[it.key()] = { (Type)i, it.value().at("Default") };
                    found = true;
                }
            Require(found, "Unknown variable type");
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
        if (Nodes.size() > 4096 || Links.size() > 8192 || Variables.size() > 256) return false;
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
        for (size_t i = 0; i < m_Graph.Nodes.size(); ++i)
        {
            auto& n = m_Graph.Nodes[i];
            m_NodeIndex[n.ID] = i;
            if (n.Kind.starts_with("Event.")) m_Events[n.Kind.substr(6)].push_back(n.ID);
            if (n.Kind == "Function") m_Functions[n.Inputs.contains("Name") ? n.Inputs.at("Name").get<std::string>() : "MyFunction"] = n.ID;
        }
        for (auto& l : m_Graph.Links)
        {
            auto& def = Catalog().at(m_Graph.Find(l.From)->Kind);
            auto* pin = FindPin(def.Outputs, l.Output);
            if (pin->Kind == Type::Exec) m_OutputLinks[{ l.From, l.Output }] = l;
            else m_InputLinks[{ l.To, l.Input }] = l;
        }
        for (auto& [name, v] : m_Graph.Variables)
        {
            auto it = overrides.is_object() ? overrides.find(name) : overrides.end();
            m_Variables[name] = it != overrides.end() && Matches(v.Kind, *it) ? *it : v.Default;
        }
        return true;
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
    Value Machine::Input(const Node& n, const std::string& pin, Task& t, std::set<uint32_t>& active)
    {
        auto linked = m_InputLinks.find({ n.ID, pin });
        if (linked != m_InputLinks.end()) return Evaluate(linked->second.From, linked->second.Output, t, active);
        auto it = n.Inputs.find(pin);
        if (it != n.Inputs.end()) return it->second;
        auto* p = FindPin(Catalog().at(n.Kind).Inputs, pin);
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
            r = it == t.Event.end() ? FindPin(Catalog().at(k).Outputs, output)->Default : *it;
        }
        else if (k.starts_with("Constant.")) r = in("Value");
        else if (k.starts_with("Get.")) r = m_Variables.at(in("Name").get<std::string>());
        else if (!Catalog().at(k).Pure)
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
            for (auto& p : Catalog().at(k).Inputs) args[p.Name] = in(p.Name.c_str());
            r = m_Bridge(k, args);
            if (r.is_object())
            {
                Require(r.contains(output), "Engine bridge output missing");
                r = r.at(output);
            }
        }
        auto* p = FindPin(Catalog().at(k).Outputs, output);
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
            Require(m_Tasks.size() < 128, "Pending event limit");
            auto event = m_Events.find(name);
            if (event == m_Events.end()) return;
            for (auto id : event->second)
            {
                Require(m_Tasks.size() < 128, "Pending event limit");
                Task t;
                t.Event = payload;
                t.Stack.push_back({ id });
                m_Tasks.push_back(std::move(t));
            }
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
                while (!t.Stack.empty() && t.Wait <= 0 && !Paused)
                {
                    Frame f = t.Stack.back();
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
                    if (k == "For")
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
                            t.Results[n->ID] = { { "Index", index } };
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
                            auto* loop = Find(frame.Node);
                            if (loop && (loop->Kind == "For" || loop->Kind == "While"))
                            {
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
                        Push(t, n->ID, "Then");
                    }
                    else if (k == "Call")
                    {
                        auto name = in("Name");
                        Push(t, n->ID, "Then");
                        Require(t.Stack.size() < 1024, "Function call stack limit");
                        t.Stack.push_back({ m_Functions.at(name.get<std::string>()) });
                    }
                    else if (k == "Function" || k.starts_with("Event.")) Push(t, n->ID, "Then");
                    else
                    {
                        Require(!Catalog().at(k).Pure, "Pure node in execution chain");
                        Require((bool)m_Bridge, "No engine bridge for " + k);
                        std::map<std::string, Value> args;
                        for (auto& p : Catalog().at(k).Inputs)
                            if (p.Kind != Type::Exec) args[p.Name] = in(p.Name.c_str());
                        Value result = m_Bridge(k, args);
                        if (k == "SetWorldDestination" || k == "CompleteNavTraversal" || k == "CancelNavTraversal")
                        {
                            Require(Matches(Type::Bool, result), "Invalid navigation operation result");
                            t.Results[n->ID] = { { "Success", result } };
                        }
                        if (k == "SpawnPrefab")
                        {
                            Require(Matches(Type::Entity, result), "Invalid spawned entity");
                            t.Results[n->ID] = { { "Value", result } };
                        }
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
