#include "vr_item_assets.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool validate_item(const hytalevr::VrItemAssetCatalog& catalog,
                   const char* id,
                   std::string& error) {
    size_t definition_index = catalog.definitions.size();
    for (size_t index = 0; index < catalog.definitions.size(); ++index) {
        if (catalog.definitions[index].id == id) {
            definition_index = index;
            break;
        }
    }
    if (definition_index >= catalog.definitions.size()) {
        error = std::string("missing item definition: ") + id;
        return false;
    }

    hytalevr::VrItemRenderAsset asset;
    if (!hytalevr::load_vr_item_render_asset(
            catalog, definition_index, asset, error)) {
        return false;
    }
    for (const hytalevr::VrItemVertex& vertex : asset.vertices) {
        if (!std::isfinite(vertex.u) || !std::isfinite(vertex.v) ||
            vertex.u < -0.0001f || vertex.u > 1.0001f ||
            vertex.v < -0.0001f || vertex.v > 1.0001f) {
            error = std::string("item UV is outside its texture: ") + id;
            return false;
        }
    }
    return !asset.vertices.empty();
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::wcerr << L"usage: hytale_vr_item_assets_smoketest <Assets.zip>\n";
        return 2;
    }

    hytalevr::VrItemAssetCatalog catalog;
    std::string error;
    if (!hytalevr::load_vr_item_asset_catalog(argv[1], catalog, error)) {
        std::cerr << error << "\n";
        return 3;
    }
    if (catalog.definitions.empty() || catalog.texture_candidates.empty()) {
        std::cerr << "catalog is empty\n";
        return 4;
    }
    if (catalog.definitions.size() < 3000) {
        std::cerr << "catalog does not include the block item families\n";
        return 5;
    }

    size_t sample_index = 0;
    for (size_t index = 0; index < catalog.definitions.size(); ++index) {
        if (catalog.definitions[index].id == "Weapon_Sword_Iron") {
            sample_index = index;
            break;
        }
    }

    hytalevr::VrItemRenderAsset asset;
    if (!hytalevr::load_vr_item_render_asset(
            catalog, sample_index, asset, error)) {
        std::cerr << error << "\n";
        return 6;
    }
    if (asset.vertices.empty() || asset.texture_rgba.empty() ||
        asset.texture_width == 0 || asset.texture_height == 0) {
        std::cerr << "sample item did not produce render data\n";
        return 7;
    }
    if (!validate_item(catalog, "Weapon_Sword_Crude", error) ||
        !validate_item(catalog, "Weapon_Spear_Crude", error) ||
        !validate_item(catalog, "Ingredient_Stick", error)) {
        std::cerr << error << "\n";
        return 8;
    }

    size_t torch_index = catalog.definitions.size();
    for (size_t index = 0; index < catalog.definitions.size(); ++index) {
        if (catalog.definitions[index].id == "Furniture_Crude_Torch") {
            torch_index = index;
            break;
        }
    }
    hytalevr::VrItemRenderAsset torch;
    if (torch_index >= catalog.definitions.size() ||
        !hytalevr::load_vr_item_render_asset(
            catalog, torch_index, torch, error) ||
        !torch.flame_anchor.valid || !torch.needs_flame_effect ||
        !std::isfinite(torch.flame_anchor.x) ||
        !std::isfinite(torch.flame_anchor.y) ||
        !std::isfinite(torch.flame_anchor.z)) {
        std::cerr << "torch flame attachment is invalid: " << error << "\n";
        return 9;
    }
    uint32_t flame_width = 0;
    uint32_t flame_height = 0;
    std::vector<uint8_t> flame_rgba;
    if (!hytalevr::load_vr_asset_texture(
            catalog,
            "Common/Particles/Textures/Fire/Fire_Center_Erosion32.png",
            flame_width, flame_height, flame_rgba, error) ||
        flame_width != 64 || flame_height != 64 ||
        flame_rgba.size() != 64u * 64u * 4u) {
        std::cerr << "torch flame texture is invalid: " << error << "\n";
        return 10;
    }

    size_t generated_cubes = 0;
    size_t block_models = 0;
    for (const hytalevr::VrItemAssetDefinition& definition :
         catalog.definitions) {
        if (definition.generated_cube) ++generated_cubes;
        if (definition.model_path.rfind("Common/Blocks/", 0) == 0) {
            ++block_models;
        }
        if (definition.expected_vertex_count == 0 ||
            definition.uv_max_x < definition.uv_min_x ||
            definition.uv_max_y < definition.uv_min_y) {
            std::cerr << "invalid item metadata: " << definition.id << "\n";
            return 11;
        }
    }
    if (generated_cubes == 0 || block_models == 0) {
        std::cerr << "block cube or custom-model coverage is missing\n";
        return 12;
    }
    for (const hytalevr::VrItemTextureCandidate& candidate :
         catalog.texture_candidates) {
        if (candidate.definition_indices.empty()) {
            std::cerr << "texture candidate has no item definitions\n";
            return 13;
        }
        for (size_t definition_index : candidate.definition_indices) {
            if (definition_index >= catalog.definitions.size()) {
                std::cerr << "texture candidate definition is out of range\n";
                return 14;
            }
        }
    }

    std::vector<uint8_t> fallback_rgba(16 * 16 * 4, 255);
    hytalevr::VrItemRenderAsset fallback;
    if (!hytalevr::build_vr_item_fallback_cube(
            fallback_rgba, 16, 16, fallback, error) ||
        fallback.vertices.size() != 36) {
        std::cerr << "captured item fallback failed: " << error << "\n";
        return 15;
    }

    std::cout << "definitions=" << catalog.definitions.size()
              << " textures=" << catalog.texture_candidates.size()
              << " cubes=" << generated_cubes
              << " blockModels=" << block_models
              << " sample=" << asset.definition.id
              << " vertices=" << asset.vertices.size()
              << " torchFlame=(" << torch.flame_anchor.x << ","
              << torch.flame_anchor.y << "," << torch.flame_anchor.z << ")"
              << " texture=" << asset.texture_width << "x"
              << asset.texture_height << "\n";
    return 0;
}
