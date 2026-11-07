#include "Zamin/Asset/AssetInfo.h"
#include "Zamin/Renderer/Mesh.h"
#include "Zamin/Utils/FileSystem.h"

#include <stb_image.h>
#include <cgltf.h>

// Same configuration as the implementation in AudioEngine.cpp.
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>

namespace Zamin {

    namespace fs = std::filesystem;

    // ------------------------------------------------------------------ images
    static std::string ImageFormatName(const fs::path& path)
    {
        std::string ext = FileSystem::GetExtension(path);
        if (ext == ".png") return "PNG";
        if (ext == ".jpg" || ext == ".jpeg") return "JPEG";
        if (ext == ".bmp") return "BMP";
        if (ext == ".tga") return "TGA";
        if (ext == ".gif") return "GIF";
        if (ext == ".psd") return "PSD";
        if (ext == ".hdr") return "Radiance HDR";
        return ext.empty() ? std::string("?") : ext.substr(1);
    }

    ImageFileInfo AssetInfo::ReadImage(const fs::path& path)
    {
        ImageFileInfo info;
        info.Format = ImageFormatName(path);
        std::error_code ec;
        if (!fs::is_regular_file(path, ec))
        {
            info.Error = "fayl topilmadi";
            return info;
        }
        std::string file = path.string();
        int width = 0, height = 0, channels = 0;
        if (!stbi_info(file.c_str(), &width, &height, &channels))
        {
            const char* reason = stbi_failure_reason();
            info.Error = reason ? reason : "rasm emas";
            return info;
        }
        info.Valid = width > 0 && height > 0;
        info.Width = (uint32_t)width;
        info.Height = (uint32_t)height;
        info.Channels = (uint32_t)channels;
        info.Is16Bit = stbi_is_16_bit(file.c_str()) != 0;
        info.IsHDR = stbi_is_hdr(file.c_str()) != 0;
        if (!info.Valid)
            info.Error = "o'lchami 0";
        return info;
    }

    uint32_t AssetInfo::GetMipLevelCount(uint32_t width, uint32_t height)
    {
        uint32_t size = std::max(width, height), levels = 1;
        while (size > 1)
        {
            size /= 2;
            levels++;
        }
        return levels;
    }

    uint64_t AssetInfo::EstimateTextureMemory(uint32_t width, uint32_t height, uint32_t bytesPerPixel, bool mipmaps)
    {
        uint64_t total = 0;
        uint64_t w = std::max(1u, width), h = std::max(1u, height);
        while (true)
        {
            total += w * h * bytesPerPixel;
            if (!mipmaps || (w == 1 && h == 1))
                break;
            w = std::max<uint64_t>(1, w / 2);
            h = std::max<uint64_t>(1, h / 2);
        }
        return total;
    }

    std::string AssetInfo::FormatDuration(double seconds)
    {
        seconds = std::max(0.0, seconds);
        if (seconds < 10.0)
            return std::format("{:.3f} s", seconds);
        if (seconds < 60.0)
            return std::format("{:.1f} s", seconds);
        int minutes = (int)(seconds / 60.0);
        double rest = seconds - minutes * 60.0;
        return std::format("{}:{:04.1f}", minutes, rest);
    }

    // ------------------------------------------------------------------ models
    static const char* CgltfError(cgltf_result result)
    {
        switch (result)
        {
            case cgltf_result_data_too_short:   return "fayl qisqa (buzilgan?)";
            case cgltf_result_unknown_format:   return "noma'lum format";
            case cgltf_result_invalid_json:     return "JSON xato";
            case cgltf_result_invalid_gltf:     return "glTF tuzilishi xato";
            case cgltf_result_invalid_options:  return "noto'g'ri sozlamalar";
            case cgltf_result_file_not_found:   return "fayl topilmadi";
            case cgltf_result_io_error:         return "o'qish xatosi";
            case cgltf_result_out_of_memory:    return "xotira yetmadi";
            case cgltf_result_legacy_gltf:      return "eski glTF 1.0 qo'llab-quvvatlanmaydi";
            default:                            return "noma'lum xato";
        }
    }

