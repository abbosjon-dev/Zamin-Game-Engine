#include "VisualGraphBytecode.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>

namespace Zamin::Visual::Bytecode {
    namespace {
        constexpr uint64_t FnvOffset = 0xcbf29ce484222325ull, FnvPrime = 0x100000001b3ull;
        uint64_t Fnv(const uint8_t* data, size_t size, uint64_t hash = FnvOffset)
        {
            for (size_t i = 0; i < size; ++i) hash = (hash ^ data[i]) * FnvPrime;
            return hash;
        }
        uint64_t Fnv(const std::string& text, uint64_t hash) { return Fnv(reinterpret_cast<const uint8_t*>(text.data()), text.size(), hash); }

        enum Tag : uint8_t { Null = 0, False, True, Int, Unsigned, Float, String, Array, Object };
        // Limits of a decoded program: a resolved and expanded graph may hold more than one file's 4096 nodes.
        constexpr size_t MaxNodes = 65536, MaxLinks = 131072, MaxMembers = 1024, MaxPins = 256, MaxStrings = 1 << 20, MaxString = 65536;

        class Writer
        {
        public:
            std::vector<uint8_t> Body;
            void U8(uint8_t v) { Body.push_back(v); }
            void Var(uint64_t v)
            {
                while (v >= 0x80)
                {
                    Body.push_back(uint8_t(v) | 0x80);
                    v >>= 7;
                }
                Body.push_back(uint8_t(v));
            }
            void Zig(int64_t v) { Var((uint64_t(v) << 1) ^ uint64_t(v >> 63)); }
            void F64(double v)
            {
                uint64_t bits;
                std::memcpy(&bits, &v, 8);
                for (int i = 0; i < 8; ++i) Body.push_back(uint8_t(bits >> (8 * i)));
            }
            void S(const std::string& s)
            {
                auto [it, added] = m_Index.try_emplace(s, uint32_t(Table.size()));
                if (added) Table.push_back(s);
                Var(it->second);
            }
            void Val(const Value& v)
            {
                switch (v.type())
                {
                    case Value::value_t::null: U8(Null); break;
                    case Value::value_t::boolean: U8(v.get<bool>() ? True : False); break;
                    case Value::value_t::number_integer:
                        U8(Int);
                        Zig(v.get<int64_t>());
                        break;
                    case Value::value_t::number_unsigned:
                        U8(Unsigned);
                        Var(v.get<uint64_t>());
                        break;
                    case Value::value_t::number_float:
                        U8(Float);
                        F64(v.get<double>());
                        break;
                    case Value::value_t::string:
                        U8(String);
                        S(v.get_ref<const std::string&>());
                        break;
                    case Value::value_t::array:
                        U8(Array);
                        Var(v.size());
                        for (const auto& item : v) Val(item);
                        break;
                    case Value::value_t::object:
                        U8(Object);
                        Var(v.size());
                        for (auto it = v.begin(); it != v.end(); ++it)
                        {
                            S(it.key());
                            Val(it.value());
                        }
                        break;
                    default: throw std::runtime_error("Graph value of an unsupported JSON type");
                }
            }
            void P(const Pin& p)
            {
                S(p.Name);
                U8(uint8_t(p.Kind));
                S(p.Enum);
                Val(p.Default);
            }
            void Pins(const std::vector<Pin>& pins)
            {
                Var(pins.size());
                for (const auto& p : pins) P(p);
            }
            std::vector<std::string> Table;
        private:
            std::map<std::string, uint32_t> m_Index;
        };

