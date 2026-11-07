#include "Zamin/Utils/FileSystem.h"
#include "Zamin/Core/Log.h"
#include "Zamin/Utils/StringUtils.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>

#if defined(__linux__) || defined(__APPLE__)
    #include <pwd.h>
    #include <unistd.h>
    #include <climits>
#endif

namespace Zamin {

    std::string FileSystem::ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        if (!in)
        {
            ZM_CORE_ERROR("Fayl ochilmadi: '{}'", path.string());
            return {};
        }
        // A folder opens as a stream on Linux (and seeking to its end gives a huge size - ext4:
        // INT64_MAX): there is no text in it, as before.
        std::error_code ec;
        const std::filesystem::file_status status = std::filesystem::status(path, ec);
        if (!ec && std::filesystem::is_directory(status))
            return {};
        // A regular file: read straight into the string at its size (large scene files: no
        // second copy through a stream).
        if (!ec && std::filesystem::is_regular_file(status))
        {
            const uintmax_t size = std::filesystem::file_size(path, ec);
            if (!ec && size > 0 && size <= (uintmax_t)std::numeric_limits<std::streamsize>::max() / 2)
            {
                std::string text;
                try
                {
                    text.resize((size_t)size);
                }
                catch (const std::exception&)
                {
                    ZM_CORE_ERROR("Fayl o'qilmadi (xotira yetmadi): '{}' ({} bayt)", path.string(), size);
                    return {};
                }
                in.read(text.data(), (std::streamsize)size);
                text.resize((size_t)std::max<std::streamsize>(in.gcount(), 0)); // it may have shrunk meanwhile
                return text;
            }
        }
        // Not a regular file of known size (a pipe ...): as a stream.
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    bool FileSystem::WriteTextFile(const std::filesystem::path& path, const std::string& content)
    {
        if (path.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out)
        {
            ZM_CORE_ERROR("Faylga yozib bo'lmadi: '{}'", path.string());
            return false;
        }
        out << content;
        return true;
    }

    bool FileSystem::WriteBinaryFile(const std::filesystem::path& path, const void* data, size_t size)
    {
        if (path.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out)
        {
            ZM_CORE_ERROR("Faylga yozib bo'lmadi: '{}'", path.string());
            return false;
        }
        out.write((const char*)data, (std::streamsize)size);
        return (bool)out;
    }

