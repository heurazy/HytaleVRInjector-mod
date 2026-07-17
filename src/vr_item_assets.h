#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hytalevr {

struct VrItemVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

struct VrItemAttachmentPoint {
    bool valid = false;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct VrItemAssetDefinition {
    std::string id;
    std::string model_path;
    std::string texture_path;
    int preference = 0;
    bool generated_cube = false;
    size_t expected_vertex_count = 0;
    int uv_min_x = 0;
    int uv_min_y = 0;
    int uv_max_x = 0;
    int uv_max_y = 0;
};

inline bool vr_item_definition_is_block_or_decor(
        const VrItemAssetDefinition& definition) {
    return definition.generated_cube ||
           definition.model_path.rfind("Common/Blocks/", 0) == 0;
}

struct VrItemTextureCandidate {
    std::vector<size_t> definition_indices;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
    uint64_t fingerprint = 0;
    uint64_t flipped_fingerprint = 0;
    uint64_t alpha_fingerprint = 0;
    uint64_t flipped_alpha_fingerprint = 0;
};

struct VrItemRenderAsset {
    VrItemAssetDefinition definition;
    uint32_t texture_width = 0;
    uint32_t texture_height = 0;
    std::vector<uint8_t> texture_rgba;
    std::vector<VrItemVertex> vertices;
    VrItemAttachmentPoint flame_anchor;
    bool needs_flame_effect = false;
};

struct VrZipEntry {
    uint64_t local_header_offset = 0;
    uint64_t compressed_size = 0;
    uint64_t uncompressed_size = 0;
    uint16_t method = 0;
};

struct VrItemAssetCatalog {
    std::wstring zip_path;
    std::unordered_map<std::string, VrZipEntry> zip_entries;
    std::vector<VrItemAssetDefinition> definitions;
    std::vector<VrItemTextureCandidate> texture_candidates;
    std::vector<std::pair<uint32_t, uint32_t>> texture_dimensions;
};

inline bool vr_item_id_is_tool_or_weapon(std::string_view id) {
    return id.rfind("Tool_", 0) == 0 || id.rfind("Weapon_", 0) == 0;
}

inline bool vr_item_id_is_shield(std::string_view id) {
    return id.rfind("Weapon_Shield_", 0) == 0;
}

inline bool vr_item_id_is_torch(std::string_view id) {
    return id.find("Torch") != std::string_view::npos;
}

inline bool vr_item_shield_allowed_for_main_item(std::string_view main_item_id) {
    return vr_item_id_is_tool_or_weapon(main_item_id);
}

bool load_vr_item_asset_catalog(const std::wstring& assets_zip,
                                VrItemAssetCatalog& catalog,
                                std::string& error);

bool load_vr_item_render_asset(const VrItemAssetCatalog& catalog,
                               size_t definition_index,
                               VrItemRenderAsset& asset,
                               std::string& error);

bool load_vr_asset_texture(const VrItemAssetCatalog& catalog,
                           const std::string& asset_path,
                           uint32_t& width,
                           uint32_t& height,
                           std::vector<uint8_t>& rgba,
                           std::string& error);

bool build_vr_item_fallback_cube(const std::vector<uint8_t>& rgba,
                                 uint32_t width,
                                 uint32_t height,
                                 VrItemRenderAsset& asset,
                                 std::string& error);

double vr_item_texture_difference(const uint8_t* atlas_rgba,
                                  size_t atlas_stride,
                                  bool flip_rows,
                                  const VrItemTextureCandidate& candidate);

uint64_t vr_item_texture_fingerprint(const uint8_t* rgba,
                                     uint32_t width,
                                     uint32_t height,
                                     size_t stride,
                                     bool flip_rows);

uint64_t vr_item_alpha_fingerprint(const uint8_t* rgba,
                                   uint32_t width,
                                   uint32_t height,
                                   size_t stride,
                                   bool flip_rows);

} // namespace hytalevr
