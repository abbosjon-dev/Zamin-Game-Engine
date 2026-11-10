#include "EditorLayer.h"
#include "UI.h"
#include <Zamin/Core/Process.h>
#include <Zamin/Scripting/ModuleABI.h>
#include <Zamin/Utils/FileSystem.h>
#include <Zamin/Utils/PlatformUtils.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <fstream>

namespace Zamin {
    namespace fs = std::filesystem;
    void EditorLayer::BuildScripts(bool reloadAfter)
    {
        if (m_Building)
        {
            ZM_WARN("Kompilyatsiya allaqachon ketmoqda...");
            return;
        }
        auto project = Project::GetActive();
        if (!project)
        {
            ZM_WARN("Avval loyiha oching");
            return;
        }
        if(!m_CodeEditorPanel.SaveAll()) {
            m_ShowCodeEditor=true;
            ZM_ERROR("Build canceled: save or resolve modified code files first");
            return;
        }
        if(project->GetConfig().ScriptModulePath.empty()) {
            project->GetConfig().ScriptModulePath="Binaries/lib"+project->GetConfig().Name+".so";
            if(!Project::SaveActive(Project::GetProjectFilePath())) {
                project->GetConfig().ScriptModulePath.clear();
                ZM_ERROR("Build canceled: native script module settings could not be saved");
                return;
            }
        }
        fs::path projectDir = Project::GetProjectDirectory();
        if (!fs::exists(projectDir / "CMakeLists.txt"))
            Project::WriteBuildFiles(projectDir, project->GetConfig().Name);

        if (m_BuildThread.joinable()) m_BuildThread.join();

        fs::path buildDir = projectDir / "Build";
        fs::path engineCMakeDir = FileSystem::GetExecutableDirectory().parent_path();
        const bool ninja = RunProcess({ "ninja", "--version" }).Succeeded();
        const bool needsConfigure = !fs::exists(buildDir / "CMakeCache.txt");
        std::vector<std::string> configure{ "cmake", "-S", projectDir.string(), "-B", buildDir.string(),
            "-DCMAKE_BUILD_TYPE=Release", "-DZamin_DIR=" + engineCMakeDir.string() };
        if (ninja) { configure.push_back("-G"); configure.push_back("Ninja"); }
        const std::vector<std::string> build{ "cmake", "--build", buildDir.string(), "--parallel" };

        m_Building = true;
        m_ReloadAfterBuild = reloadAfter;
        m_BuildStartTime = Platform::GetTime();
        BeginBuildOutput(buildDir);
        ZM_INFO("C++ skriptlar kompilyatsiya qilinmoqda: {}", project->GetConfig().Name);

        m_BuildThread = std::thread([this, configure, build, needsConfigure]() {
            auto onLine = [this](const std::string& line) {
                AddBuildOutputLine(line);
                if (line.empty()) return;
                if (line.find("error") != std::string::npos || line.find("Error") != std::string::npos) ZM_ERROR("{}", line);
                else if (line.find("warning") != std::string::npos) ZM_WARN("{}", line);
                else ZM_TRACE("{}", line);
            };
            bool ok = false;
            try
            {
                ProcessResult result;
                if (needsConfigure) result = RunProcess(configure, onLine);
                else result.ExitCode = 0;
                if (result.Succeeded()) result = RunProcess(build, onLine);
                if (!result.Error.empty()) onLine("Build error: " + result.Error);
                ok = result.Succeeded();
            }
            catch (const std::exception& error) { ZM_ERROR("Build error: {}", error.what()); }
            m_BuildSucceeded = ok;
            m_Building = false;
            m_BuildFinished = true;
        });
    }

    void EditorLayer::ReloadScriptModule()
    {
        fs::path module = Project::GetScriptModulePath();
        if (module.empty())
            return;
        const bool loaded = ScriptEngine::LoadModule(module);
        if (!loaded) return; // preserve the running scene and inspector after a rejected candidate
        if (m_SceneState != SceneState::Edit) OnSceneStop();
        if (loaded)
            ZM_INFO("Skriptlar qayta yuklandi (hot reload) " ICON_FA_CHECK);
        m_SceneHierarchyPanel.OnScriptsReloaded();
        // ZAMIN_EDITOR_HOT_RELOAD_TEST=<frames> (Tests/hot_reload_test.sh): the game plays again after every load
        // of the module (the first build, then each hot reload); <frames> frames after the first hot reload the
        // editor closes.
        if (const char* test = std::getenv("ZAMIN_EDITOR_HOT_RELOAD_TEST"); test && loaded)
        {
            m_HotReloadTestPlay = true;
            if (++m_HotReloadTestLoads >= 2)
                m_HotReloadTestCloseIn = std::max(1, std::atoi(test));
        }
    }

