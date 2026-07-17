#include <windows.h>

#include <chrono>
#include <iostream>
#include <thread>

#include "ui_scale_shared.h"

int wmain() {
    HANDLE mapping = OpenFileMappingW(
        FILE_MAP_ALL_ACCESS, FALSE, L"Local\\HytaleUIScaleSharedMemory");
    if (!mapping) {
        std::wcerr << L"UI scale shared mapping was not found.\n";
        return 2;
    }
    auto* shared = static_cast<const hytalevr::UiScaleSharedData*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                      sizeof(hytalevr::UiScaleSharedData)));
    if (!shared) {
        CloseHandle(mapping);
        std::wcerr << L"UI scale shared mapping could not be read.\n";
        return 3;
    }

    LONG previous_sequence = -1;
    for (int sample = 0; sample < 50; ++sample) {
        const LONG sequence = InterlockedCompareExchange(
            const_cast<volatile LONG*>(&shared->heldItemAtlasSequence), 0, 0);
        if (sequence != previous_sequence || sample == 49) {
            previous_sequence = sequence;
            std::wcout
                << L"frame=" << shared->renderFrameSequence
                << L" poseMask=" << shared->heldItemPoseValidMask
                << L" patches=" << shared->heldItemMatrixPatches
                << L" matrixFrame=" << shared->heldItemMatrixFrame
                << L" presenceMask=" << shared->heldItemLayerVisibleMask
                << L" atlasSeq=" << sequence
                << L" atlasFrame=" << shared->heldItemAtlasFrame
                << L" atlasMask=" << shared->heldItemAtlasValidMask
                << L"\n";
            for (int side = 0; side < 2; ++side) {
                std::wcout
                    << L"  side=" << side
                    << L" tex=" << shared->heldItemAtlasTextureIds[side]
                    << L" offset=" << shared->heldItemAtlasOffsets[side][0]
                    << L"," << shared->heldItemAtlasOffsets[side][1]
                    << L" size=" << shared->heldItemAtlasSizes[side][0]
                    << L"x" << shared->heldItemAtlasSizes[side][1]
                    << L" vao=" << shared->heldItemVertexArrayIds[side]
                    << L" count=" << shared->heldItemIndexCounts[side]
                    << L" uv=" << shared->heldItemUvBounds[side][0]
                    << L"," << shared->heldItemUvBounds[side][1]
                    << L"-" << shared->heldItemUvBounds[side][2]
                    << L"," << shared->heldItemUvBounds[side][3]
                    << L"\n";
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    return 0;
}