        class Reader
        {
        public:
            Reader(const uint8_t* data, size_t size) : m_Data(data), m_Size(size) {}
            [[noreturn]] void Fail(const char* what) const { throw std::runtime_error(std::string("Cooked graph damaged: ") + what); }
            uint8_t U8()
            {
                if (m_At >= m_Size) Fail("truncated");
                return m_Data[m_At++];
            }
            uint64_t Var()
            {
                uint64_t v = 0;
                for (int shift = 0; shift < 64; shift += 7)
                {
                    const uint8_t b = U8();
                    v |= uint64_t(b & 0x7f) << shift;
                    if (!(b & 0x80)) return v;
                }
                Fail("bad varint");
            }
            size_t Count(size_t limit)
            {
                const uint64_t n = Var();
                if (n > limit) Fail("count over its limit");
                return size_t(n);
            }
            int64_t Zig()
            {
                const uint64_t v = Var();
                return int64_t(v >> 1) ^ -int64_t(v & 1);
            }
            double F64()
            {
                if (m_Size - m_At < 8) Fail("truncated");
                uint64_t bits = 0;
                for (int i = 0; i < 8; ++i) bits |= uint64_t(m_Data[m_At++]) << (8 * i);
                double v;
                std::memcpy(&v, &bits, 8);
                return v;
            }
            const std::string& S()
            {
                const uint64_t i = Var();
                if (i >= Table.size()) Fail("string index");
                return Table[size_t(i)];
            }
            Value Val(int depth = 0)
            {
                if (depth > 64) Fail("value nesting");
                switch (U8())
                {
                    case Null: return nullptr;
                    case False: return false;
                    case True: return true;
                    case Int: return Zig();
                    case Unsigned: return Var();
                    case Float:
                    {
                        const double d = F64();
                        if (!std::isfinite(d)) Fail("number not finite");
                        return d;
                    }
                    case String: return S();
                    case Array:
                    {
                        Value v = Value::array();
                        const size_t n = Count(1024);
                        for (size_t i = 0; i < n; ++i) v.push_back(Val(depth + 1));
                        return v;
                    }
                    case Object:
                    {
                        Value v = Value::object();
                        const size_t n = Count(256);
                        for (size_t i = 0; i < n; ++i)
                        {
                            const auto& key = S();
                            v[key] = Val(depth + 1);
                        }
                        return v;
                    }
                    default: Fail("value tag");
                }
            }
            Type Kind(bool exec)
            {
                const uint8_t k = U8();
                if (k >= uint8_t(Type::Count) || (!exec && k == uint8_t(Type::Exec))) Fail("pin type");
                return Type(k);
            }
            Pin P(bool exec = false)
            {
                Pin p;
                p.Name = S();
                p.Kind = Kind(exec);
                p.Enum = S();
                p.Default = Val();
                return p;
            }
            std::vector<Pin> Pins(bool exec = false)
            {
                std::vector<Pin> pins(Count(MaxPins));
                for (auto& p : pins) p = P(exec);
                return pins;
            }
            bool End() const { return m_At == m_Size; }
            std::vector<std::string> Table;
        private:
            const uint8_t* m_Data;
            size_t m_Size, m_At = 0;
        };

        void Put32(std::vector<uint8_t>& out, uint32_t v)
        {
            for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
        }
        void Put64(std::vector<uint8_t>& out, uint64_t v)
        {
            for (int i = 0; i < 8; ++i) out.push_back(uint8_t(v >> (8 * i)));
        }
        uint64_t Get(const uint8_t* p, int bytes)
        {
            uint64_t v = 0;
            for (int i = 0; i < bytes; ++i) v |= uint64_t(p[i]) << (8 * i);
            return v;
        }
        const Pin* FindPin(const std::vector<Pin>& pins, const std::string& name)
        {
            for (const auto& p : pins)
                if (p.Name == name) return &p;
            return nullptr;
        }
    }

    uint64_t CatalogHash()
    {
        static const uint64_t hash = [] {
            uint64_t h = Fnv("Zamin.VisualGraph.Bytecode|" + std::to_string(FormatVersion) + "|" + std::to_string(Graph::FormatVersion), FnvOffset);
            for (const auto& [kind, definition] : Catalog())
            {
                h = Fnv(kind + "|" + definition.Category + (definition.Pure ? "|pure" : "|exec"), h);
                for (const auto* pins : { &definition.Inputs, &definition.Outputs })
                {
                    h = Fnv(pins == &definition.Inputs ? "<" : ">", h);
                    for (const auto& p : *pins) h = Fnv(p.Name + ":" + TypeName(p.Kind) + "=" + p.Default.dump(), h);
                }
            }
            return h;
        }();
        return hash;
    }

    bool IsCooked(const uint8_t* data, size_t size) { return size >= HeaderSize && std::memcmp(data, Magic, 4) == 0; }

