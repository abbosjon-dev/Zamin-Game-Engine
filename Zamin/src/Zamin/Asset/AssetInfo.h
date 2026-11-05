#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Zamin {

    // What an image file holds, read from its header (nothing is decoded).
    struct ImageFileInfo
    {
        bool Valid = false;
        uint32_t Width = 0, Height = 0;
        uint32_t Channels = 0;       // stored in the file: 1 gray, 2 gray + alpha, 3 RGB, 4 RGBA
        bool Is16Bit = false;        // 16 bits per channel (PNG / PSD)
        bool IsHDR = false;          // floating point (Radiance .hdr)
        std::string Format;          // "PNG", "JPEG", "Radiance HDR"...
        std::string Error;           // why it is not valid
    };

    // A model file without loading it on the GPU: counts, materials, skeleton and animations.
    struct ModelFileInfo
    {
        struct Primitive
        {
            std::string Mesh;        // name of the mesh the primitive belongs to
            std::string Material;    // "" = none
            uint32_t Vertices = 0;
            uint32_t Triangles = 0;  // 0 for lines / points (not drawn)
            bool Skinned = false;
        };
        struct MaterialInfo
        {
            std::string Name;
            glm::vec4 BaseColor{ 1.0f };
            float Metallic = 1.0f, Roughness = 1.0f;
            // Texture slots that are set ("Albedo", "Normal", "MetalRough", "Emissive", "Occlusion").
            std::vector<std::string> Maps;
        };
        struct Animation
        {
            std::string Name;
            float Duration = 0.0f;   // seconds
            uint32_t Channels = 0;
            bool TargetsJoints = false; // only skeleton animations are played by the Animator
        };

        bool Valid = false;
        std::string Format;          // "glTF", "glTF (binary)", "OBJ"
        std::string Generator;       // the exporter (glTF asset.generator)
        std::string Version;         // glTF asset.version
        uint32_t Meshes = 0, Nodes = 0, Images = 0, Skins = 0;
        uint32_t Joints = 0;         // of the first skin (the one the engine uses)
        std::vector<Primitive> Primitives;
        uint64_t Vertices = 0, Triangles = 0; // over every primitive (each mesh once)
        std::vector<MaterialInfo> Materials;
        std::vector<Animation> Animations;
        std::vector<std::string> ExternalFiles; // buffers and images next to the model (relative uri)
        std::vector<std::string> MissingFiles;  // of those, the ones that do not exist
        std::string Error;
    };

    // What a sound file holds (read by the decoder; the samples are not kept).
    struct AudioFileInfo
    {
        bool Valid = false;
        std::string Format;          // "WAV", "MP3", "FLAC"...
        std::string SampleFormat;    // "16-bit", "24-bit", "32-bit float"...
        uint32_t Channels = 0;
        uint32_t SampleRate = 0;     // Hz
        uint64_t Frames = 0;         // samples per channel
        double Duration = 0.0;       // seconds
        std::string Error;
    };

    // Readers for the asset inspector. They only read files (no GPU, no audio device), so they
    // work headless and from any thread.
    class AssetInfo
    {
    public:
        static ImageFileInfo ReadImage(const std::filesystem::path& path);
        // .gltf / .glb (meshes, materials, skins, animations) or .obj (vertices and triangles).
        static ModelFileInfo ReadModel(const std::filesystem::path& path);
        static AudioFileInfo ReadAudio(const std::filesystem::path& path);
        // The sound's outline: `buckets` (min, max) pairs in -1..1 from start to end (every
        // channel together). Empty, with the reason in `error`, when it cannot be decoded.
        static std::vector<glm::vec2> ReadAudioWaveform(const std::filesystem::path& path, uint32_t buckets,
                                                        std::string* error = nullptr);

        // Levels of a full mip chain down to 1x1 (1 = only the image itself).
        static uint32_t GetMipLevelCount(uint32_t width, uint32_t height);
        // Bytes a texture takes on the GPU: every level of its mip chain when `mipmaps`.
        static uint64_t EstimateTextureMemory(uint32_t width, uint32_t height, uint32_t bytesPerPixel, bool mipmaps);
        // "0.250 s", "12.5 s", "2:05.3".
        static std::string FormatDuration(double seconds);
    };

}
