#pragma once

#include "Zamin/Renderer/Model.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Zamin {

    // Decodes the textures and parses the models of streamed world cells off the main thread, under
    // hard file and byte budgets. CPU staging only: it never touches the GPU, the scene or the asset
    // registry. What it stages is taken by TextureLibrary::LoadStaged / Model::Load on the main thread
    // and uploaded by AsyncLoader::Update within the upload budget.
    //
    // Create and all public calls belong to one owner thread (the main thread: Submit asks the model
    // cache). Work runs on private workers, independently of JobSystem's synchronous mode. One worker
    // stages one ticket, its files in request order. Destruction cancels, joins and drops what no one took.
    class WorldAssetStager
    {
    public:
        enum class Kind
        {
            Texture,
            Model,
            Material // only read for the textures it names; MaterialLibrary loads the material itself
        };
        struct Request
        {
            Kind Type = Kind::Texture;
            std::filesystem::path File; // absolute
            bool PixelArt = false;      // Texture: the nearest-filtered cache entry
            ModelImportSettings Model;  // Model: read by the owner thread (AssetImporter)
            std::filesystem::path AssetRoot; // Material: what its texture paths are relative to
        };
        struct Limits
        {
            size_t Workers = 2;
            size_t MaxTickets = 16;
            // Files of one ticket, the textures its materials name included.
            size_t MaxFilesPerTicket = 256;
            // One source file on disk.
            uint64_t MaxFileBytes = 64ull * 1024 * 1024;
            // Decoded pixels and parsed mesh data of one ticket. An image is refused from its header,
            // before any of its pixels are decoded.
            uint64_t MaxTicketBytes = 256ull * 1024 * 1024;
            // Everything staged at once. A ticket is charged MaxTicketBytes while it is being staged and
            // what it really holds afterwards, until it is released; tickets that would not fit wait.
            // Images embedded in model files are decoded by the model parser and not charged.
            uint64_t MaxStagedBytes = 512ull * 1024 * 1024;
            // 0: off. A mesh is uploaded as one piece: one larger than this is refused (textures are cut
            // into rows and always fit).
            uint64_t MaxUploadUnitBytes = 0;

            bool Validate(std::string& error) const;
        };
        enum class State
        {
            Unknown, // never submitted, or released
            Queued,
            Staging,
            Ready,
            Failed // a budget was exceeded: nothing of the ticket stays staged
        };
        struct Status
        {
            State Stage = State::Unknown;
            size_t Files = 0;      // requested, the materials' textures included
            size_t Staged = 0;     // decoded / parsed by this ticket
            size_t Resident = 0;   // already in the libraries' caches
            size_t Unreadable = 0; // missing or broken: reported once, the cell lives without them
            uint64_t Bytes = 0;    // decoded pixels + mesh data this ticket holds
            uint64_t GPUBytes = 0; // what uploading them requests from the GPU (mip chains included)
            std::string Error;     // Failed: the file and the budget it exceeded
        };
        struct Stats
        {
            size_t Tickets = 0, Queued = 0, Staging = 0, Ready = 0;
            uint64_t ReservedBytes = 0, PeakReservedBytes = 0; // charged against MaxStagedBytes
            uint64_t Submitted = 0, Completed = 0, BudgetFailures = 0, Cancelled = 0;
            uint64_t FilesStaged = 0, FilesUnreadable = 0, StagedBytes = 0; // totals so far
            double WorkerMilliseconds = 0;
        };

        static std::unique_ptr<WorldAssetStager> Create(const Limits& limits, std::string& error);
        ~WorldAssetStager();
        WorldAssetStager(const WorldAssetStager&) = delete;
        WorldAssetStager& operator=(const WorldAssetStager&) = delete;

        // A ticket for these files (duplicates and models the cache already has are left out). 0 with
        // `error` when there are too many files or tickets; nothing is queued then.
        uint64_t Submit(std::vector<Request> requests, std::string& error);
        Status GetStatus(uint64_t ticket) const;
        // The ticket ends: staging under way is cancelled at its next file, what it staged and nothing
        // took is dropped (shared files stay for the other tickets that staged them), its bytes are free.
        void Release(uint64_t ticket);
        // The requests of a ready ticket that are staged or resident (unreadable ones left out).
        std::vector<Request> GetStaged(uint64_t ticket) const;
        Stats GetStats() const;
        const Limits& GetLimits() const;

    private:
        struct Data;
        explicit WorldAssetStager(std::unique_ptr<Data> data);
        std::unique_ptr<Data> m_Data;
    };
}
