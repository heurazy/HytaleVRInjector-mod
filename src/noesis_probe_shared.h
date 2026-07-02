#pragma once

#include <cstdint>

constexpr wchar_t kNoesisProbeMappingName[] = L"Local\\HytaleNoesisProbe";
constexpr uint32_t kNoesisProbeMagic = 0x4E50524F; // NPRO
constexpr uint32_t kNoesisProbeVersion = 1;

struct NoesisProbeShared {
    uint32_t magic = kNoesisProbeMagic;
    uint32_t version = kNoesisProbeVersion;
    volatile uint32_t hook_active = 0;
    volatile int32_t hook_error = 0;
    volatile uint32_t begin_calls = 0;
    volatile uint32_t end_calls = 0;
    volatile uint32_t begin_offscreen_calls = 0;
    volatile uint32_t end_offscreen_calls = 0;
    volatile uint32_t renderer_render_calls = 0;
    volatile uint32_t renderer_offscreen_calls = 0;
    volatile uint32_t update_render_tree_calls = 0;
    volatile uint32_t draw_batch_calls = 0;
    volatile uint32_t set_render_target_calls = 0;
    volatile uint32_t unload_requested = 0;
};