    static bool IsFileUri(const char* uri)
    {
        if (!uri || !*uri)
            return false;
        std::string_view u(uri);
        return !u.starts_with("data:") && u.find("://") == std::string_view::npos;
    }

    static std::string DecodeUri(const char* uri)
    {
        std::string decoded = uri;
        cgltf_decode_uri(decoded.data());
        decoded.resize(std::strlen(decoded.c_str()));
        return decoded;
    }

    // Seconds of a sampler's input: the accessor's max when the file states it (required by
    // glTF), otherwise its last value (the buffers are loaded then).
    static float SamplerDuration(const cgltf_animation_sampler* sampler, bool buffersLoaded)
    {
        if (!sampler || !sampler->input)
            return 0.0f;
        const cgltf_accessor* input = sampler->input;
        if (input->has_max)
            return input->max[0];
        if (!buffersLoaded || input->count == 0)
            return 0.0f;
        float t = 0.0f;
        cgltf_accessor_read_float(input, input->count - 1, &t, 1);
        return t;
    }

    static void ReadGLTF(const fs::path& path, ModelFileInfo& info)
    {
        cgltf_options options{};
        cgltf_data* data = nullptr;
        std::string file = path.string();
        if (cgltf_result result = cgltf_parse_file(&options, file.c_str(), &data); result != cgltf_result_success)
        {
            info.Error = CgltfError(result);
            return;
        }
        std::unique_ptr<cgltf_data, void (*)(cgltf_data*)> guard(data, cgltf_free);
        info.Valid = true;
        info.Format = data->file_type == cgltf_file_type_glb ? "glTF (binary)" : "glTF";
        info.Generator = data->asset.generator ? data->asset.generator : "";
        info.Version = data->asset.version ? data->asset.version : "";
        info.Meshes = (uint32_t)data->meshes_count;
        info.Nodes = (uint32_t)data->nodes_count;
        info.Images = (uint32_t)data->images_count;
        info.Skins = (uint32_t)data->skins_count;
        info.Joints = data->skins_count ? (uint32_t)data->skins[0].joints_count : 0;

        // Files next to the model (buffers, images).
        fs::path dir = path.parent_path();
        // Returns false when the file is missing.
        auto addFile = [&](const char* uri) {
            if (!IsFileUri(uri))
                return true;
            std::string decoded = DecodeUri(uri);
            std::error_code ec;
            bool exists = fs::is_regular_file(dir / decoded, ec);
            if (std::find(info.ExternalFiles.begin(), info.ExternalFiles.end(), decoded) == info.ExternalFiles.end())
            {
                info.ExternalFiles.push_back(decoded);
                if (!exists)
                    info.MissingFiles.push_back(decoded);
            }
            return exists;
        };
        bool buffersPresent = true;
        for (cgltf_size i = 0; i < data->buffers_count; i++)
            buffersPresent = addFile(data->buffers[i].uri) && buffersPresent;
        for (cgltf_size i = 0; i < data->images_count; i++)
            addFile(data->images[i].uri);

        // Durations without a stated max need the buffers (counts and names do not).
        bool buffersLoaded = buffersPresent && cgltf_load_buffers(&options, data, file.c_str()) == cgltf_result_success;

        for (cgltf_size m = 0; m < data->materials_count; m++)
        {
            const cgltf_material& mat = data->materials[m];
            ModelFileInfo::MaterialInfo out;
            out.Name = mat.name ? mat.name : std::format("Material{}", m);
            if (mat.has_pbr_metallic_roughness)
            {
                const auto& pbr = mat.pbr_metallic_roughness;
                out.BaseColor = { pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3] };
                out.Metallic = pbr.metallic_factor;
                out.Roughness = pbr.roughness_factor;
                if (pbr.base_color_texture.texture) out.Maps.push_back("Albedo");
                if (pbr.metallic_roughness_texture.texture) out.Maps.push_back("MetalRough");
            }
            if (mat.normal_texture.texture) out.Maps.push_back("Normal");
            if (mat.emissive_texture.texture) out.Maps.push_back("Emissive");
            if (mat.occlusion_texture.texture) out.Maps.push_back("Occlusion");
            info.Materials.push_back(std::move(out));
        }