    bool IsCookedFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        char magic[4]{};
        return in.read(magic, 4) && std::memcmp(magic, Magic, 4) == 0;
    }

    CompileResult Compile(const Graph& graph)
    {
        CompileResult result;
        try
        {
            result.Warnings = graph.Warnings();
        }
        catch (const std::exception&)
        {
        }
        Program program;
        if (!Machine::Prepare(graph, program, result.Error, &result.Errors)) return result;
        const Graph& g = program.Runnable;
        Writer w;
        auto variable = [&](const Variable& v) {
            w.U8(uint8_t(v.Kind));
            w.S(v.Enum);
            w.Val(v.Default);
        };
        try
        {
            w.Var(g.Variables.size());
            for (const auto& [name, v] : g.Variables)
            {
                w.S(name);
                variable(v);
            }
            w.Var(g.Enums.size());
            for (const auto& [name, e] : g.Enums)
            {
                w.S(name);
                w.S(e.Source);
                w.Var(e.Entries.size());
                for (const auto& entry : e.Entries)
                {
                    w.S(entry.Name);
                    w.Zig(entry.Value);
                }
            }
            w.Var(g.Structs.size());
            for (const auto& [name, s] : g.Structs)
            {
                w.S(name);
                w.Pins(s.Fields);
            }
            w.Var(g.Functions.size());
            for (const auto& [name, f] : g.Functions)
            {
                w.S(name);
                w.Pins(f.Inputs);
                w.Pins(f.Outputs);
                w.Var(f.Locals.size());
                for (const auto& [local, v] : f.Locals)
                {
                    w.S(local);
                    variable(v);
                }
            }
            w.Var(g.Natives.size());
            for (const auto& [key, n] : g.Natives)
            {
                w.S(key);
                w.U8(uint8_t((n.Static ? 1 : 0) | (n.Pure ? 2 : 0)));
                w.S(n.Tooltip);
                w.Pins(n.Inputs);
                w.Pins(n.Outputs);
            }
            // The pin descriptions, each once; nodes refer to them by index.
            std::map<const Definition*, uint32_t> index;
            std::vector<const Definition*> table;
            for (const auto& d : program.Definitions)
                if (index.try_emplace(d.get(), uint32_t(table.size())).second) table.push_back(d.get());
            w.Var(table.size());
            for (const auto* d : table)
            {
                w.S(d->Name);
                w.S(d->Category);
                w.U8(d->Pure ? 1 : 0);
                w.Pins(d->Inputs);
                w.Pins(d->Outputs);
            }
            w.Var(g.Nodes.size());
            for (size_t i = 0; i < g.Nodes.size(); ++i)
            {
                const auto& n = g.Nodes[i];
                w.Var(n.ID);
                w.S(n.Kind);
                w.Var(index.at(program.Definitions[i].get()));
                w.Var(n.Inputs.size());
                for (const auto& [key, value] : n.Inputs)
                {
                    w.S(key);
                    w.Val(value);
                }
            }
            w.Var(g.Links.size());
            for (const auto& l : g.Links)
            {
                w.Var(l.From);
                w.S(l.Output);
                w.Var(l.To);
                w.S(l.Input);
            }
            w.Var(program.MacroOrigins.size());
            for (const auto& [id, origin] : program.MacroOrigins)
            {
                w.Var(id);
                w.Var(origin.first);
                w.Var(origin.second);
            }
            w.Var(program.SourceMacros.size());
            for (const auto& [id, macro] : program.SourceMacros)
            {
                w.Var(id);
                w.S(macro);
            }
            w.Var(g.Inherited.Origins.size());
            for (const auto& [id, origin] : g.Inherited.Origins)
            {
                w.Var(id);
                w.S(origin.first);
                w.Var(origin.second);
            }
            w.Var(g.Inherited.OverriddenEvents.size());
            for (auto id : g.Inherited.OverriddenEvents) w.Var(id);
            w.Var(g.Inherited.ParentEvents.size());
            for (const auto& [id, handlers] : g.Inherited.ParentEvents)
            {
                w.Var(id);
                w.Var(handlers.size());
                for (auto handler : handlers) w.Var(handler);
            }
            // Payload: the string table, then the body.
            Writer payload;
            payload.Var(w.Table.size());
            for (const auto& text : w.Table)
            {
                payload.Var(text.size());
                payload.Body.insert(payload.Body.end(), text.begin(), text.end());
            }
            payload.Body.insert(payload.Body.end(), w.Body.begin(), w.Body.end());
            auto& out = result.Bytes;
            out.insert(out.end(), Magic, Magic + 4);
            Put32(out, FormatVersion);
            Put64(out, CatalogHash());
            Put64(out, Fnv(payload.Body.data(), payload.Body.size()));
            Put32(out, uint32_t(payload.Body.size()));
            out.insert(out.end(), payload.Body.begin(), payload.Body.end());
            if (out.size() > MaxBytes) throw std::runtime_error("Cooked graph exceeds 8 MiB");
        }
        catch (const std::exception& e)
        {
            result.Error = e.what();
            result.Errors.push_back({ 0, e.what() });
            result.Bytes.clear();
            return result;
        }
        result.Ok = true;
        result.Nodes = g.Nodes.size();
        result.Links = g.Links.size();
        std::set<const Definition*> distinct;
        for (const auto& d : program.Definitions) distinct.insert(d.get());
        result.Definitions = distinct.size();
        return result;
    }

    bool Read(const uint8_t* data, size_t size, Program& out, std::string& error)
    {
        out = {};
        try
        {
            if (!IsCooked(data, size)) throw std::runtime_error("Not a cooked graph");
            if (size > MaxBytes) throw std::runtime_error("Cooked graph exceeds 8 MiB");
            const auto version = uint32_t(Get(data + 4, 4));
            if (version != FormatVersion) throw std::runtime_error("Cooked graph format " + std::to_string(version) + " (this engine reads " + std::to_string(FormatVersion) + "): export the game again");
            if (Get(data + 8, 8) != CatalogHash()) throw std::runtime_error("Cooked graph made by another engine version (its node catalogue differs): export the game again");
            const uint64_t hash = Get(data + 16, 8);
            const size_t payload = size_t(Get(data + 24, 4));
            if (payload != size - HeaderSize) throw std::runtime_error("Cooked graph damaged: size");
            const uint8_t* body = data + HeaderSize;
            if (Fnv(body, payload) != hash) throw std::runtime_error("Cooked graph damaged: checksum");
            Reader r(body, payload);
            const size_t strings = r.Count(MaxStrings);
            r.Table.reserve(strings);
            for (size_t i = 0; i < strings; ++i)
            {
                const size_t length = r.Count(MaxString);
                std::string text;
                text.resize(length);
                for (size_t c = 0; c < length; ++c) text[c] = char(r.U8());
                r.Table.push_back(std::move(text));
            }
            Graph& g = out.Runnable;
            auto variable = [&]() {
                Variable v;
                v.Kind = r.Kind(false);
                v.Enum = r.S();
                v.Default = r.Val();
                return v;
            };
            for (size_t i = 0, n = r.Count(MaxMembers); i < n; ++i)
            {
                const auto& name = r.S();
                g.Variables[name] = variable();
            }
            for (size_t i = 0, n = r.Count(MaxMembers); i < n; ++i)
            {
                const auto& name = r.S();
                EnumDefinition e;
                e.Source = r.S();
                e.Entries.resize(r.Count(256));
                for (auto& entry : e.Entries)
                {
                    entry.Name = r.S();
                    entry.Value = r.Zig();
                }
                g.Enums[name] = std::move(e);
            }
            for (size_t i = 0, n = r.Count(MaxMembers); i < n; ++i)
            {
                const auto& name = r.S();
                g.Structs[name].Fields = r.Pins();
            }
            for (size_t i = 0, n = r.Count(MaxMembers); i < n; ++i)
            {
                const auto& name = r.S();
                FunctionSignature f;
                f.Inputs = r.Pins();
                f.Outputs = r.Pins();
                for (size_t l = 0, count = r.Count(MaxPins); l < count; ++l)
                {
                    const auto& local = r.S();
                    f.Locals[local] = variable();
                }
                g.Functions[name] = std::move(f);
            }
            for (size_t i = 0, n = r.Count(MaxMembers); i < n; ++i)
            {
                const auto& key = r.S();
                NativeSignature s;
                const uint8_t flags = r.U8();
                s.Static = flags & 1;
                s.Pure = flags & 2;
                s.Tooltip = r.S();
                s.Inputs = r.Pins();
                s.Outputs = r.Pins();
                g.Natives[key] = std::move(s);
            }
            std::vector<std::shared_ptr<const Definition>> definitions(r.Count(MaxNodes));
            for (auto& d : definitions)
            {
                auto definition = std::make_shared<Definition>();
                definition->Name = r.S();
                definition->Category = r.S();
                definition->Pure = r.U8() != 0;
                definition->Inputs = r.Pins(true);
                definition->Outputs = r.Pins(true);
                d = std::move(definition);
            }
            const size_t nodes = r.Count(MaxNodes);
            g.Nodes.resize(nodes);
            out.Definitions.resize(nodes);
            std::map<uint32_t, size_t> ids;
            for (size_t i = 0; i < nodes; ++i)
            {
                auto& n = g.Nodes[i];
                const uint64_t id = r.Var();
                if (id == 0 || id > UINT32_MAX || !ids.try_emplace(uint32_t(id), i).second) r.Fail("node ID");
                n.ID = uint32_t(id);
                n.Kind = r.S();
                const uint64_t d = r.Var();
                if (d >= definitions.size()) r.Fail("description index");
                out.Definitions[i] = definitions[size_t(d)];
                if (!Catalog().contains(n.Kind)) r.Fail("unknown node kind");
                for (size_t k = 0, count = r.Count(MaxPins); k < count; ++k)
                {
                    const auto& key = r.S();
                    if (!FindPin(out.Definitions[i]->Inputs, key)) r.Fail("literal of a missing pin");
                    n.Inputs[key] = r.Val();
                }
            }
            auto node = [&](uint64_t id) {
                auto it = id <= UINT32_MAX ? ids.find(uint32_t(id)) : ids.end();
                if (it == ids.end()) r.Fail("link or table names a missing node");
                return it->second;
            };
            const size_t links = r.Count(MaxLinks);
            g.Links.resize(links);
            for (auto& l : g.Links)
            {
                const size_t from = node(r.Var());
                l.From = g.Nodes[from].ID;
                l.Output = r.S();
                const size_t to = node(r.Var());
                l.To = g.Nodes[to].ID;
                l.Input = r.S();
                const Pin* o = FindPin(out.Definitions[from]->Outputs, l.Output);
                const Pin* in = FindPin(out.Definitions[to]->Inputs, l.Input);
                if (!o || !in || !SameType(*o, *in)) r.Fail("link pins");
            }
            for (size_t i = 0, n = r.Count(MaxNodes); i < n; ++i)
            {
                const auto id = g.Nodes[node(r.Var())].ID;
                const auto instance = uint32_t(r.Var());
                const auto source = uint32_t(r.Var());
                out.MacroOrigins[id] = { instance, source };
            }
            for (size_t i = 0, n = r.Count(MaxNodes); i < n; ++i)
            {
                const auto id = uint32_t(r.Var());
                out.SourceMacros[id] = r.S();
            }
            for (size_t i = 0, n = r.Count(MaxNodes); i < n; ++i)
            {
                // (an inherited macro instance is expanded: its ID stays in the table for error messages only)
                const auto id = uint32_t(r.Var());
                const auto& path = r.S();
                g.Inherited.Origins[id] = { path, uint32_t(r.Var()) };
            }
            for (size_t i = 0, n = r.Count(MaxNodes); i < n; ++i) g.Inherited.OverriddenEvents.insert(g.Nodes[node(r.Var())].ID);
            for (size_t i = 0, n = r.Count(MaxNodes); i < n; ++i)
            {
                const auto id = g.Nodes[node(r.Var())].ID;
                auto& handlers = g.Inherited.ParentEvents[id];
                for (size_t h = 0, count = r.Count(MaxNodes); h < count; ++h) handlers.push_back(g.Nodes[node(r.Var())].ID);
            }
            if (!r.End()) r.Fail("trailing bytes");
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            out = {};
            return false;
        }
    }

    bool ReadFile(const std::filesystem::path& path, Program& out, std::string& error)
    {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || size > MaxBytes)
        {
            error = ec ? "Cooked graph could not be read" : "Cooked graph exceeds 8 MiB";
            return false;
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        std::ifstream in(path, std::ios::binary);
        if (!in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
        {
            error = "Cooked graph could not be read";
            return false;
        }
        return Read(bytes.data(), bytes.size(), out, error);
    }
}