    // Keep the upstream linker-completion check alongside the ABI preflight: a stable
    // file size alone does not mean that the linker has written the section table yet.
    static bool IsElfFileComplete(const fs::path& file, uintmax_t size)
    {
        unsigned char header[64]{};
        std::ifstream in(file, std::ios::binary);
        if (!in.read(reinterpret_cast<char*>(header), sizeof(header))) return false;
        if (header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' || header[3] != 'F' || header[4] != 2 || header[5] != 1) return true;
        const auto read = [&](size_t at, size_t bytes) {
            uint64_t value = 0;
            for (size_t i=0; i<bytes; ++i) value |= uint64_t(header[at+i]) << (8*i);
            return value;
        };
        const uint64_t table = read(0x28,8), entrySize = read(0x3a,2), count = read(0x3c,2);
        return table <= size && entrySize * count <= size - table;
    }

    void EditorLayer::CheckModuleHotReload(Timestep ts)
    {
        m_HotReloadTimer += ts;
        if (m_HotReloadTimer < 1.0f || !m_AutoReload || m_Building)
            return;
        m_HotReloadTimer = 0.0f;
        static fs::path rejectedPath;
        static fs::file_time_type rejectedTime{};
        static uintmax_t rejectedSize=0;
        fs::path module = Project::GetScriptModulePath();
        std::error_code ec;
        if (module.empty() || !fs::exists(module, ec))
            return;
        auto time = fs::last_write_time(module, ec);
        if (!ec && (!ScriptEngine::IsModuleLoaded() || time != ScriptEngine::GetLoadedModuleWriteTime()))
        {
            // Built outside the editor (an IDE, cmake --build): the linker writes the file in place, so a change
            // is loaded only once the file stayed the same (time and size) for one more poll - a module read
            // half written would not load. Failed candidates now keep the old module alive.
            const uintmax_t size = fs::file_size(module, ec);
            if (ec || time != m_ChangedModuleTime || size != m_ChangedModuleSize || !IsElfFileComplete(module, size))
            {
                m_ChangedModuleTime = time;
                m_ChangedModuleSize = ec ? 0 : size;
                m_HotReloadTimer = 0.6f; // looked at again in 0.4 s
                return;
            }
            if(rejectedPath==module&&rejectedTime==time&&rejectedSize==size)return;
            ZM_INFO("Skript moduli o'zgardi - qayta yuklanmoqda");
            ReloadScriptModule();
            if(!ScriptEngine::IsModuleLoaded()||ScriptEngine::GetModulePath()!=module||ScriptEngine::GetLoadedModuleWriteTime()!=time)
            {rejectedPath=module;rejectedTime=time;rejectedSize=size;}
            else rejectedPath.clear();
        }
    }

    bool EditorLayer::CreateScriptClass(const std::string& className, ScriptTemplateKind kind)
    {
        if (!Project::GetActive()) return false;
        if (std::string problem = ScriptTemplate::ValidateClassName(className); !problem.empty())
        {
            ZM_ERROR("Skript yaratilmadi: {}", problem);
            return false;
        }
        if (ScriptRegistry::Find(className))
        {
            ZM_WARN("'{}' klassi allaqachon mavjud (skript modulida ro'yxatdan o'tgan)", className);
            return false;
        }
        fs::path file = Project::GetSourceDirectory() / (className + ".cpp");
        if (fs::exists(file))
        {
            ZM_WARN("Fayl allaqachon mavjud: {}", file.string());
            return false;
        }
        if (!FileSystem::WriteTextFile(file, ScriptTemplate::Generate(className, kind)))
        {
            ZM_ERROR("Fayl yozilmadi: {}", file.string());
            return false;
        }
        OpenInCodeEditor(file);
        ZM_INFO("Yangi skript yaratildi ({} shablon): {}", ScriptTemplate::GetKindName(kind), file.string());
        BuildScripts(true);
        return true;
    }

    // ================================================================== export
    void EditorLayer::ReportExcludedAssets(const ExportAssetPlan& plan, const std::string& gameName, const fs::path& outputDirectory)
    {
        const fs::path report = outputDirectory / ExportAssets::ReportFileName;
        std::error_code ec;
        if (plan.Excluded.empty())
        {
            fs::remove(report, ec);
            ZM_INFO("Eksport: {} ta asset fayli ({}), hammasi ishlatiladi", plan.Included.size(), ExportAssets::FormatSize(plan.IncludedBytes));
            return;
        }
        ZM_INFO("Eksport: {} ta asset fayli ({}); {} tasi chiqarib tashlandi ({}) - ro'yxat: {}", plan.Included.size(),
                ExportAssets::FormatSize(plan.IncludedBytes), plan.Excluded.size(), ExportAssets::FormatSize(plan.ExcludedBytes), report.string());
        const size_t shown = std::min<size_t>(plan.Excluded.size(), 12);
        for (size_t i = 0; i < shown; i++)
            ZM_INFO("    - {} ({}, {})", plan.Excluded[i].Path, ExportAssets::FormatSize(plan.Excluded[i].Size), plan.Excluded[i].Reason);
        if (plan.Excluded.size() > shown)
            ZM_INFO("    ... va yana {} ta", plan.Excluded.size() - shown);
        FileSystem::WriteTextFile(report, plan.FormatReport(gameName));
    }

    void EditorLayer::BuildGame(const fs::path& outputDirectory)
    {
        auto project = Project::GetActive();
        if (!project || m_Exporting) return;
        if (m_SceneState != SceneState::Edit) OnSceneStop();
        if (m_SceneDirty) SaveScene();
        if (m_SceneDirty)
        {
            ZM_ERROR("O'yin eksport qilinmadi: sahna saqlanmadi");
            return;
        }
        m_ProjectSettingsPanel.SaveIfDirty(); // the exported game reads the .zproject (input map)

        const auto started = std::chrono::steady_clock::now();
        std::string name = project->GetConfig().Name;
        const PlayerSettings player = project->GetConfig().Player;
        const std::string productName = player.GetProductName(name);
        // The report the export ends with (the window, export-report.json / .txt): what went wrong is
        // collected here and on the export thread.
        auto report = std::make_shared<ExportReport>();
        {
            std::error_code ec;
            report->Game = productName;
            report->Version = player.Version;
            report->Company = player.CompanyName;
            report->OutputDirectory = fs::absolute(outputDirectory, ec).lexically_normal().string();
            const std::time_t now = std::time(nullptr);
            std::tm tm{};
            localtime_r(&now, &tm);
            char date[32];
            std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &tm);
            report->Date = date;
        }
        // (Only the export thread uses these from here on: the main thread reads the report once it is handed over.)
        auto warn = [report](std::string message) {
            ZM_WARN("Eksport: {}", message);
            report->Warnings.push_back(std::move(message));
        };
        auto fail = [report](std::string message) {
            ZM_ERROR("Eksport: {}", message);
            report->Errors.push_back(std::move(message));
        };
        // The start scene and the build list: what would break the game is an error, the rest warnings.
        for (const BuildSceneIssue& issue : Project::CheckBuildScenes(project->GetConfig(), Project::GetAssetDirectory()))
        {
            if (issue.Error)
                fail(issue.Message);
            else
                warn(issue.Message);
        }
        const fs::path iconFile = Project::GetIconPath();
        if (!iconFile.empty())
        {
            ImageData icon;
            std::string error;
            if (!GameIcon::Load(iconFile, icon, &error))
                warn(std::format("o'yin ikonkasi o'qilmadi ({}) - .desktop faylida ikonka bo'lmaydi", error));
        }
        fs::path projectDir = Project::GetProjectDirectory();
        fs::path projectFile = Project::GetProjectFilePath();
        fs::path modulePath = Project::GetScriptModulePath();
        fs::path moduleRel = project->GetConfig().ScriptModulePath;
        fs::path assetRel = project->GetConfig().AssetDirectory;
        fs::path exeDir = FileSystem::GetExecutableDirectory();
        const ExportSettings exportSettings = project->GetConfig().Export;
        // The script module in Release (-O2) in a build folder of its own: needs the project's CMakeLists.txt
        // (written like BuildScripts does for a project without one) and its sources.
        std::error_code sourcesError;
        const bool hasSources = !modulePath.empty() && fs::is_directory(Project::GetSourceDirectory(), sourcesError);
        if (exportSettings.ReleaseScriptModule && hasSources && !fs::exists(projectDir / "CMakeLists.txt"))
            Project::WriteBuildFiles(projectDir, name);
        ExportModuleBuild moduleBuild;
        moduleBuild.ProjectDirectory = projectDir;
        moduleBuild.BuildDirectory = ExportModuleBuild::DefaultBuildDirectory(projectDir);
        moduleBuild.EngineDirectory = exeDir.parent_path();
        moduleBuild.OutputDirectory = moduleBuild.BuildDirectory / "bin";
        moduleBuild.Ninja = RunProcess({ "ninja", "--version" }).Succeeded();
        // Which assets go: with a build list only its enabled scenes, the start scene and their sub-scenes;
        // then what they lead to (prefabs, materials, models, clips ...), what the project file and the C++
        // code name, the folders always taken - the rest stays out (Project settings > Build scenes > Export).
        // Planned here, on this thread (the registry).
        std::vector<std::string> buildScenes, notes;
        AssetReferenceIndex referenceIndex;
        const ExportAssetPlan assetPlan = ExportAssets::PlanForActiveProject(&buildScenes, &notes, &referenceIndex);
        for (const std::string& note : notes)
            ZM_INFO("Eksport: {}", note);
        // The prefabs the game gets are checked (R1-R10, docs/Prefabs.md), and the scenes / prefabs that
        // hold the editor's prefab links are exported without them (instances older than their prefabs
        // brought up to date in the copy; the project's files stay as they are). Here, on this thread
        // (the editor's prefab database); the export thread only writes the texts.
        std::map<std::string, std::string> bakedFiles;
        {
            PrefabExportResult prefabCheck = PrefabExport::Prepare(assetPlan, m_PrefabDatabase, &referenceIndex);
            for (const PrefabExportIssue& issue : prefabCheck.Issues)
            {
                std::string message = issue.File.empty() ? std::format("prefab: {}", issue.Message)
                                                         : std::format("prefab ({}, {}): {}", issue.Rule, issue.File, issue.Message);
                if (issue.Level == PrefabExportIssue::Severity::Error ||
                    (issue.Level == PrefabExportIssue::Severity::Warning && exportSettings.PrefabWarningsAsErrors))
                    fail(std::move(message));
                else if (issue.Level == PrefabExportIssue::Severity::Warning)
                    warn(std::move(message));
                else
                    ZM_INFO("Eksport: {}", message);
            }
            ZM_INFO("Eksport: {}{}", prefabCheck.Summary(),
                    prefabCheck.Stripped ? std::format(" ({} ta faylning prefab bog'lanishlari olib tashlandi)", prefabCheck.Stripped) : std::string());
            bakedFiles = std::move(prefabCheck.Baked);
        }
        const bool binaryScenes = m_BuildGameBinaryScenes;
        // The exported game reads its asset registry from one file (made here, on this thread) - without the
        // assets left out.
        std::string registryText;
        if (AssetRegistry* registry = Project::GetAssetRegistry())
        {
            AssetRegistry::Snapshot snapshot = registry->GetSnapshot();
            std::erase_if(snapshot, [&](const AssetRegistry::SnapshotEntry& e) { return assetPlan.IsExcluded(e.Path); });
            registryText = AssetRegistry::SerializeSnapshot(snapshot);
        }

        if (m_ExportThread.joinable()) m_ExportThread.join();
        m_Exporting = true;
        m_ExportThread = std::thread([=, this]() {
            std::error_code ec;
            auto copyOpts = fs::copy_options::recursive | fs::copy_options::overwrite_existing;
            fs::create_directories(outputDirectory / "Game", ec);

            bool ok = true;
            auto copy = [&](const fs::path& from, const fs::path& to) {
                std::error_code e;
                if (!fs::exists(from)) { fail(std::format("topilmadi: {}", from.string())); ok = false; return; }
                fs::create_directories(to.parent_path(), e);
                fs::copy(from, to, copyOpts, e);
                if (e) { fail(std::format("nusxalashda xato {}: {}", from.string(), e.message())); ok = false; }
            };

            // The binaries: the player, the engine (and later the script module) - stripped below.
            std::vector<fs::path> binaries;
            copy(exeDir / "ZaminPlayer", outputDirectory / "ZaminPlayer");
            binaries.push_back(outputDirectory / "ZaminPlayer");
            for (auto& entry : fs::directory_iterator(exeDir, ec))
                if (entry.path().filename().string().starts_with("libZamin"))
                {
                    copy(entry.path(), outputDirectory / entry.path().filename());
                    binaries.push_back(outputDirectory / entry.path().filename());
                }
            copy(exeDir / "Resources", outputDirectory / "Resources");
            copy(projectFile, outputDirectory / "Game" / projectFile.filename());
            {
                // The assets of the plan, each with its .zmeta (a folder exported before loses what is left out now).
                const fs::path assets = outputDirectory / "Game" / assetRel;
                std::error_code e;
                if (!fs::exists(assets, e) || !fs::equivalent(assets, projectDir / assetRel, e))
                    fs::remove_all(assets, e);
                for (const std::string& file : assetPlan.Included)
                {
                    if (auto baked = bakedFiles.find(file); baked != bakedFiles.end())
                    {
                        // Without the editor's prefab links (and up to date with its prefabs).
                        fs::create_directories((assets / file).parent_path(), e);
                        if (!FileSystem::WriteTextFile(assets / file, baked->second))
                        {
                            fail(std::format("yozilmadi: {}", (assets / file).string()));
                            ok = false;
                        }
                    }
                    else
                        copy(projectDir / assetRel / file, assets / file);
                    const fs::path meta = projectDir / assetRel / (file + ".zmeta");
                    if (fs::exists(meta, e))
                        copy(meta, assets / (file + ".zmeta"));
                }
                if (!buildScenes.empty())
                    ZM_INFO("Eksport: build ro'yxatidagi {} ta sahna", buildScenes.size());
                ReportExcludedAssets(assetPlan, name, outputDirectory);
            }
            if (binaryScenes)
            {
                // Every scene of the game also as a binary cache next to it (smaller, already in the
                // current format): the player reads that one instead (SceneSerializer).
                const fs::path assets = outputDirectory / "Game" / assetRel;
                size_t written = 0, textBytes = 0, binaryBytes = 0;
                for (auto it = fs::recursive_directory_iterator(assets, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                {
                    if (it->path().extension() != ".zscene")
                        continue;
                    std::string error;
                    std::error_code e;
                    if (SceneSerializer::WriteBinaryCache(it->path(), &error))
                    {
                        written++;
                        textBytes += (size_t)fs::file_size(it->path(), e);
                        binaryBytes += (size_t)fs::file_size(SceneSerializer::GetBinaryCachePath(it->path()), e);
                    }
                    else
                        warn(std::format("sahnaning binar keshi yozilmadi ({}): {}", error, it->path().filename().string()));
                }
                if (written)
                    ZM_INFO("Eksport: {} ta sahna binar keshga yozildi ({:.1f} KB -> {:.1f} KB)", written, (double)textBytes / 1024.0,
                            (double)binaryBytes / 1024.0);
            }
            if (!registryText.empty() && !FileSystem::WriteTextFile(outputDirectory / "Game" / Project::BuildAssetRegistryFile, registryText))
            {
                fail("asset registri yozilmadi");
                ok = false;
            }

            // The script module: compiled again in Release (-O2, its own build folder) - the editor's module
            // when that is off or fails. (None at all: the game is exported without scripts - a warning.)
            bool moduleExported = false;
            if (!modulePath.empty())
            {
                fs::path module = modulePath;
                std::string how = "editor moduli nusxalandi";
                if (exportSettings.ReleaseScriptModule && hasSources)
                {
                    ZM_INFO("Eksport: skript moduli Release'da kompilyatsiya qilinmoqda ({}): {}", moduleBuild.OptimizationFlags,
                            moduleBuild.BuildDirectory.string());
                    const auto buildStart = std::chrono::steady_clock::now();
                    std::string firstError;
                    auto onLine = [&](const std::string& line) {
                        if (line.empty())
                            return;
                        if (firstError.empty() && (line.find("error") != std::string::npos || line.find("Error") != std::string::npos))
                            firstError = line;
                        ZM_TRACE("{}", line);
                    };
                    int code = RunShellCommand(moduleBuild.MakeConfigureCommand(), onLine);
                    if (code == 0)
                        code = RunShellCommand(moduleBuild.MakeBuildCommand(), onLine);
                    // lib<Name>.so: the project's module name, else the only module the build made.
                    fs::path built = moduleBuild.OutputDirectory / modulePath.filename();
                    std::error_code e;
                    if (code == 0 && !fs::exists(built, e))
                        for (const auto& entry : fs::directory_iterator(moduleBuild.OutputDirectory, e))
                            if (entry.path().extension() == ".so")
                                built = entry.path();
                    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - buildStart).count();
                    if (code == 0 && fs::exists(built, e))
                    {
                        module = built;
                        how = std::format("Release ({}), {:.1f} s da kompilyatsiya qilindi", moduleBuild.OptimizationFlags, seconds);
                        ZM_INFO("Eksport: skript moduli {} ({})", how, ExportAssets::FormatSize(fs::file_size(built, e)));
                    }
                    else
                    {
                        how = "editor moduli nusxalandi (Release kompilyatsiya bo'lmadi)";
                        warn(std::format("skript moduli Release'da kompilyatsiya qilinmadi{} - editor moduli nusxalandi",
                                         firstError.empty() ? std::format(" (chiqish kodi {})", code) : ": " + firstError));
                    }
                }
                if (fs::exists(module))
                {
                    const auto destination=outputDirectory/"Game"/moduleRel;
                    copy(module,destination);
                    const auto abi=ModuleABI::Inspect(destination);
                    if(!abi.Compatible())
                    {
                        fail("skript moduli SDK/ABI tekshiruvidan o'tmadi: "+abi.Message);
                        report->ScriptModule="SDK/ABI mos emas";ok=false;
                    }
                    else
                    {
                        binaries.push_back(destination);report->ScriptModule=how;moduleExported=true;
                    }
                }
                else
                {
                    warn("skript moduli topilmadi - o'yin skriptlarsiz eksport qilinadi");
                    report->ScriptModule = "yo'q (skriptsiz)";
                }
            }
            else
                report->ScriptModule = "yo'q (loyihada skript moduli yo'q)";

            // The binaries without their debug information and local symbols (much smaller).
            if (exportSettings.StripBinaries)
            {
                if (!BinaryStrip::IsAvailable())
                    warn("strip dasturi topilmadi (binutils) - binarlar kichraytirilmadi");
                else
                {
                    uintmax_t before = 0, after = 0;
                    size_t stripped = 0;
                    for (const fs::path& binary : binaries)
                    {
                        std::error_code e;
                        if (!fs::is_regular_file(binary, e) || !BinaryStrip::IsElfFile(binary))
                            continue;
                        const StripResult result = BinaryStrip::Strip(binary);
                        if (!result.Ok)
                        {
                            warn(std::format("strip: {} - {}", binary.filename().string(), result.Error));
                            continue;
                        }
                        before += result.SizeBefore;
                        after += result.SizeAfter;
                        stripped++;
                    }
                    report->StrippedBytes = before > after ? before - after : 0;
                    if (stripped)
                        ZM_INFO("Eksport: strip - {} ta binar {} -> {}", stripped, ExportAssets::FormatSize(before), ExportAssets::FormatSize(after));
                }
            }

            // (It also points the desktop entry next to it at the icon in the folder it runs from: below.)
            fs::path launcher = outputDirectory / (name + ".sh");
            FileSystem::WriteTextFile(launcher, ExportFiles::MakeLauncherScript("Game/" + projectFile.filename().string(), name + ".desktop",
                                                                                iconFile.empty() ? std::string() : name + ".png"));
            fs::permissions(launcher, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                          fs::perms::others_read | fs::perms::others_exec, ec);

            // The game's icon next to the launcher (the PNG as it is, and icons/hicolor/<size>/apps/<id>.png in
            // every size up to its own) and a desktop entry that works wherever the folder goes (its Exec runs
            // the launcher next to the entry itself; the icon path is where the game was exported to - the
            // launcher points it at the new place when the game starts from there).
            // install-desktop-entry.sh puts the game into the application menus for wherever the folder is
            // (absolute paths, the icons into the user's icon theme).
            const std::string entryId = ExportFiles::MakeDesktopEntryId(name);
            const fs::path absoluteOut = fs::absolute(outputDirectory, ec);
            fs::path exportedIcon;
            fs::remove_all(outputDirectory / "icons", ec); // the sizes of an earlier export
            if (!iconFile.empty())
            {
                ImageData icon;
                if (GameIcon::Load(iconFile, icon))
                {
                    exportedIcon = absoluteOut / (name + ".png");
                    copy(iconFile, exportedIcon);
                    std::string iconError;
                    const std::vector<uint32_t> sizes = ExportFiles::WriteIconSet(icon, outputDirectory, entryId, &iconError);
                    if (sizes.empty())
                        warn(std::format("ikonka o'lchamlari yozilmadi: {}", iconError));
                    else
                        ZM_INFO("Eksport: ikonka {} o'lchamda ({} ... {} px, icons/hicolor)", sizes.size(), sizes.front(), sizes.back());
                }
            }
            if (!FileSystem::WriteTextFile(outputDirectory / (name + ".desktop"),
                                           ExportFiles::MakeRelocatableDesktopEntry(productName, player, launcher.filename().string(), exportedIcon)))
            {
                fail(std::format("{}.desktop yozilmadi", name));
                ok = false;
            }
            else
                fs::permissions(outputDirectory / (name + ".desktop"), fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                                fs::perm_options::add, ec);
            const fs::path installScript = outputDirectory / ExportFiles::DesktopInstallScriptName;
            if (!FileSystem::WriteTextFile(installScript,
                                           ExportFiles::MakeDesktopInstallScript(productName, player, launcher.filename().string(),
                                                                                 exportedIcon.empty() ? std::string() : exportedIcon.filename().string(),
                                                                                 entryId)))
            {
                fail(std::format("{} yozilmadi", ExportFiles::DesktopInstallScriptName));
                ok = false;
            }
            else
                fs::permissions(installScript, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                                                   fs::perms::others_exec, ec);

            // The folder checks itself: everything the exported game needs to start is there (the launcher, the
            // player, the engine, the project, its start / build scenes, the registry, the script module - when
            // one was exported: a game exported without scripts was a warning above).
            for (const std::string& problem : ExportFiles::CheckExportFolder(outputDirectory, name, moduleExported))
            {
                fail(std::format("eksport to'liq emas - {}", problem));
                ok = false;
            }

            // The report: what the folder holds now, what was left out, how long it took and what went wrong.
            report->SetExcluded(assetPlan);
            report->ScanDirectory(outputDirectory);
            report->Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            report->Succeeded = ok && report->Errors.empty();
            if (!report->Write(outputDirectory))
                ZM_WARN("Eksport hisoboti yozilmadi: {}", (outputDirectory / ExportReport::JsonFileName).string());
            ZM_INFO("Eksport hisoboti: {} ta fayl, {} ({} ogohlantirish, {} xato) - {}", report->FileCount, ExportAssets::FormatSize(report->TotalBytes),
                    report->Warnings.size(), report->Errors.size(), (outputDirectory / ExportReport::TextFileName).string());

            if (report->Succeeded) ZM_INFO("O'yin eksport qilindi " ICON_FA_ROCKET " {}  (ishga tushirish: {})", outputDirectory.string(), launcher.string());
            else ZM_ERROR("O'yinni eksport qilishda xatolar bo'ldi");
            {
                std::lock_guard<std::mutex> lock(m_ExportReportMutex);
                m_PendingExportReport = std::make_unique<ExportReport>(*report);
            }
            m_Exporting = false;
        });
    }

}
