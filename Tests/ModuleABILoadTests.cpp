#include <Zamin/Zamin.h>
#include <Zamin/Scripting/ModuleABI.h>
#include <Zamin/Utils/FileSystem.h>
#include <Zamin/Core/Process.h>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
using namespace Zamin;
namespace fs = std::filesystem;
static void Check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
static float Value()
{
    auto* info = ScriptRegistry::Find("ABIProbe");
    Check(info && info->Factory && !info->Fields.empty(), "registered probe fields");
    std::unique_ptr<ScriptableEntity> instance(info->Factory());
    return *static_cast<float*>(info->Fields[0].Accessor(instance.get()));
}
int main(int argc, char** argv)
{
    if (argc != 5) return 2;
    auto work = fs::temp_directory_path() / ("zamin-abi-load-" + std::to_string(uint64_t(UUID())));
    fs::create_directories(work);
    auto trace = work / "constructors.log";
    const auto* previous = std::getenv("ZAMIN_ABI_CONSTRUCTOR_TRACE");
    std::string saved = previous ? previous : "";
    bool had = previous != nullptr;
    setenv("ZAMIN_ABI_CONSTRUCTOR_TRACE", trace.c_str(), 1);
    int result = 0;
    try
    {
        for (int i : { 1, 2, 4 }) Check(ModuleABI::Inspect(argv[i]).Compatible(), "SDK-built module has compatible marker");
        Check(ModuleABI::Inspect(argv[3]).Status == ModuleABIStatus::MissingRecord, "legacy module missing marker");
        Check(ScriptEngine::LoadModule(argv[1]), "load compatible v1");
        Check(Value() == 17, "v1 factory");
        int callbacks = 0;
        float seen = 0;
        ScriptEngine::SetBeforeUnloadCallback([&] {++callbacks;seen=Value(); });
        auto bad = work / "bad-abi.so";
        auto bytes = FileSystem::ReadBinaryFile(argv[2]);
        auto fingerprint = ModuleABI::EngineFingerprint();
        size_t patched = 0;
        for (size_t i = 0; i + fingerprint.size() <= bytes.size(); ++i)
            if (std::equal(fingerprint.begin(), fingerprint.end(), bytes.begin() + i))
            {
                bytes[i] = bytes[i] == 'a' ? 'b' : 'a';
                ++patched;
            }
        Check(patched > 0, "find embedded SDK fingerprint");
        Check(FileSystem::WriteBinaryFile(bad, bytes.data(), bytes.size()), "write incompatible module");
        auto before = FileSystem::ReadTextFile(trace);
        Check(!ScriptEngine::LoadModule(bad), "reject mismatched ABI");
        Check(callbacks == 0 && Value() == 17, "bad ABI preserves running module");
        Check(FileSystem::ReadTextFile(trace) == before, "bad ABI executes no constructors");
        Check(!ScriptEngine::LoadModule(argv[3]), "reject missing ABI marker");
        Check(callbacks == 0 && Value() == 17, "legacy rejection preserves v1");
        Check(FileSystem::ReadTextFile(trace) == before, "legacy constructor never runs");
        Check(!ScriptEngine::LoadModule(argv[4]), "unresolved dependency fails dlopen");
        Check(callbacks == 0 && Value() == 17, "failed dlopen preserves old registry/module");
        Check(!ScriptEngine::LoadModule(work / "missing.so"), "reject missing file");
        Check(Value() == 17 && callbacks == 0, "missing file preserves v1");
        Check(ScriptEngine::LoadModule(argv[2]), "hot reload compatible v2");
        Check(callbacks == 1 && seen == 17, "unload callback sees OLD class metadata");
        Check(Value() == 29, "new class committed after old unload");
        ScriptEngine::UnloadModule();
        Check(callbacks == 2 && seen == 29, "v2 unload callback");
        Check(!ScriptRegistry::Find("ABIProbe"), "module registrations removed");
        auto stripped = work / "stripped.so";
        fs::copy_file(argv[2], stripped);
        auto strippedResult = RunProcess({ "strip", "--strip-unneeded", stripped.string() });
        Check(strippedResult.Succeeded(), "strip module");
        Check(ModuleABI::Inspect(stripped).Compatible(), "strip retains ABI section");
        ScriptEngine::SetBeforeUnloadCallback({});
        Check(ScriptEngine::LoadModule(stripped), "load stripped module");
        Check(Value() == 29, "stripped module factory");
        ScriptEngine::UnloadModule();
        std::puts("PASS: native SDK ABI, constructor gate, failed reload preservation, staged registry, hot reload and strip");
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        result = 1;
    }
    ScriptEngine::SetBeforeUnloadCallback({});
    ScriptEngine::UnloadModule();
    if (had) setenv("ZAMIN_ABI_CONSTRUCTOR_TRACE", saved.c_str(), 1);
    else unsetenv("ZAMIN_ABI_CONSTRUCTOR_TRACE");
    fs::remove_all(work);
    return result;
}
