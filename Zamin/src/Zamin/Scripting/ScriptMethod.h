#pragma once

/// @file
/// @brief Reflected C++ methods and free functions that Visual Script graphs call (ZM_METHOD, ZM_FUNCTION),
/// and the C++ enums / structs they take and return (ZM_SCRIPT_ENUM, ZM_SCRIPT_STRUCT).
///
/// @code
///   ZM_SCRIPT_ENUM(Medal, "Bronze", "Silver", {"Gold", 5})
///   ZM_SCRIPT_STRUCT(Score, ZM_STRUCT_FIELD(Score, Points), ZM_STRUCT_FIELD(Score, Rank))
///   ZM_REGISTER_SCRIPT(Scoreboard, ZM_FIELD(Scoreboard, Bonus),
///       ZM_METHOD(Scoreboard, Award, "Points").Tooltip("Adds points, returns the new total"),
///       ZM_PURE_METHOD(Scoreboard, RankOf, "Points"))
///   ZM_REGISTER_FUNCTIONS(Workshop, ZM_PURE_FUNCTION(Hypot, "X", "Y"))
/// @endcode
///
/// Parameter and result types: float, double, int, int64_t, bool, std::string, glm::vec2/3/4, EntityRef, a C++ enum
/// registered with ZM_SCRIPT_ENUM, a struct registered with ZM_SCRIPT_STRUCT and std::vector of any of these
/// (parameters by value or const reference). The values cross as Visual Script values (nlohmann::json): numbers as
/// double (an int parameter takes whole numbers within its range only), vectors as arrays, entities as UUIDs,
/// enums as their integer, structs as objects of their fields.

