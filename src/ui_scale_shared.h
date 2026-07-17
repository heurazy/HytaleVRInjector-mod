#pragma once

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace hytalevr {

inline constexpr char kUiScaleMappingNameA[] =
    "Local\\HytaleUIScaleSharedMemory_v2";
inline constexpr wchar_t kUiScaleMappingNameW[] =
    L"Local\\HytaleUIScaleSharedMemory_v2";
inline constexpr uint32_t kUiScaleMagic = 0x48595549; // HYUI
inline constexpr uint32_t kUiScaleVersion = 2;

struct UiScaleSharedData {
    uint32_t magic;
    uint32_t version;
    uint32_t structSize;
    uint32_t reservedHeader;
    float uiScale;
    float offsetX;
    float offsetY;
    int disableUboScaling;
    int disableShadows;
    int disableParticles;
    int disableDistortion;
    int hideFirstPerson;
    volatile LONG menuVisibleCounter;
    volatile LONG menuLargeDrawCounter;
    volatile LONG menuTextureId;
    volatile LONG menuTextureWidth;
    volatile LONG menuTextureHeight;
    volatile LONG menuTextureFrame;
    volatile LONG menuCaptureError;
    volatile LONG currentEye;
    volatile LONG renderFrameSequence;
    volatile LONG suppressMenuInGame;
    volatile LONG menuIgnoreDrawThreshold;
    int firstPersonControllerReanchor;
    int firstPersonControllerPoseValid;
    float firstPersonHandNdcX;
    float firstPersonHandNdcY;
    float firstPersonHandDepth;
    volatile LONG firstPersonMatrixPatches;
    volatile LONG sceneDepthTextureId;
    volatile LONG sceneDepthTextureWidth;
    volatile LONG sceneDepthTextureHeight;
    volatile LONG sceneDepthTextureFrame;
    float sceneDepthFarClip;
    volatile LONG distortionTextureId;
    volatile LONG distortionTextureWidth;
    volatile LONG distortionTextureHeight;
    volatile LONG distortionTextureFrame;
    volatile LONG vrSceneMatricesValid;
    volatile LONG vrSceneMatrixSequence;
    float vrSceneView[16];
    float vrSceneProjection[16];
    float vrSceneViewProjection[16];
    float vrSceneInvView[16];
    float vrSceneInvViewProjection[16];
    float vrSceneReprojection[16];
    float vrSceneProjectionInfo[4];

    // Head-relative OpenVR controller poses. Left is index 0, right is index 1.
    volatile LONG heldItemPoseSequence;
    volatile LONG heldItemPoseValidMask;
    int heldItemReanchorEnabled;
    float heldItemWorldScale;
    float heldItemLocalOffset[3];
    float heldItemVisualScale;
    float heldItemControllerPoses[2][12];
    volatile LONG heldItemMatrixPatches;
    volatile LONG heldItemMatrixFrame;
    volatile LONG heldItemLayerTextureId;
    volatile LONG heldItemDepthTextureId;
    volatile LONG heldItemDepthTextureWidth;
    volatile LONG heldItemDepthTextureHeight;
    volatile LONG heldItemDepthTextureFrame;
    volatile LONG heldItemLayerVisibleMask;
    float heldItemLayerViewPositions[2][3];

    // Source atlas state for the actual first-person item draw. The VR
    // renderer fingerprints this small atlas region against Assets.zip.
    volatile LONG heldItemAtlasSequence;
    volatile LONG heldItemAtlasFrame;
    volatile LONG heldItemAtlasValidMask;
    volatile LONG heldItemAtlasTextureIds[2];
    volatile LONG heldItemAtlasOffsets[2][2];
    volatile LONG heldItemAtlasSizes[2][2];
    volatile LONG heldItemVertexArrayIds[2];
    volatile LONG heldItemIndexCounts[2];
    volatile LONG heldItemUvBounds[2][4];

};

inline bool ui_scale_shared_data_compatible(const UiScaleSharedData& data) {
    const auto* shared =
        reinterpret_cast<const volatile UiScaleSharedData*>(&data);
    if (shared->magic != kUiScaleMagic) {
        return false;
    }
    MemoryBarrier();
    return shared->version == kUiScaleVersion &&
           shared->structSize == sizeof(UiScaleSharedData);
}

inline void initialize_ui_scale_shared_data(UiScaleSharedData& data) {
    std::memset(&data, 0, sizeof(data));
    data.uiScale = 1.0f;
    data.menuIgnoreDrawThreshold = 1;
    data.sceneDepthFarClip = 1024.0f;
    data.heldItemWorldScale = 1.0f;
    data.heldItemLocalOffset[2] = -0.10f;
    data.heldItemVisualScale = 0.30f;
    data.heldItemMatrixFrame = -1;
    data.heldItemDepthTextureFrame = -1;
    data.heldItemAtlasFrame = -1;
    data.version = kUiScaleVersion;
    data.structSize = sizeof(UiScaleSharedData);
    MemoryBarrier();
    InterlockedExchange(
        reinterpret_cast<volatile LONG*>(&data.magic),
        static_cast<LONG>(kUiScaleMagic));
}

} // namespace hytalevr
