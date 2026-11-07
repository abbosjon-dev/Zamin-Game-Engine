#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace Zamin {

    // File helpers: reading and writing whole files, path arithmetic and the standard folders.
    //
    // Paths: NormalizePath, RelativePath, IsSubPath, GetRelativeInside, IsSafeRelativePath,
    // SafeJoin and SanitizeRelativePath are LEXICAL: they look only at the text, never at the disk
    // (no symbolic links are followed, the current folder is not read). Both '/' and '\' separate
    // parts (paths written on Windows work as well), the results always use '/'. An absolute path
    // starts with '/' (or a drive, "C:/"); everything else is relative. CanonicalPath and
    // IsSubPathOnDisk do resolve symbolic links (on disk).
    //
    //   FileSystem::NormalizePath("Assets\\Textures/./../Sprites//hero.png")   // "Assets/Sprites/hero.png"
    //   FileSystem::GetRelativeInside("/game/Assets/a/b.png", "/game/Assets")  // "a/b.png" ("" outside)
    //   fs::path file = FileSystem::SafeJoin(saveFolder, slotName);            // empty: it would leave the folder
    class FileSystem
    {
    public:
        // ------------------------------------------------------------------ files
        static std::string ReadTextFile(const std::filesystem::path& path);
        static bool WriteTextFile(const std::filesystem::path& path, const std::string& content);
        static std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& path);
        // Writes the bytes (the parent folders are made). False when the file cannot be written.
        static bool WriteBinaryFile(const std::filesystem::path& path, const void* data, size_t size);
        static std::filesystem::path GetExecutablePath();
        static std::filesystem::path GetExecutableDirectory() { return GetExecutablePath().parent_path(); }

        // ------------------------------------------------------------------ paths (lexical)
        // The path with '/' separators, "." parts and repeated / trailing separators dropped and
        // each "name/.." pair removed. A relative path keeps its leading ".." ("../a"); at the root
        // of an absolute one they are dropped ("/../a" -> "/a"). Empty stays empty; a path that
        // cancels out ("a/..") is ".".
        static std::string NormalizePath(const std::filesystem::path& path);
        // True for "/x", "\x" and drive paths ("C:/x", "C:\x", "C:"): the text alone decides (no
        // current folder, the same answer on every system).
        static bool IsAbsolutePath(const std::filesystem::path& path);
        // `path` relative to `base` (both normalized first): "../.." where it climbs out, "." when
        // they are the same. Empty when there is no such path: one absolute and the other relative,
        // different drives, `base` climbing above where `path` starts ("..", "a" -> ?), an empty one.
        //   RelativePath("Assets/Scenes/A.zscene", "Assets/Textures") == "../Scenes/A.zscene"
        static std::string RelativePath(const std::filesystem::path& path, const std::filesystem::path& base);
        // Whether `path` lies inside `root` (a whole-part prefix after normalizing: "Assets2/x" is not
        // inside "Assets"; "Assets/../x" is not either). `allowSame`: the root itself counts as inside.
        static bool IsSubPath(const std::filesystem::path& path, const std::filesystem::path& root, bool allowSame = true);
        // `path` relative to `root` when it lies inside it; empty when it does not, or is the root.
        static std::string GetRelativeInside(const std::filesystem::path& path, const std::filesystem::path& root);
        // A relative path that stays inside whatever folder it is joined to: not absolute, no drive,
        // not climbing out with ".." (after normalizing: "a/../b" is fine, "a/../../b" is not), no NUL
        // byte. "." (the folder itself) is safe, "" is not.
        static bool IsSafeRelativePath(const std::filesystem::path& relative);
        // `root` / `relative` when `relative` is safe (IsSafeRelativePath; joined normalized, "." gives
        // `root`), else an EMPTY path. For names that come from players, save files, mods or the
        // network. Lexical: a symbolic link inside `root` may still point elsewhere.
        static std::filesystem::path SafeJoin(const std::filesystem::path& root, const std::filesystem::path& relative);
        // The path made relative and safe by dropping what would climb out: the root or drive and the
        // leading ".." ("/etc/x" -> "etc/x", "../../a" -> "a", "C:\a" -> "a"); "" when nothing is left.
        static std::string SanitizeRelativePath(const std::filesystem::path& path);

        // ------------------------------------------------------------------ paths (on disk)
        // Absolute (against the current folder) and normalized; symbolic links are kept.
        static std::filesystem::path AbsolutePath(const std::filesystem::path& path);
        // Absolute with symbolic links resolved as far as the path exists (weakly_canonical); the
        // rest normalized. Never throws: on an error the absolute path.
        static std::filesystem::path CanonicalPath(const std::filesystem::path& path);
        // IsSubPath of the canonical paths: also true when a symbolic link leads inside.
        static bool IsSubPathOnDisk(const std::filesystem::path& path, const std::filesystem::path& root, bool allowSame = true);

        // ------------------------------------------------------------------ extensions
        // The extension in lower case with its dot (".png"); "" without one (".gitignore" has none).
        static std::string GetExtension(const std::filesystem::path& path);
        // Whether the file name ends with `extension`, ignoring case; the dot is optional and it may
        // have several parts ("a.tar.gz" has "gz", ".gz" and ".tar.gz"). A name that is only the
        // extension (".png") does not have it. "": true when there is no extension.
        static bool HasExtension(const std::filesystem::path& path, std::string_view extension);
        static bool HasExtension(const std::filesystem::path& path, std::initializer_list<std::string_view> extensions);
        // The path with its last extension replaced (the dot is optional; "" removes it).
        static std::filesystem::path ReplaceExtension(const std::filesystem::path& path, std::string_view extension);
        // The file name without its last extension ("hero.png" -> "hero").
        static std::string GetStem(const std::filesystem::path& path) { return path.stem().string(); }

        // ------------------------------------------------------------------ standard folders
        // The system's temporary folder ($TMPDIR, else /tmp).
        static std::filesystem::path GetTempDirectory();
        // A new, empty folder of its own in the temporary folder: <temp>/<prefix>-XXXXXX (made
        // atomically, only this user may enter it; separators in the prefix become '_'). Empty when
        // it cannot be made (logged).
        static std::filesystem::path CreateTempDirectory(std::string_view prefix = "zamin");
        // $HOME (else the user database); empty when unknown.
        static std::filesystem::path GetHomeDirectory();
        // Per-user folders of the XDG base directory spec: $XDG_DATA_HOME / $XDG_CONFIG_HOME /
        // $XDG_CACHE_HOME when set to an absolute path, else ~/.local/share, ~/.config, ~/.cache
        // (on Windows %APPDATA%, %APPDATA%, %LOCALAPPDATA%). Empty when there is no home. Not created.
        static std::filesystem::path GetUserDataDirectory();
        static std::filesystem::path GetUserConfigDirectory();
        static std::filesystem::path GetUserCacheDirectory();
        // A path for showing: under the home folder as "~/..." (the rest as it is).
        static std::string ShortenHomePath(const std::filesystem::path& path);
    };

    // A temporary folder that is deleted (with everything in it) when this object goes out of
    // scope, unless Release() was called. Tests, exports, unpacking.
    //
    //   TempDirectory temp("zamin-export");
    //   FileSystem::WriteTextFile(temp.GetPath() / "a.txt", "...");
    class TempDirectory
    {
    public:
        explicit TempDirectory(std::string_view prefix = "zamin");
        ~TempDirectory();
        TempDirectory(const TempDirectory&) = delete;
        TempDirectory& operator=(const TempDirectory&) = delete;
        TempDirectory(TempDirectory&& other) noexcept;
        TempDirectory& operator=(TempDirectory&& other) noexcept;

        // Empty when the folder could not be made.
        const std::filesystem::path& GetPath() const { return m_Path; }
        bool IsValid() const { return !m_Path.empty(); }
        explicit operator bool() const { return IsValid(); }
        // Keeps the folder (it is not deleted); its path is returned.
        std::filesystem::path Release();
        // Deletes the folder now (also done by the destructor).
        void Remove();

    private:
        std::filesystem::path m_Path;
    };

}