#include "Zamin/Scripting/ScriptField.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace Zamin {

    class ScriptableEntity;
    using ScriptValue = nlohmann::json;

    /// The kind of a reflected method's parameter or result (the Visual Script pin types it maps to).
    enum class ScriptValueKind : uint8_t { Void = 0, Bool, Number, String, Vec2, Vec3, Vec4, Entity, Enum, Struct };

    struct ScriptValueType
    {
        ScriptValueKind Kind = ScriptValueKind::Void;
        bool Array = false;    ///< std::vector<...> of Kind
        bool Integer = false;  ///< a Number from an integer type: whole numbers within its range only
        std::string Named;     ///< the enum / struct name (ZM_SCRIPT_ENUM / ZM_SCRIPT_STRUCT)
        bool operator==(const ScriptValueType&) const = default;
    };

    /// "Number", "Number[]", "Enum Medal", "Struct Score[]" ...
    std::string ScriptValueTypeName(const ScriptValueType& type);

    struct ScriptParameter
    {
        std::string Name;
        ScriptValueType Type;
    };

    /// A reflected method (ZM_METHOD) or free function (ZM_FUNCTION). An instance method runs on the live script
    /// instance of its class on the graph's Target entity; a static method or a free function needs none.
    struct ScriptMethodInfo
    {
        std::string Name;
        std::vector<ScriptParameter> Parameters;
        ScriptValueType Result;            ///< Kind Void: no result
        std::string ResultName = "Value";
        bool IsPure = false;               ///< a data node (no execution pins); needs a result
        bool Static = false;               ///< no instance (a static member or a free function)
        std::string TooltipText;
        /// Calls it with one value per parameter (each already of its parameter's type); returns the result
        /// (null for none). Throws std::runtime_error for a value it cannot take.
        ScriptValue (*Invoke)(ScriptableEntity* self, const ScriptValue* args) = nullptr;

        /// A data node: called when a graph reads its result, may be called any number of times.
        ScriptMethodInfo& Pure(bool pure = true) { IsPure = pure; return *this; }
        ScriptMethodInfo& Tooltip(std::string text) { TooltipText = std::move(text); return *this; }
        /// The result pin's name (default "Value").
        ScriptMethodInfo& Returns(std::string name) { ResultName = std::move(name); return *this; }
        /// The parameters' names, in order (also given in ZM_METHOD's extra arguments).
        ScriptMethodInfo& Params(std::initializer_list<const char*> names);
    };

    /// A C++ enum graphs can use (ZM_SCRIPT_ENUM): its named values.
    struct ScriptEnumInfo
    {
        std::string Name;
        std::vector<ScriptEnumOption> Options; ///< values resolved (AutoValue false)
    };

    struct ScriptStructField
    {
        std::string Name;
        ScriptValueType Type;
        ScriptValue (*Get)(const void* object) = nullptr;
        void (*Set)(void* object, const ScriptValue& value) = nullptr;
    };

    /// A C++ struct graphs can use (ZM_SCRIPT_STRUCT): a graph struct of the same name with these fields.
    struct ScriptStructInfo
    {
        std::string Name;
        std::vector<ScriptStructField> Fields;
    };

    /// Free functions under one library name (ZM_REGISTER_FUNCTIONS): the node library shows "Library.Function".
    struct ScriptLibraryInfo
    {
        std::string Name;
        std::vector<ScriptMethodInfo> Functions;
    };

    /// One ZM_REGISTER_SCRIPT entry: a field or a method.
    struct ScriptMember
    {
        ScriptMember(const ScriptFieldInfo& field) : IsField(true), Field(field) {}
        ScriptMember(const ScriptMethodInfo& method) : IsField(false), Method(method) {}
        bool IsField;
        ScriptFieldInfo Field;
        ScriptMethodInfo Method;
    };

    // ---------------------------------------------------------------- value conversion
    namespace ScriptReflection {

        template<typename T> struct IsVector : std::false_type {};
        template<typename T, typename A> struct IsVector<std::vector<T, A>> : std::true_type { using Element = T; };

        [[noreturn]] void Fail(const std::string& message);
        double Number(const ScriptValue& value);

        // ZM_SCRIPT_ENUM / ZM_SCRIPT_STRUCT declare these for their type (found by argument-dependent lookup).
        template<typename T> concept RegisteredEnum = std::is_enum_v<T> && requires(T* p) { { ZaminScriptEnumName(p) } -> std::convertible_to<const char*>; };
        template<typename T> concept RegisteredStruct = std::is_class_v<T> && requires(T* p) { { ZaminScriptStruct(p) } -> std::convertible_to<const ScriptStructInfo&>; };

        template<typename T>
        struct Marshal
        {
            static_assert(sizeof(T) == 0, "Reflected method/struct field: unsupported type (float, double, int, int64_t, bool, std::string, "
                                          "glm::vec2/3/4, EntityRef, an enum with ZM_SCRIPT_ENUM, a struct with ZM_SCRIPT_STRUCT, std::vector of these)");
        };
        template<> struct Marshal<bool>
        {
            static ScriptValueType Type() { return { ScriptValueKind::Bool }; }
            static bool From(const ScriptValue& v) { if (!v.is_boolean()) Fail("expected a Bool"); return v.get<bool>(); }
            static ScriptValue To(bool v) { return v; }
        };
        template<typename F> requires std::is_floating_point_v<F>
        struct Marshal<F>
        {
            static ScriptValueType Type() { return { ScriptValueKind::Number }; }
            static F From(const ScriptValue& v)
            {
                const double n = Number(v);
                if (std::abs(n) > double(std::numeric_limits<F>::max())) Fail("number outside the float range");
                return F(n);
            }
            static ScriptValue To(F v)
            {
                if (!std::isfinite(v)) Fail("the C++ result is not a finite number");
                return double(v);
            }
        };
        template<typename I> requires (std::is_integral_v<I> && !std::is_same_v<I, bool> && sizeof(I) <= 8)
        struct Marshal<I>
        {
            static ScriptValueType Type() { return { ScriptValueKind::Number, false, true }; }
            static I From(const ScriptValue& v)
            {
                const double n = Number(v);
                // Exact limits: an int64 beyond 2^53 is not a graph number anyway.
                const double lo = std::is_signed_v<I> ? std::max(double(std::numeric_limits<I>::min()), -9007199254740992.0) : 0.0;
                const double hi = std::min(double(std::numeric_limits<I>::max()), 9007199254740992.0);
                if (std::floor(n) != n || n < lo || n > hi) Fail("expected a whole number within the integer's range");
                return I(n);
            }
            static ScriptValue To(I v)
            {
                if (double(v) > 9007199254740992.0 || (std::is_signed_v<I> && double(v) < -9007199254740992.0)) Fail("the C++ result exceeds 2^53");
                return double(v);
            }
        };
        template<> struct Marshal<std::string>
        {
            static ScriptValueType Type() { return { ScriptValueKind::String }; }
            static std::string From(const ScriptValue& v) { if (!v.is_string()) Fail("expected a String"); return v.get<std::string>(); }
            static ScriptValue To(const std::string& v)
            {
                if (v.size() > 65536) Fail("the C++ string result exceeds 64 KiB");
                return v;
            }
        };
        template<glm::length_t N, glm::qualifier Q>
        struct Marshal<glm::vec<N, float, Q>>
        {
            using V = glm::vec<N, float, Q>;
            static ScriptValueType Type() { return { N == 2 ? ScriptValueKind::Vec2 : N == 3 ? ScriptValueKind::Vec3 : ScriptValueKind::Vec4 }; }
            static V From(const ScriptValue& v)
            {
                if (!v.is_array() || v.size() != size_t(N)) Fail("expected a vector of " + std::to_string(N) + " numbers");
                V out{};
                for (int i = 0; i < N; ++i) out[i] = Marshal<float>::From(v[size_t(i)]);
                return out;
            }
            static ScriptValue To(const V& v)
            {
                ScriptValue out = ScriptValue::array();
                for (int i = 0; i < N; ++i) out.push_back(Marshal<float>::To(v[i]));
                return out;
            }
        };
        template<> struct Marshal<EntityRef>
        {
            static ScriptValueType Type() { return { ScriptValueKind::Entity }; }
            static EntityRef From(const ScriptValue& v)
            {
                if (!v.is_number_unsigned() && !(v.is_number_integer() && v.get<int64_t>() >= 0)) Fail("expected an Entity");
                return EntityRef{ v.get<uint64_t>() };
            }
            static ScriptValue To(const EntityRef& v) { return v.ID; }
        };
        template<RegisteredEnum E>
        struct Marshal<E>
        {
            static ScriptValueType Type() { return { ScriptValueKind::Enum, false, false, ZaminScriptEnumName((E*)nullptr) }; }
            static E From(const ScriptValue& v)
            {
                if (!v.is_number_integer()) Fail("expected an Enum value");
                return static_cast<E>(v.get<int64_t>());
            }
            static ScriptValue To(E v) { return static_cast<int64_t>(v); }
        };
        template<RegisteredStruct S>
        struct Marshal<S>
        {
            static const ScriptStructInfo& Info() { return ZaminScriptStruct((S*)nullptr); }
            static ScriptValueType Type() { return { ScriptValueKind::Struct, false, false, Info().Name }; }
            static S From(const ScriptValue& v)
            {
                if (!v.is_object()) Fail("expected a Struct " + Info().Name);
                S out{};
                for (const auto& field : Info().Fields)
                {
                    auto it = v.find(field.Name);
                    if (it == v.end()) Fail("struct " + Info().Name + " misses field " + field.Name);
                    field.Set(&out, *it);
                }
                return out;
            }
            static ScriptValue To(const S& v)
            {
                ScriptValue out = ScriptValue::object();
                for (const auto& field : Info().Fields) out[field.Name] = field.Get(&v);
                return out;
            }
        };
        template<typename T, typename A>
        struct Marshal<std::vector<T, A>>
        {
            static_assert(!IsVector<T>::value, "Reflected methods: arrays of arrays are not graph values");
            static ScriptValueType Type()
            {
                auto type = Marshal<T>::Type();
                type.Array = true;
                return type;
            }
            static std::vector<T, A> From(const ScriptValue& v)
            {
                if (!v.is_array() || v.size() > 1024) Fail("expected an array of at most 1024 values");
                std::vector<T, A> out;
                out.reserve(v.size());
                for (const auto& item : v) out.push_back(Marshal<T>::From(item));
                return out;
            }
            static ScriptValue To(const std::vector<T, A>& v)
            {
                if (v.size() > 1024) Fail("the C++ array result exceeds 1024 elements");
                ScriptValue out = ScriptValue::array();
                for (const auto& item : v)
                {
                    if constexpr (std::is_same_v<T, bool>) out.push_back(bool(item));
                    else out.push_back(Marshal<T>::To(item));
                }
                return out;
            }
        };

        template<typename T> using Plain = std::remove_cvref_t<T>;
        template<typename T> constexpr bool ByValueOrConstRef = !std::is_reference_v<T> || std::is_const_v<std::remove_reference_t<T>>;

        // What ZM_METHOD / ZM_FUNCTION take: a member function (const or not), a static member or a free function.
        template<typename F> struct Callable;
        template<typename R, typename... A> struct Callable<R (*)(A...)> { using Result = R; using Args = std::tuple<A...>; static constexpr bool Member = false; };
        template<typename R, typename... A> struct Callable<R (*)(A...) noexcept> : Callable<R (*)(A...)> {};
        template<typename C, typename R, typename... A> struct Callable<R (C::*)(A...)> { using Class = C; using Result = R; using Args = std::tuple<A...>; static constexpr bool Member = true; };
        template<typename C, typename R, typename... A> struct Callable<R (C::*)(A...) const> : Callable<R (C::*)(A...)> {};
        template<typename C, typename R, typename... A> struct Callable<R (C::*)(A...) noexcept> : Callable<R (C::*)(A...)> {};
        template<typename C, typename R, typename... A> struct Callable<R (C::*)(A...) const noexcept> : Callable<R (C::*)(A...)> {};

        template<auto F, typename Owner>
        ScriptValue Invoke(ScriptableEntity* self, const ScriptValue* args)
        {
            using Info = Callable<decltype(F)>;
            using R = typename Info::Result;
            return [&]<size_t... I>(std::index_sequence<I...>) -> ScriptValue {
                auto call = [&]() -> decltype(auto) {
                    if constexpr (Info::Member)
                    {
                        auto* object = static_cast<Owner*>(self);
                        return (object->*F)(Marshal<Plain<std::tuple_element_t<I, typename Info::Args>>>::From(args[I])...);
                    }
                    else
                    {
                        (void)self;
                        return F(Marshal<Plain<std::tuple_element_t<I, typename Info::Args>>>::From(args[I])...);
                    }
                };
                if constexpr (std::is_void_v<R>)
                {
                    call();
                    return nullptr;
                }
                else return Marshal<Plain<R>>::To(call());
            }(std::make_index_sequence<std::tuple_size_v<typename Info::Args>>{});
        }

        template<typename Args, size_t... I>
        std::vector<ScriptParameter> Parameters(std::index_sequence<I...>)
        {
            static_assert((ByValueOrConstRef<std::tuple_element_t<I, Args>> && ...),
                          "Reflected methods take parameters by value or const reference (results: the return value or a struct)");
            return { ScriptParameter{ "Arg" + std::to_string(I + 1), Marshal<Plain<std::tuple_element_t<I, Args>>>::Type() }... };
        }

        template<auto F, typename Owner, typename... Names>
        ScriptMethodInfo Make(const char* name, Names... names)
        {
            using Info = Callable<decltype(F)>;
            using Args = typename Info::Args;
            constexpr size_t arity = std::tuple_size_v<Args>;
            static_assert(sizeof...(Names) == 0 || sizeof...(Names) == arity, "ZM_METHOD / ZM_FUNCTION: name every parameter, or none");
            static_assert((std::is_convertible_v<Names, const char*> && ...), "ZM_METHOD / ZM_FUNCTION: parameter names are strings");
            if constexpr (Info::Member)
                static_assert(std::is_base_of_v<typename Info::Class, Owner>, "ZM_METHOD: the method must belong to the script class");
            ScriptMethodInfo info;
            info.Name = name;
            info.Parameters = Parameters<Args>(std::make_index_sequence<arity>{});
            const char* given[] = { names..., nullptr };
            for (size_t i = 0; i < sizeof...(Names); ++i) info.Parameters[i].Name = given[i];
            if constexpr (!std::is_void_v<typename Info::Result>) info.Result = Marshal<Plain<typename Info::Result>>::Type();
            info.Static = !Info::Member;
            info.Invoke = &Invoke<F, Owner>;
            return info;
        }

        template<typename Struct, typename T>
        ScriptStructField Field(const char* name, ScriptValue (*get)(const void*), void (*set)(void*, const ScriptValue&))
        {
            return { name, Marshal<T>::Type(), get, set };
        }

        // Compile-time check of ZM_PURE_METHOD / ZM_PURE_FUNCTION.
        template<auto Method>
        constexpr void RequireResult()
        {
            static_assert(!std::is_void_v<typename Callable<decltype(Method)>::Result>, "ZM_PURE_METHOD / ZM_PURE_FUNCTION: a pure method needs a result");
        }
    }

    struct ScriptEnumRegistrar
    {
        ScriptEnumRegistrar(const char* name, std::initializer_list<ScriptEnumOption> options);
        ~ScriptEnumRegistrar();
        std::string Name;
    };
    struct ScriptStructRegistrar
    {
        explicit ScriptStructRegistrar(const ScriptStructInfo& info);
        ~ScriptStructRegistrar();
        std::string Name;
    };
    struct ScriptLibraryRegistrar
    {
        ScriptLibraryRegistrar(const char* name, std::vector<ScriptMethodInfo> functions);
        ~ScriptLibraryRegistrar();
        std::string Name;
    };

}