        for (cgltf_size m = 0; m < data->meshes_count; m++)
        {
            const cgltf_mesh& mesh = data->meshes[m];
            std::string meshName = mesh.name ? mesh.name : std::format("Mesh{}", m);
            for (cgltf_size p = 0; p < mesh.primitives_count; p++)
            {
                const cgltf_primitive& prim = mesh.primitives[p];
                ModelFileInfo::Primitive out;
                out.Mesh = meshName;
                if (prim.material)
                    out.Material = prim.material->name ? prim.material->name
                                                       : std::format("Material{}", (size_t)(prim.material - data->materials));
                for (cgltf_size a = 0; a < prim.attributes_count; a++)
                {
                    const cgltf_attribute& attr = prim.attributes[a];
                    if (attr.type == cgltf_attribute_type_position && attr.data)
                        out.Vertices = (uint32_t)attr.data->count;
                    if (attr.type == cgltf_attribute_type_joints)
                        out.Skinned = true;
                }
                if (prim.type == cgltf_primitive_type_triangles)
                    out.Triangles = (uint32_t)((prim.indices ? prim.indices->count : out.Vertices) / 3);
                else if (prim.type == cgltf_primitive_type_triangle_strip || prim.type == cgltf_primitive_type_triangle_fan)
                {
                    cgltf_size count = prim.indices ? prim.indices->count : out.Vertices;
                    out.Triangles = count >= 3 ? (uint32_t)(count - 2) : 0;
                }
                info.Vertices += out.Vertices;
                info.Triangles += out.Triangles;
                info.Primitives.push_back(std::move(out));
            }
        }

