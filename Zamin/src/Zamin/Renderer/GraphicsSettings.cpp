#include "Zamin/Renderer/GraphicsSettings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <format>

namespace Zamin {

    using ordered_json = nlohmann::ordered_json;

    const char* GraphicsDebugContextToString(GraphicsDebugContext mode)
    {
        switch (mode)
        {
            case GraphicsDebugContext::On:  return "On";
            case GraphicsDebugContext::Off: return "Off";
            default:                        return "Auto";
        }
    }

    bool GraphicsDebugContextFromString(std::string_view text, GraphicsDebugContext& out)
    {
        if (text == "Auto") { out = GraphicsDebugContext::Auto; return true; }
        if (text == "On")   { out = GraphicsDebugContext::On; return true; }
        if (text == "Off")  { out = GraphicsDebugContext::Off; return true; }
        return false;
    }

    const std::vector<GraphicsSettings::Version>& GraphicsSettings::GetSupportedVersions()
    {
        static const std::vector<Version> s_Versions = { { 4, 1, "4.1" }, { 4, 3, "4.3" }, { 4, 5, "4.5" }, { 4, 6, "4.6" } };
        return s_Versions;
    }

    static bool IsSupported(int major, int minor)
    {
        const auto& versions = GraphicsSettings::GetSupportedVersions();
        return std::any_of(versions.begin(), versions.end(), [&](const auto& v) { return v.Major == major && v.Minor == minor; });
    }

    int GraphicsSettings::SanitizeSamples(int samples)
    {
        if (samples < 2)
            return 0;
        int power = 2;
        while (power * 2 <= samples && power * 2 <= MaxMSAASamples)
            power *= 2;
        return power;
    }

    void GraphicsSettings::Sanitize()
    {
        if (!IsSupported(GLMajor, GLMinor))
        {
            GLMajor = DefaultMajor;
            GLMinor = DefaultMinor;
        }
        if (DebugContext != GraphicsDebugContext::Auto && DebugContext != GraphicsDebugContext::On && DebugContext != GraphicsDebugContext::Off)
            DebugContext = GraphicsDebugContext::Auto;
        MSAASamples = SanitizeSamples(MSAASamples);
    }

    std::string GraphicsSettings::GetVersionString() const { return std::format("{}.{}", GLMajor, GLMinor); }

    bool GraphicsSettings::ParseVersion(std::string_view text, int& major, int& minor)
    {
        // The first two digits ("4.6", "46", "4.6 core", "GL 4.5").
        int digits[2] = { -1, -1 };
        int count = 0;
        for (char c : text)
        {
            if (std::isdigit((unsigned char)c))
            {
                if (count == 2)
                    return false; // "4.10", "460"
                digits[count++] = c - '0';
            }
            else if (count == 1 && c != '.')
                return false;
            else if (count == 2 && c == '.')
                return false;
        }
        if (count != 2 || !IsSupported(digits[0], digits[1]))
            return false;
        major = digits[0];
        minor = digits[1];
        return true;
    }

    bool GraphicsSettings::WantsDebugContext() const
    {
        switch (DebugContext)
        {
            case GraphicsDebugContext::On:  return true;
            case GraphicsDebugContext::Off: return false;
            default:
#ifdef NDEBUG
                return false;
#else
                return true;
#endif
        }
    }

    std::string GraphicsSettings::ToJson() const
    {
        ordered_json j;
        j["OpenGL"] = GetVersionString();
        j["DebugContext"] = GraphicsDebugContextToString(DebugContext);
        j["MSAA"] = MSAASamples;
        j["SRGBFramebuffer"] = SRGBFramebuffer;
        return j.dump(4);
    }

    bool GraphicsSettings::FromJson(const std::string& text, GraphicsSettings& out)
    {
        ordered_json j = ordered_json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return false;
        GraphicsSettings s;
        if (auto it = j.find("OpenGL"); it != j.end() && it->is_string())
            ParseVersion(it->get<std::string>(), s.GLMajor, s.GLMinor);
        if (auto it = j.find("DebugContext"); it != j.end() && it->is_string())
            GraphicsDebugContextFromString(it->get<std::string>(), s.DebugContext);
        if (auto it = j.find("MSAA"); it != j.end() && it->is_number())
            s.MSAASamples = (int)std::clamp(it->get<double>(), 0.0, (double)MaxMSAASamples);
        if (auto it = j.find("SRGBFramebuffer"); it != j.end() && it->is_boolean())
            s.SRGBFramebuffer = it->get<bool>();
        s.Sanitize();
        out = s;
        return true;
    }

    std::string GraphicsContextInfo::Summary() const
    {
        if (!Valid)
            return "OpenGL konteksti yo'q";
        std::string extras;
        auto add = [&](const std::string& part) { extras += (extras.empty() ? "" : ", ") + part; };
        if (DebugContext)
            add("debug");
        if (Samples > 0)
            add(std::format("MSAA {}x", Samples));
        if (SRGB)
            add("sRGB");
        return std::format("OpenGL {}.{} {}{}", Major, Minor, CoreProfile ? "core" : "compat", extras.empty() ? "" : " (" + extras + ")");
    }

}