/// A C++ enum that reflected methods take or return (at namespace scope, in the enum's own namespace; a header is
/// fine). Options as in ZM_FIELD_ENUM:  ZM_SCRIPT_ENUM(Medal, "Bronze", "Silver", {"Gold", Medal::Gold})
#define ZM_SCRIPT_ENUM(Enum, ...) \
    [[maybe_unused]] inline const char* ZaminScriptEnumName(Enum*) { return #Enum; } \
    inline ::Zamin::ScriptEnumRegistrar s_ZaminScriptEnum_##Enum{ #Enum, { __VA_ARGS__ } };

/// A field of a ZM_SCRIPT_STRUCT.
#define ZM_STRUCT_FIELD(Struct, Member) \
    ::Zamin::ScriptReflection::Field<Struct, std::remove_cvref_t<decltype(std::declval<Struct&>().Member)>>(#Member, \
        [](const void* s) -> ::Zamin::ScriptValue { return ::Zamin::ScriptReflection::Marshal<std::remove_cvref_t<decltype(std::declval<Struct&>().Member)>>::To(static_cast<const Struct*>(s)->Member); }, \
        [](void* s, const ::Zamin::ScriptValue& v) { static_cast<Struct*>(s)->Member = ::Zamin::ScriptReflection::Marshal<std::remove_cvref_t<decltype(std::declval<Struct&>().Member)>>::From(v); })

/// A C++ struct (default constructible) that reflected methods take or return; graphs see a struct of the same
/// name and fields (at namespace scope, in the struct's own namespace; a header is fine).
#define ZM_SCRIPT_STRUCT(Struct, ...) \
    [[maybe_unused]] inline const ::Zamin::ScriptStructInfo& ZaminScriptStruct(Struct*) \
    { static const ::Zamin::ScriptStructInfo info{ #Struct, { __VA_ARGS__ } }; return info; } \
    inline ::Zamin::ScriptStructRegistrar s_ZaminScriptStruct_##Struct{ ZaminScriptStruct((Struct*)nullptr) };

/// A method of a script class for graphs: an instance method (runs on the Target entity's live instance) or a
/// static member. Extra arguments name the parameters:  ZM_METHOD(Player, Heal, "Amount").Tooltip("...")
/// An execution node by default; .Pure() (or ZM_PURE_METHOD) makes a data node.
#define ZM_METHOD(Class, Method, ...) \
    ::Zamin::ScriptReflection::Make<&Class::Method, Class>(#Method __VA_OPT__(,) __VA_ARGS__)
#define ZM_PURE_METHOD(Class, Method, ...) \
    (::Zamin::ScriptReflection::RequireResult<&Class::Method>(), ZM_METHOD(Class, Method __VA_OPT__(,) __VA_ARGS__).Pure())

/// A free function for graphs (inside ZM_REGISTER_FUNCTIONS):  ZM_FUNCTION(Hypot, "X", "Y")
#define ZM_FUNCTION(Function, ...) \
    ::Zamin::ScriptReflection::Make<&Function, void>(#Function __VA_OPT__(,) __VA_ARGS__)
#define ZM_PURE_FUNCTION(Function, ...) \
    (::Zamin::ScriptReflection::RequireResult<&Function>(), ZM_FUNCTION(Function __VA_OPT__(,) __VA_ARGS__).Pure())

/// Registers free functions under a library name (graphs show "Library.Function"):
///   ZM_REGISTER_FUNCTIONS(Workshop, ZM_PURE_FUNCTION(Hypot, "X", "Y"), ZM_FUNCTION(Log, "Text"))
#define ZM_REGISTER_FUNCTIONS(Library, ...) \
    static ::Zamin::ScriptLibraryRegistrar s_ZaminScriptLibrary_##Library(#Library, { __VA_ARGS__ });
