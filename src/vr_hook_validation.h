#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hytalevr {

inline bool camera_hook_site_valid(const unsigned char* target) {
    constexpr unsigned char first_prefix[] = {0x0F, 0x28, 0xB4, 0x24};
    constexpr unsigned char second_prefix[] = {0x0F, 0x28, 0xBC, 0x24};
    if (!target || std::memcmp(target, first_prefix, sizeof(first_prefix)) != 0 ||
        std::memcmp(target + 8, second_prefix, sizeof(second_prefix)) != 0) {
        return false;
    }
    int32_t first_stack_offset = 0;
    int32_t second_stack_offset = 0;
    std::memcpy(&first_stack_offset, target + 4, sizeof(first_stack_offset));
    std::memcpy(&second_stack_offset, target + 12, sizeof(second_stack_offset));
    return first_stack_offset >= 0x40 && first_stack_offset <= 0x4000 &&
           (first_stack_offset & 0x0F) == 0 &&
           second_stack_offset == first_stack_offset - 0x10;
}

inline bool interaction_hook_site_valid(const unsigned char* target) {
    constexpr unsigned char first_prefix[] = {0x0F, 0x29, 0xBD};
    constexpr unsigned char second_prefix[] = {0x44, 0x0F, 0x29, 0x85};
    if (!target || std::memcmp(target, first_prefix, sizeof(first_prefix)) != 0 ||
        std::memcmp(target + 7, second_prefix, sizeof(second_prefix)) != 0) {
        return false;
    }
    int32_t origin_stack_offset = 0;
    int32_t direction_stack_offset = 0;
    std::memcpy(&origin_stack_offset, target + 3, sizeof(origin_stack_offset));
    std::memcpy(&direction_stack_offset, target + 11, sizeof(direction_stack_offset));
    return origin_stack_offset <= -0x20 && origin_stack_offset >= -0x4000 &&
           (origin_stack_offset & 0x0F) == 0 &&
           direction_stack_offset == origin_stack_offset - 0x10;
}

inline bool absolute_jump_patch_matches(const unsigned char* target, size_t size,
                                        const void* destination) {
    if (!target || size < 12 || target[0] != 0x48 || target[1] != 0xB8 ||
        target[10] != 0xFF || target[11] != 0xE0) {
        return false;
    }
    uintptr_t encoded_destination = 0;
    std::memcpy(&encoded_destination, target + 2, sizeof(encoded_destination));
    if (encoded_destination != reinterpret_cast<uintptr_t>(destination)) return false;
    for (size_t index = 12; index < size; ++index) {
        if (target[index] != 0x90) return false;
    }
    return true;
}

} // namespace hytalevr