        for (cgltf_size a = 0; a < data->animations_count; a++)
        {
            const cgltf_animation& anim = data->animations[a];
            ModelFileInfo::Animation out;
            out.Name = anim.name ? anim.name : ("Anim" + std::to_string(a)); // the loader's default name
            out.Channels = (uint32_t)anim.channels_count;
            for (cgltf_size c = 0; c < anim.channels_count; c++)
            {
                const cgltf_animation_channel& ch = anim.channels[c];
                out.Duration = std::max(out.Duration, SamplerDuration(ch.sampler, buffersLoaded));
                if (data->skins_count && ch.target_node)
                    for (cgltf_size j = 0; j < data->skins[0].joints_count; j++)
                        if (data->skins[0].joints[j] == ch.target_node)
                            out.TargetsJoints = true;
            }
            info.Animations.push_back(std::move(out));
        }
    }

    ModelFileInfo AssetInfo::ReadModel(const fs::path& path)
    {
        ModelFileInfo info;
        std::error_code ec;
        if (!fs::is_regular_file(path, ec))
        {
            info.Error = "fayl topilmadi";
            return info;
        }
        std::string ext = FileSystem::GetExtension(path);
        if (ext == ".gltf" || ext == ".glb")
        {
            ReadGLTF(path, info);
            return info;
        }
        if (ext == ".obj")
        {
            info.Format = "OBJ";
            std::vector<MeshVertex> vertices;
            std::vector<uint32_t> indices;
            if (!MeshLibrary::ReadOBJ(path, vertices, indices, &info.Error))
                return info;
            info.Valid = true;
            info.Meshes = 1;
            ModelFileInfo::Primitive prim;
            prim.Mesh = path.stem().string();
            prim.Vertices = (uint32_t)vertices.size();
            prim.Triangles = (uint32_t)(indices.size() / 3);
            info.Vertices = prim.Vertices;
            info.Triangles = prim.Triangles;
            info.Primitives.push_back(std::move(prim));
            return info;
        }
        info.Error = "model formati emas";
        return info;
    }

    // ------------------------------------------------------------------ audio
    static std::string AudioFormatName(const fs::path& path)
    {
        std::string ext = FileSystem::GetExtension(path);
        if (ext == ".wav") return "WAV";
        if (ext == ".mp3") return "MP3";
        if (ext == ".flac") return "FLAC";
        if (ext == ".ogg") return "OGG Vorbis";
        return ext.empty() ? std::string("?") : ext.substr(1);
    }

    static const char* SampleFormatName(ma_format format)
    {
        switch (format)
        {
            case ma_format_u8:  return "8-bit";
            case ma_format_s16: return "16-bit";
            case ma_format_s24: return "24-bit";
            case ma_format_s32: return "32-bit";
            case ma_format_f32: return "32-bit float";
            default:            return "?";
        }
    }

    static std::string DecoderError(const fs::path& path, ma_result result)
    {
        if (result == MA_NO_BACKEND || result == MA_INVALID_FILE || result == MA_INVALID_DATA || result == MA_FORMAT_NOT_SUPPORTED)
            return FileSystem::GetExtension(path) == ".ogg" ? "OGG Vorbis dekoderi yo'q (WAV, MP3 yoki FLAC ishlating)"
                                                 : "format qo'llab-quvvatlanmaydi yoki fayl buzilgan";
        return ma_result_description(result);
    }

    AudioFileInfo AssetInfo::ReadAudio(const fs::path& path)
    {
        AudioFileInfo info;
        info.Format = AudioFormatName(path);
        std::error_code ec;
        if (!fs::is_regular_file(path, ec))
        {
            info.Error = "fayl topilmadi";
            return info;
        }
        ma_decoder decoder;
        ma_decoder_config config = ma_decoder_config_init(ma_format_unknown, 0, 0); // the file's own format
        if (ma_result result = ma_decoder_init_file(path.string().c_str(), &config, &decoder); result != MA_SUCCESS)
        {
            info.Error = DecoderError(path, result);
            return info;
        }
        ma_format format = ma_format_unknown;
        ma_uint32 channels = 0, sampleRate = 0;
        ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0);
        ma_uint64 frames = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
        ma_decoder_uninit(&decoder);

        info.SampleFormat = SampleFormatName(format);
        info.Channels = channels;
        info.SampleRate = sampleRate;
        info.Frames = frames;
        info.Duration = sampleRate ? (double)frames / (double)sampleRate : 0.0;
        info.Valid = channels > 0 && sampleRate > 0;
        if (!info.Valid)
            info.Error = "ovoz ma'lumoti yo'q";
        return info;
    }

    std::vector<glm::vec2> AssetInfo::ReadAudioWaveform(const fs::path& path, uint32_t buckets, std::string* error)
    {
        std::vector<glm::vec2> peaks;
        if (buckets == 0)
            return peaks;
        ma_decoder decoder;
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0); // floats, the file's channels / rate
        if (ma_result result = ma_decoder_init_file(path.string().c_str(), &config, &decoder); result != MA_SUCCESS)
        {
            if (error)
                *error = DecoderError(path, result);
            return peaks;
        }
        ma_uint32 channels = 0;
        ma_decoder_get_data_format(&decoder, nullptr, &channels, nullptr, nullptr, 0);
        ma_uint64 frames = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
        if (channels == 0 || frames == 0)
        {
            ma_decoder_uninit(&decoder);
            if (error)
                *error = "ovoz bo'sh";
            return peaks;
        }

        peaks.assign(buckets, glm::vec2(0.0f));
        std::vector<bool> touched(buckets, false);
        std::vector<float> chunk(4096 * (size_t)channels);
        ma_uint64 position = 0;
        while (position < frames)
        {
            ma_uint64 read = 0;
            if (ma_decoder_read_pcm_frames(&decoder, chunk.data(), 4096, &read) != MA_SUCCESS || read == 0)
                break;
            for (ma_uint64 f = 0; f < read; f++)
            {
                size_t bucket = (size_t)std::min<ma_uint64>(buckets - 1, (position + f) * buckets / frames);
                glm::vec2& peak = peaks[bucket];
                for (ma_uint32 c = 0; c < channels; c++)
                {
                    float v = std::clamp(chunk[(size_t)f * channels + c], -1.0f, 1.0f);
                    if (!touched[bucket])
                    {
                        peak = glm::vec2(v);
                        touched[bucket] = true;
                    }
                    peak.x = std::min(peak.x, v);
                    peak.y = std::max(peak.y, v);
                }
            }
            position += read;
        }
        ma_decoder_uninit(&decoder);
        return peaks;
    }

}