    std::vector<uint8_t> FileSystem::ReadBinaryFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in)
            return {};
        std::streamsize size = in.tellg();
        in.seekg(0, std::ios::beg);
        std::vector<uint8_t> data((size_t)size);
        in.read(reinterpret_cast<char*>(data.data()), size);
        return data;
    }

    std::filesystem::path FileSystem::GetExecutablePath()
    {
#if defined(__linux__)
        char buf[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0)
        {
            buf[len] = '\0';
            return std::filesystem::path(buf);
        }
#endif
        return std::filesystem::current_path() / "unknown";
    }

    // ------------------------------------------------------------------ paths (lexical)
    namespace {

        constexpr bool IsSeparator(char c) { return c == '/' || c == '\\'; }

        // A path split into its root and normalized parts. The parts point into the text the path
        // was parsed from, which must outlive them.
        struct PathParts
        {
            std::string Root; // "", "/", "C:/" or "C:"
            std::vector<std::string_view> Parts;
            bool HasNul = false;

            bool IsAbsolute() const { return !Root.empty(); }
            bool ClimbsOut() const { return !Parts.empty() && Parts.front() == ".."; }
        };

        PathParts ParsePath(std::string_view text)
        {
            PathParts out;
            out.HasNul = text.find('\0') != std::string_view::npos;
            size_t i = 0;
            // A drive: a letter and ':' followed by a separator or nothing ("a:b" is a file name).
            if (text.size() >= 2 && StringUtils::IsAlpha(text[0]) && text[1] == ':' && (text.size() == 2 || IsSeparator(text[2])))
            {
                out.Root = { StringUtils::ToUpper(text[0]), ':' };
                i = 2;
            }
            if (i < text.size() && IsSeparator(text[i]))
            {
                out.Root += '/';
                while (i < text.size() && IsSeparator(text[i]))
                    i++;
            }
            const bool rooted = !out.Root.empty() && out.Root.back() == '/';
            while (i < text.size())
            {
                size_t end = i;
                while (end < text.size() && !IsSeparator(text[end]))
                    end++;
                const std::string_view part = text.substr(i, end - i);
                if (part == "..")
                {
                    if (!out.Parts.empty() && out.Parts.back() != "..")
                        out.Parts.pop_back();
                    else if (!rooted)
                        out.Parts.push_back(part); // at the root there is nothing above: dropped
                }
                else if (!part.empty() && part != ".")
                    out.Parts.push_back(part);
                i = end;
                while (i < text.size() && IsSeparator(text[i]))
                    i++;
            }
            return out;
        }

        void AppendParts(std::string& out, const std::vector<std::string_view>& parts, size_t from)
        {
            for (size_t i = from; i < parts.size(); i++)
            {
                if (!out.empty() && out.back() != '/')
                    out += '/';
                out += parts[i];
            }
        }

        std::string JoinPath(const PathParts& path)
        {
            std::string out = path.Root;
            AppendParts(out, path.Parts, 0);
            return out.empty() ? std::string(".") : out;
        }

        // Whether `root` (all its parts) starts `path`, and how many parts that is.
        bool StartsWithParts(const PathParts& path, const PathParts& root)
        {
            if (path.Root != root.Root || root.Parts.size() > path.Parts.size())
                return false;
            return std::equal(root.Parts.begin(), root.Parts.end(), path.Parts.begin());
        }

    }

    std::string FileSystem::NormalizePath(const std::filesystem::path& path)
    {
        const std::string text = path.generic_string();
        if (text.empty())
            return {};
        return JoinPath(ParsePath(text));
    }

    bool FileSystem::IsAbsolutePath(const std::filesystem::path& path)
    {
        const std::string text = path.generic_string();
        return ParsePath(text).IsAbsolute();
    }

    std::string FileSystem::RelativePath(const std::filesystem::path& path, const std::filesystem::path& base)
    {
        const std::string pathText = path.generic_string(), baseText = base.generic_string();
        if (pathText.empty() || baseText.empty())
            return {};
        const PathParts p = ParsePath(pathText), b = ParsePath(baseText);
        if (p.Root != b.Root)
            return {};
        size_t common = 0;
        while (common < p.Parts.size() && common < b.Parts.size() && p.Parts[common] == b.Parts[common])
            common++;
        // `base` still climbing out after the shared start: the name of the folder it ends in is
        // not in the text.
        for (size_t i = common; i < b.Parts.size(); i++)
            if (b.Parts[i] == "..")
                return {};
        std::string out;
        for (size_t i = common; i < b.Parts.size(); i++)
            out += out.empty() ? ".." : "/..";
        AppendParts(out, p.Parts, common);
        return out.empty() ? std::string(".") : out;
    }

    bool FileSystem::IsSubPath(const std::filesystem::path& path, const std::filesystem::path& root, bool allowSame)
    {
        const std::string pathText = path.generic_string(), rootText = root.generic_string();
        if (pathText.empty() || rootText.empty())
            return false;
        const PathParts p = ParsePath(pathText), r = ParsePath(rootText);
        if (!StartsWithParts(p, r))
            return false;
        if (p.Parts.size() == r.Parts.size())
            return allowSame;
        // After a root of only ".." parts the path may climb further ("../.." is not in "..").
        return p.Parts[r.Parts.size()] != "..";
    }

    std::string FileSystem::GetRelativeInside(const std::filesystem::path& path, const std::filesystem::path& root)
    {
        const std::string pathText = path.generic_string(), rootText = root.generic_string();
        if (pathText.empty() || rootText.empty())
            return {};
        const PathParts p = ParsePath(pathText), r = ParsePath(rootText);
        if (!StartsWithParts(p, r) || p.Parts.size() == r.Parts.size() || p.Parts[r.Parts.size()] == "..")
            return {};
        std::string out;
        AppendParts(out, p.Parts, r.Parts.size());
        return out;
    }

    bool FileSystem::IsSafeRelativePath(const std::filesystem::path& relative)
    {
        const std::string text = relative.generic_string();
        if (text.empty())
            return false;
        const PathParts p = ParsePath(text);
        return !p.HasNul && !p.IsAbsolute() && !p.ClimbsOut();
    }

    std::filesystem::path FileSystem::SafeJoin(const std::filesystem::path& root, const std::filesystem::path& relative)
    {
        if (!IsSafeRelativePath(relative))
            return {};
        const std::string normalized = NormalizePath(relative);
        if (normalized == ".")
            return root;
        return root / normalized;
    }

    std::string FileSystem::SanitizeRelativePath(const std::filesystem::path& path)
    {
        const std::string text = path.generic_string();
        PathParts p = ParsePath(text);
        std::string out;
        size_t from = 0;
        while (from < p.Parts.size() && p.Parts[from] == "..")
            from++;
        for (size_t i = from; i < p.Parts.size(); i++)
            if (p.Parts[i].find('\0') == std::string_view::npos)
            {
                if (!out.empty())
                    out += '/';
                out += p.Parts[i];
            }
        return out;
    }

    // ------------------------------------------------------------------ paths (on disk)
    std::filesystem::path FileSystem::AbsolutePath(const std::filesystem::path& path)
    {
        if (path.empty())
            return {};
        std::error_code ec;
        std::filesystem::path absolute = std::filesystem::absolute(path, ec);
        return std::filesystem::path(NormalizePath(ec ? path : absolute));
    }

    std::filesystem::path FileSystem::CanonicalPath(const std::filesystem::path& path)
    {
        if (path.empty())
            return {};
        std::error_code ec;
        std::filesystem::path absolute = std::filesystem::absolute(path, ec);
        if (ec)
            absolute = path;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, ec);
        return std::filesystem::path(NormalizePath(ec ? absolute : canonical));
    }

    bool FileSystem::IsSubPathOnDisk(const std::filesystem::path& path, const std::filesystem::path& root, bool allowSame)
    {
        if (path.empty() || root.empty())
            return false;
        return IsSubPath(CanonicalPath(path), CanonicalPath(root), allowSame);
    }

    // ------------------------------------------------------------------ extensions
    std::string FileSystem::GetExtension(const std::filesystem::path& path)
    {
        return StringUtils::ToLower(path.extension().string());
    }

    bool FileSystem::HasExtension(const std::filesystem::path& path, std::string_view extension)
    {
        if (extension.empty())
            return !path.has_extension();
        const std::string name = path.filename().string();
        const size_t dotted = extension.size() + (extension.front() == '.' ? 0 : 1);
        if (name.size() <= dotted)
            return false; // ".png" is a hidden file's name, not an extension
        const std::string_view tail = std::string_view(name).substr(name.size() - dotted);
        if (extension.front() != '.' && tail.front() != '.')
            return false;
        return StringUtils::EqualsIgnoreCase(extension.front() == '.' ? tail : tail.substr(1), extension);
    }

    bool FileSystem::HasExtension(const std::filesystem::path& path, std::initializer_list<std::string_view> extensions)
    {
        for (std::string_view extension : extensions)
            if (HasExtension(path, extension))
                return true;
        return false;
    }

    std::filesystem::path FileSystem::ReplaceExtension(const std::filesystem::path& path, std::string_view extension)
    {
        std::filesystem::path out = path;
        if (extension.empty())
            out.replace_extension();
        else if (extension.front() == '.')
            out.replace_extension(std::filesystem::path(extension));
        else
            out.replace_extension(std::filesystem::path("." + std::string(extension)));
        return out;
    }

    // ------------------------------------------------------------------ standard folders
    namespace {

        // An environment variable naming an absolute folder (XDG: relative ones are ignored).
        std::filesystem::path AbsoluteFromEnvironment(const char* name)
        {
            const char* value = std::getenv(name);
            if (!value || !*value)
                return {};
            std::filesystem::path path(value);
            return path.is_absolute() ? path : std::filesystem::path();
        }

    }

    std::filesystem::path FileSystem::GetTempDirectory()
    {
        std::error_code ec;
        std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
        if (ec || temp.empty())
            return "/tmp";
        return temp;
    }

    std::filesystem::path FileSystem::CreateTempDirectory(std::string_view prefix)
    {
        // The prefix is one name: separators (and NUL) cannot lead out of the temporary folder.
        std::string name = std::string(prefix.empty() ? std::string_view("zamin") : prefix);
        for (char& c : name)
            if (IsSeparator(c) || c == '\0')
                c = '_';
        const std::filesystem::path temp = GetTempDirectory();
#if defined(__linux__) || defined(__APPLE__)
        std::string pattern = (temp / (name + "-XXXXXX")).string();
        if (char* made = mkdtemp(pattern.data()))
            return std::filesystem::path(made);
        ZM_CORE_ERROR("Vaqtinchalik papka yaratilmadi: '{}' ({})", pattern, std::strerror(errno));
        return {};
#else
        // No mkdtemp: random names until one is new (create_directory is atomic: false when it exists).
        std::random_device device;
        std::mt19937_64 rng(((uint64_t)device() << 32) ^ device());
        for (int attempt = 0; attempt < 64; attempt++)
        {
            const std::filesystem::path candidate = temp / std::format("{}-{:012x}", name, rng() & 0xFFFFFFFFFFFFull);
            std::error_code ec;
            if (std::filesystem::create_directory(candidate, ec))
                return candidate;
            if (ec)
                break;
        }
        ZM_CORE_ERROR("Vaqtinchalik papka yaratilmadi: '{}'", (temp / name).string());
        return {};
#endif
    }

    std::filesystem::path FileSystem::GetHomeDirectory()
    {
#ifdef _WIN32
        if (const char* profile = std::getenv("USERPROFILE"); profile && *profile)
            return profile;
#endif
        if (const char* home = std::getenv("HOME"); home && *home)
            return home;
#if defined(__linux__) || defined(__APPLE__)
        if (const passwd* user = getpwuid(getuid()); user && user->pw_dir && *user->pw_dir)
            return user->pw_dir;
#endif
        return {};
    }

    std::filesystem::path FileSystem::GetUserDataDirectory()
    {
#ifdef _WIN32
        return AbsoluteFromEnvironment("APPDATA");
#else
        if (std::filesystem::path xdg = AbsoluteFromEnvironment("XDG_DATA_HOME"); !xdg.empty())
            return xdg;
        const std::filesystem::path home = GetHomeDirectory();
        return home.empty() ? home : home / ".local" / "share";
#endif
    }

    std::filesystem::path FileSystem::GetUserConfigDirectory()
    {
#ifdef _WIN32
        return AbsoluteFromEnvironment("APPDATA");
#else
        if (std::filesystem::path xdg = AbsoluteFromEnvironment("XDG_CONFIG_HOME"); !xdg.empty())
            return xdg;
        const std::filesystem::path home = GetHomeDirectory();
        return home.empty() ? home : home / ".config";
#endif
    }

    std::filesystem::path FileSystem::GetUserCacheDirectory()
    {
#ifdef _WIN32
        return AbsoluteFromEnvironment("LOCALAPPDATA");
#else
        if (std::filesystem::path xdg = AbsoluteFromEnvironment("XDG_CACHE_HOME"); !xdg.empty())
            return xdg;
        const std::filesystem::path home = GetHomeDirectory();
        return home.empty() ? home : home / ".cache";
#endif
    }

    std::string FileSystem::ShortenHomePath(const std::filesystem::path& path)
    {
        const std::filesystem::path home = GetHomeDirectory();
        if (!home.empty() && IsAbsolutePath(path) && IsSubPath(path, home))
        {
            const std::string inside = GetRelativeInside(path, home);
            return inside.empty() ? std::string("~") : "~/" + inside;
        }
        return path.string();
    }

    // ------------------------------------------------------------------ TempDirectory
    TempDirectory::TempDirectory(std::string_view prefix)
        : m_Path(FileSystem::CreateTempDirectory(prefix))
    {
    }

    TempDirectory::~TempDirectory()
    {
        Remove();
    }

    TempDirectory::TempDirectory(TempDirectory&& other) noexcept
        : m_Path(std::move(other.m_Path))
    {
        other.m_Path.clear();
    }

    TempDirectory& TempDirectory::operator=(TempDirectory&& other) noexcept
    {
        if (this != &other)
        {
            Remove();
            m_Path = std::move(other.m_Path);
            other.m_Path.clear();
        }
        return *this;
    }

    std::filesystem::path TempDirectory::Release()
    {
        std::filesystem::path path = std::move(m_Path);
        m_Path.clear();
        return path;
    }

    void TempDirectory::Remove()
    {
        if (m_Path.empty())
            return;
        std::error_code ec;
        std::filesystem::remove_all(m_Path, ec);
        if (ec)
            ZM_CORE_WARN("Vaqtinchalik papka o'chirilmadi: '{}' ({})", m_Path.string(), ec.message());
        m_Path.clear();
    }

}
