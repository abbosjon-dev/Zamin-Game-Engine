#pragma once
#include "Zamin/Editor/CompilerDiagnostics.h"
#include <filesystem>
#include <functional>
#include <memory>
namespace Zamin {
    class CodeEditorPanel
    {
    public:
        CodeEditorPanel();
        ~CodeEditorPanel();
        CodeEditorPanel(const CodeEditorPanel&) = delete;
        CodeEditorPanel& operator=(const CodeEditorPanel&) = delete;
        bool Open(const std::filesystem::path& file, int line = 0, int column = 0);
        void OnImGuiRender(bool* show);
        bool SaveAll();
        void FlushRecovery();
        bool IsFocused() const;
        void SetProject(const std::filesystem::path& root, const std::filesystem::path& cache);
        void SetBuildRunning(bool running);
        std::function<void()> OnNewScript;
        void SetDiagnostics(const std::vector<CompilerDiagnostic>& diagnostics);
        std::function<void()> OnBuild;
        std::function<void(const std::filesystem::path&, int, int)> OnExternalOpen;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
