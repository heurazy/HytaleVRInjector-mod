#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <thread>
#include <vector>

namespace {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Matrix4 {
    float value[16]{};
};

struct Candidate {
    uintptr_t triple = 0;
    uintptr_t viewtarget = 0;
    Vec3 value;
};

struct ModuleRange {
    uintptr_t base = 0;
    uintptr_t end = 0;
    std::wstring name;
};

struct GlTableCandidate {
    uintptr_t address = 0;
    uintptr_t use_program = 0;
    uintptr_t get_uniform_location = 0;
    uintptr_t uniform3f = 0;
    uintptr_t uniform3fv = 0;
    uintptr_t uniform_matrix4fv = 0;
};

struct CameraChainCandidate {
    uintptr_t camera_update = 0;
    uintptr_t viewtarget = 0;
    Vec3 camera_value;
    Vec3 viewtarget_value;
};

DWORD find_process_id(const wchar_t* exe_name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exe_name) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return pid;
}

uintptr_t parse_hex(const wchar_t* text) {
    if (!text) return 0;
    return static_cast<uintptr_t>(_wcstoui64(text, nullptr, 0));
}

float parse_float(const wchar_t* text, float fallback) {
    if (!text) return fallback;
    wchar_t* end = nullptr;
    float value = static_cast<float>(wcstod(text, &end));
    return end == text ? fallback : value;
}

double parse_double(const wchar_t* text, double fallback) {
    if (!text) return fallback;
    wchar_t* end = nullptr;
    double value = wcstod(text, &end);
    return end == text ? fallback : value;
}

bool finite_reasonable(float v) {
    return std::isfinite(v) && std::fabs(v) < 100000.0f;
}

bool read_exact(HANDLE process, uintptr_t address, void* out, size_t size) {
    SIZE_T read = 0;
    return ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), out, size, &read) &&
           read == size;
}

bool write_exact(HANDLE process, uintptr_t address, const void* data, size_t size) {
    SIZE_T written = 0;
    return WriteProcessMemory(process, reinterpret_cast<LPVOID>(address), data, size, &written) &&
           written == size;
}

bool read_vec3(HANDLE process, uintptr_t address, Vec3& out) {
    return read_exact(process, address, &out, sizeof(out));
}

bool read_ptr(HANDLE process, uintptr_t address, uintptr_t& out) {
    return read_exact(process, address, &out, sizeof(out));
}

bool write_vec3(HANDLE process, uintptr_t address, const Vec3& value) {
    return write_exact(process, address, &value, sizeof(value));
}

Matrix4 multiply_matrix(const Matrix4& a, const Matrix4& b) {
    Matrix4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result.value[column * 4 + row] +=
                    a.value[k * 4 + row] * b.value[column * 4 + k];
            }
        }
    }
    return result;
}

Matrix4 make_roll(float radians) {
    Matrix4 result{};
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result.value[0] = c;
    result.value[1] = s;
    result.value[4] = -s;
    result.value[5] = c;
    result.value[10] = 1.0f;
    result.value[15] = 1.0f;
    return result;
}

std::vector<ModuleRange> enumerate_modules(DWORD pid) {
    std::vector<ModuleRange> modules;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return modules;

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snap, &entry)) {
        do {
            const auto base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
            modules.push_back(ModuleRange{base, base + entry.modBaseSize, entry.szModule});
        } while (Module32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return modules;
}

bool name_equals_ci(const std::wstring& a, const wchar_t* b) {
    return _wcsicmp(a.c_str(), b) == 0;
}

bool address_in_ranges(uintptr_t address, const std::vector<ModuleRange>& ranges) {
    for (const auto& range : ranges) {
        if (address >= range.base && address < range.end) return true;
    }
    return false;
}

bool address_in_named_range(uintptr_t address, const std::vector<ModuleRange>& ranges,
                            const wchar_t* name) {
    for (const auto& range : ranges) {
        if (name_equals_ci(range.name, name) && address >= range.base && address < range.end) {
            return true;
        }
    }
    return false;
}

bool looks_readable_table_page(DWORD protect) {
    if (protect & PAGE_GUARD) return false;
    if (protect & PAGE_NOACCESS) return false;
    const DWORD base = protect & 0xff;
    return base == PAGE_READONLY || base == PAGE_READWRITE || base == PAGE_WRITECOPY ||
           base == PAGE_EXECUTE_READ || base == PAGE_EXECUTE_READWRITE ||
           base == PAGE_EXECUTE_WRITECOPY;
}

std::vector<GlTableCandidate> scan_for_gl_tables(HANDLE process, DWORD pid, size_t max_results) {
    const auto modules = enumerate_modules(pid);
    std::vector<ModuleRange> gl_ranges;
    for (const auto& module : modules) {
        if (name_equals_ci(module.name, L"nvoglv64.dll") ||
            name_equals_ci(module.name, L"OPENGL32.DLL") ||
            name_equals_ci(module.name, L"GLU32.dll")) {
            gl_ranges.push_back(module);
        }
    }

    std::vector<GlTableCandidate> results;
    uintptr_t address = 0x10000;
    const uintptr_t max_address = 0x00007FFFFFFFFFFF;

    while (address < max_address && results.size() < max_results) {
        MEMORY_BASIC_INFORMATION mbi{};
        SIZE_T query = VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi));
        if (query == 0) break;

        const auto base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const auto size = static_cast<size_t>(mbi.RegionSize);
        const bool committed = mbi.State == MEM_COMMIT;

        if (committed && looks_readable_table_page(mbi.Protect) && size >= 0x3C0 &&
            size <= 64ull * 1024ull * 1024ull) {
            std::vector<unsigned char> buffer(size);
            SIZE_T bytes_read = 0;
            if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(base), buffer.data(),
                                  buffer.size(), &bytes_read)) {
                for (size_t i = 0; i + 0x3C0 <= bytes_read && results.size() < max_results;
                     i += sizeof(uintptr_t)) {
                    auto read_ptr = [&](size_t off) {
                        uintptr_t v = 0;
                        std::memcpy(&v, buffer.data() + i + off, sizeof(v));
                        return v;
                    };

                    const uintptr_t use_program = read_ptr(0x310);
                    const uintptr_t get_uniform_location = read_ptr(0x338);
                    const uintptr_t uniform3f = read_ptr(0x388);
                    const uintptr_t uniform3fv = read_ptr(0x3A8);
                    const uintptr_t uniform_matrix4fv = read_ptr(0x3B8);

                    const bool has_driver_ptr =
                        address_in_named_range(use_program, gl_ranges, L"nvoglv64.dll") ||
                        address_in_named_range(get_uniform_location, gl_ranges, L"nvoglv64.dll") ||
                        address_in_named_range(uniform3f, gl_ranges, L"nvoglv64.dll") ||
                        address_in_named_range(uniform3fv, gl_ranges, L"nvoglv64.dll") ||
                        address_in_named_range(uniform_matrix4fv, gl_ranges, L"nvoglv64.dll");
                    const bool not_same_stub =
                        use_program != get_uniform_location &&
                        get_uniform_location != uniform_matrix4fv &&
                        use_program != uniform_matrix4fv;

                    if (address_in_ranges(use_program, gl_ranges) &&
                        address_in_ranges(get_uniform_location, gl_ranges) &&
                        address_in_ranges(uniform_matrix4fv, gl_ranges) &&
                        (has_driver_ptr || not_same_stub)) {
                        results.push_back(GlTableCandidate{base + i, use_program,
                                                           get_uniform_location, uniform3f,
                                                           uniform3fv, uniform_matrix4fv});
                    }
                }
            }
        }

        const uintptr_t next = base + size;
        if (next <= address) break;
        address = next;
    }

    return results;
}

std::vector<Candidate> scan_for_viewtargets(HANDLE process, Vec3 center, float tolerance,
                                            size_t max_results) {
    std::vector<Candidate> results;
    uintptr_t address = 0x10000;
    const uintptr_t max_address = 0x00007FFFFFFFFFFF;

    while (address < max_address && results.size() < max_results) {
        MEMORY_BASIC_INFORMATION mbi{};
        SIZE_T query = VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi));
        if (query == 0) break;

        const auto base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const auto size = static_cast<size_t>(mbi.RegionSize);
        const bool committed = mbi.State == MEM_COMMIT;
        const bool guarded = (mbi.Protect & PAGE_GUARD) != 0;
        const bool no_access = (mbi.Protect & PAGE_NOACCESS) != 0;
        const bool readable = committed && !guarded && !no_access;

        if (readable && size > 0 && size <= 64ull * 1024ull * 1024ull) {
            std::vector<unsigned char> buffer(size);
            SIZE_T bytes_read = 0;
            if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(base), buffer.data(),
                                  buffer.size(), &bytes_read)) {
                for (size_t i = 0; i + 20 <= bytes_read && results.size() < max_results; i += 4) {
                    Vec3 value{};
                    std::memcpy(&value, buffer.data() + i, sizeof(value));
                    if (!finite_reasonable(value.x) || !finite_reasonable(value.y) ||
                        !finite_reasonable(value.z)) {
                        continue;
                    }
                    if (std::fabs(value.x - center.x) > tolerance ||
                        std::fabs(value.y - center.y) > tolerance ||
                        std::fabs(value.z - center.z) > tolerance) {
                        continue;
                    }

                    float extra0 = 9999.0f;
                    float extra1 = 9999.0f;
                    std::memcpy(&extra0, buffer.data() + i + 12, sizeof(float));
                    std::memcpy(&extra1, buffer.data() + i + 16, sizeof(float));
                    if (!std::isfinite(extra0) || !std::isfinite(extra1) ||
                        std::fabs(extra0) > 10000.0f || std::fabs(extra1) > 10000.0f) {
                        continue;
                    }

                    const uintptr_t triple = base + i;
                    if (triple < 0x54) continue;
                    results.push_back(Candidate{triple, triple - 0x54, value});
                }
            }
        }

        const uintptr_t next = base + size;
        if (next <= address) break;
        address = next;
    }

    return results;
}

bool close_vec3(const Vec3& a, const Vec3& b, float tolerance) {
    return std::fabs(a.x - b.x) <= tolerance && std::fabs(a.y - b.y) <= tolerance &&
           std::fabs(a.z - b.z) <= tolerance;
}

std::vector<CameraChainCandidate> scan_for_camera_chains(HANDLE process, Vec3 center,
                                                         float tolerance,
                                                         size_t max_results) {
    std::vector<CameraChainCandidate> results;
    uintptr_t address = 0x10000;
    const uintptr_t max_address = 0x00007FFFFFFFFFFF;

    while (address < max_address && results.size() < max_results) {
        MEMORY_BASIC_INFORMATION mbi{};
        SIZE_T query = VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi));
        if (query == 0) break;

        const auto base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const auto size = static_cast<size_t>(mbi.RegionSize);
        const bool committed = mbi.State == MEM_COMMIT;

        if (committed && looks_readable_table_page(mbi.Protect) && size >= 0x320 &&
            size <= 64ull * 1024ull * 1024ull) {
            std::vector<unsigned char> buffer(size);
            SIZE_T bytes_read = 0;
            if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(base), buffer.data(),
                                  buffer.size(), &bytes_read)) {
                for (size_t i = 0x2FC; i + 0x310 <= bytes_read && results.size() < max_results;
                     i += 4) {
                    Vec3 camera_value{};
                    std::memcpy(&camera_value, buffer.data() + i, sizeof(camera_value));
                    if (!finite_reasonable(camera_value.x) || !finite_reasonable(camera_value.y) ||
                        !finite_reasonable(camera_value.z) ||
                        !close_vec3(camera_value, center, tolerance)) {
                        continue;
                    }

                    const uintptr_t camera_update = base + i - 0x2FC;
                    uintptr_t p0 = 0, p1 = 0, p2 = 0, viewtarget = 0;
                    Vec3 viewtarget_value{};
                    if (!read_ptr(process, camera_update + 0x18, p0) || p0 < 0x10000) continue;
                    if (!read_ptr(process, p0 + 0x30, p1) || p1 < 0x10000) continue;
                    if (!read_ptr(process, p1 + 0x110, p2) || p2 < 0x10000) continue;
                    if (!read_ptr(process, p2 + 0x3B0, viewtarget) || viewtarget < 0x10000) continue;
                    if (!read_vec3(process, viewtarget + 0x54, viewtarget_value)) continue;
                    if (!close_vec3(camera_value, viewtarget_value, 0.25f)) continue;

                    results.push_back(CameraChainCandidate{camera_update, viewtarget, camera_value,
                                                           viewtarget_value});
                }
            }
        }

        const uintptr_t next = base + size;
        if (next <= address) break;
        address = next;
    }

    return results;
}

void print_usage() {
    std::puts("hytale_viewtarget_writer");
    std::puts("");
    std::puts("Modes:");
    std::puts("  scan [centerX centerY centerZ tolerance]");
    std::puts("  rawscan [centerX centerY centerZ tolerance]");
    std::puts("  test --viewtarget 0xADDR [dx dy dz seconds hz]");
    std::puts("  test [centerX centerY centerZ tolerance dx dy dz seconds hz]");
    std::puts("  spray [centerX centerY centerZ tolerance dx dy dz seconds hz]");
    std::puts("  glscan");
    std::puts("  camchain [centerX centerY centerZ tolerance]");
    std::puts("  testaddr 0xADDR [dx dy dz seconds hz]");
    std::puts("  rolltest --camera 0xADDR [degrees seconds hz]");
    std::puts("");
    std::puts("Examples:");
    std::puts("  hytale_viewtarget_writer scan 158 122 256 25");
    std::puts("  hytale_viewtarget_writer rawscan 158.11 121 256.118 0.25");
    std::puts("  hytale_viewtarget_writer test --viewtarget 0x1B368C0088C 5 0 0 2 120");
    std::puts("  hytale_viewtarget_writer spray 158 122 256 25 50 0 0 5 120");
    std::puts("  hytale_viewtarget_writer glscan");
    std::puts("  hytale_viewtarget_writer camchain 158.112 122.599 265.118 0.5");
    std::puts("  hytale_viewtarget_writer testaddr 0x28DD4BC0568 5 0 0 2 120");
    std::puts("  hytale_viewtarget_writer rolltest --camera 0x21364FFE110 20 5 1000");
}

int run_rolltest(HANDLE process, int argc, wchar_t** argv) {
    if (argc < 4 || _wcsicmp(argv[2], L"--camera") != 0) {
        print_usage();
        return 1;
    }

    const uintptr_t camera = parse_hex(argv[3]);
    const float degrees = argc >= 5 ? parse_float(argv[4], 20.0f) : 20.0f;
    const double seconds = argc >= 6 ? parse_double(argv[5], 5.0) : 5.0;
    const double hz = argc >= 7 ? parse_double(argv[6], 1000.0) : 1000.0;
    if (camera < 0x10000) {
        std::puts("Invalid camera address.");
        return 2;
    }

    constexpr uintptr_t kViewOffset = 0x2E0;
    constexpr uintptr_t kViewProjectionOffset = 0x320;
    constexpr uintptr_t kProjectionOffset = 0x4E0;
    Matrix4 original_view{}, original_view_projection{}, projection{};
    if (!read_exact(process, camera + kViewOffset, &original_view, sizeof(original_view)) ||
        !read_exact(process, camera + kViewProjectionOffset, &original_view_projection,
                    sizeof(original_view_projection)) ||
        !read_exact(process, camera + kProjectionOffset, &projection, sizeof(projection))) {
        std::printf("Failed to read camera matrices at 0x%p\n",
                    reinterpret_cast<void*>(camera));
        return 3;
    }

    constexpr float kPi = 3.14159265358979323846f;
    const Matrix4 roll = make_roll(degrees * kPi / 180.0f);
    const Matrix4 rolled_view = multiply_matrix(roll, original_view);
    const Matrix4 rolled_view_projection = multiply_matrix(projection, rolled_view);
    const auto delay = std::chrono::duration<double>(1.0 / std::max(1.0, hz));
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    size_t writes = 0;

    std::printf("camera=0x%p view=+0x%llX viewProjection=+0x%llX projection=+0x%llX "
                "roll=%.2f degrees seconds=%.2f hz=%.1f\n",
                reinterpret_cast<void*>(camera),
                static_cast<unsigned long long>(kViewOffset),
                static_cast<unsigned long long>(kViewProjectionOffset),
                static_cast<unsigned long long>(kProjectionOffset), degrees, seconds, hz);

    while (std::chrono::steady_clock::now() < end) {
        if (write_exact(process, camera + kViewOffset, &rolled_view, sizeof(rolled_view)) &&
            write_exact(process, camera + kViewProjectionOffset, &rolled_view_projection,
                        sizeof(rolled_view_projection))) {
            ++writes;
        }
        std::this_thread::sleep_for(delay);
    }

    write_exact(process, camera + kViewOffset, &original_view, sizeof(original_view));
    write_exact(process, camera + kViewProjectionOffset, &original_view_projection,
                sizeof(original_view_projection));
    std::printf("writes=%zu matrices restored\n", writes);
    return writes == 0 ? 4 : 0;
}

int run_scan(HANDLE process, int argc, wchar_t** argv) {
    Vec3 center{158.0f, 122.0f, 256.0f};
    float tolerance = 25.0f;
    if (argc >= 5) {
        center.x = parse_float(argv[2], center.x);
        center.y = parse_float(argv[3], center.y);
        center.z = parse_float(argv[4], center.z);
    }
    if (argc >= 6) tolerance = parse_float(argv[5], tolerance);

    auto candidates = scan_for_viewtargets(process, center, tolerance, 64);
    std::printf("scan center=(%.3f %.3f %.3f) tolerance=%.3f candidates=%zu\n",
                center.x, center.y, center.z, tolerance, candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        std::printf("[%02zu] viewTarget=0x%p triple=0x%p pos=(%.3f %.3f %.3f)\n",
                    i, reinterpret_cast<void*>(c.viewtarget), reinterpret_cast<void*>(c.triple),
                    c.value.x, c.value.y, c.value.z);
    }
    return candidates.empty() ? 2 : 0;
}

int run_rawscan(HANDLE process, int argc, wchar_t** argv) {
    Vec3 center{158.0f, 121.0f, 256.0f};
    float tolerance = 1.0f;
    if (argc >= 5) {
        center.x = parse_float(argv[2], center.x);
        center.y = parse_float(argv[3], center.y);
        center.z = parse_float(argv[4], center.z);
    }
    if (argc >= 6) tolerance = parse_float(argv[5], tolerance);

    std::vector<std::pair<uintptr_t, Vec3>> results;
    uintptr_t address = 0x10000;
    const uintptr_t max_address = 0x00007FFFFFFFFFFF;
    while (address < max_address && results.size() < 256) {
        MEMORY_BASIC_INFORMATION mbi{};
        SIZE_T query = VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi));
        if (query == 0) break;
        const auto base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const auto size = static_cast<size_t>(mbi.RegionSize);
        if (mbi.State == MEM_COMMIT && looks_readable_table_page(mbi.Protect) && size >= 12 &&
            size <= 64ull * 1024ull * 1024ull) {
            std::vector<unsigned char> buffer(size);
            SIZE_T bytes_read = 0;
            if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(base), buffer.data(),
                                  buffer.size(), &bytes_read)) {
                for (size_t i = 0; i + 12 <= bytes_read && results.size() < 256; i += 4) {
                    Vec3 value{};
                    std::memcpy(&value, buffer.data() + i, sizeof(value));
                    if (!finite_reasonable(value.x) || !finite_reasonable(value.y) ||
                        !finite_reasonable(value.z)) {
                        continue;
                    }
                    if (std::fabs(value.x - center.x) <= tolerance &&
                        std::fabs(value.y - center.y) <= tolerance &&
                        std::fabs(value.z - center.z) <= tolerance) {
                        results.push_back({base + i, value});
                    }
                }
            }
        }
        const uintptr_t next = base + size;
        if (next <= address) break;
        address = next;
    }

    std::printf("rawscan center=(%.3f %.3f %.3f) tolerance=%.3f candidates=%zu\n",
                center.x, center.y, center.z, tolerance, results.size());
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        std::printf("[%03zu] addr=0x%p pos=(%.3f %.3f %.3f)\n", i,
                    reinterpret_cast<void*>(r.first), r.second.x, r.second.y, r.second.z);
    }
    return results.empty() ? 2 : 0;
}

int run_glscan(HANDLE process, DWORD pid) {
    const auto candidates = scan_for_gl_tables(process, pid, 64);
    std::printf("glscan candidates=%zu\n", candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        std::printf("[%02zu] table=0x%p useProgram=0x%p getUniformLocation=0x%p "
                    "uniform3f=0x%p uniform3fv=0x%p uniformMatrix4fv=0x%p\n",
                    i, reinterpret_cast<void*>(c.address),
                    reinterpret_cast<void*>(c.use_program),
                    reinterpret_cast<void*>(c.get_uniform_location),
                    reinterpret_cast<void*>(c.uniform3f),
                    reinterpret_cast<void*>(c.uniform3fv),
                    reinterpret_cast<void*>(c.uniform_matrix4fv));
    }
    return candidates.empty() ? 2 : 0;
}

int run_camchain(HANDLE process, int argc, wchar_t** argv) {
    Vec3 center{158.112f, 122.599f, 265.118f};
    float tolerance = 1.0f;
    if (argc >= 5) {
        center.x = parse_float(argv[2], center.x);
        center.y = parse_float(argv[3], center.y);
        center.z = parse_float(argv[4], center.z);
    }
    if (argc >= 6) tolerance = parse_float(argv[5], tolerance);

    const auto candidates = scan_for_camera_chains(process, center, tolerance, 32);
    std::printf("camchain center=(%.3f %.3f %.3f) tolerance=%.3f candidates=%zu\n",
                center.x, center.y, center.z, tolerance, candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        std::printf("[%02zu] cameraUpdate=0x%p pos+2FC=(%.3f %.3f %.3f) "
                    "viewTarget=0x%p vt+54=(%.3f %.3f %.3f)\n",
                    i, reinterpret_cast<void*>(c.camera_update), c.camera_value.x,
                    c.camera_value.y, c.camera_value.z, reinterpret_cast<void*>(c.viewtarget),
                    c.viewtarget_value.x, c.viewtarget_value.y, c.viewtarget_value.z);
    }
    return candidates.empty() ? 2 : 0;
}

int run_test(HANDLE process, int argc, wchar_t** argv) {
    uintptr_t viewtarget = 0;
    Vec3 center{158.0f, 122.0f, 256.0f};
    float tolerance = 25.0f;
    Vec3 delta{5.0f, 0.0f, 0.0f};
    double seconds = 2.0;
    double hz = 120.0;

    if (argc >= 4 && wcscmp(argv[2], L"--viewtarget") == 0) {
        viewtarget = parse_hex(argv[3]);
        if (argc >= 7) {
            delta.x = parse_float(argv[4], delta.x);
            delta.y = parse_float(argv[5], delta.y);
            delta.z = parse_float(argv[6], delta.z);
        }
        if (argc >= 8) seconds = parse_double(argv[7], seconds);
        if (argc >= 9) hz = parse_double(argv[8], hz);
    } else {
        if (argc >= 6) {
            center.x = parse_float(argv[2], center.x);
            center.y = parse_float(argv[3], center.y);
            center.z = parse_float(argv[4], center.z);
            tolerance = parse_float(argv[5], tolerance);
        }
        if (argc >= 9) {
            delta.x = parse_float(argv[6], delta.x);
            delta.y = parse_float(argv[7], delta.y);
            delta.z = parse_float(argv[8], delta.z);
        }
        if (argc >= 10) seconds = parse_double(argv[9], seconds);
        if (argc >= 11) hz = parse_double(argv[10], hz);

        auto candidates = scan_for_viewtargets(process, center, tolerance, 1);
        if (candidates.empty()) {
            std::puts("No candidate found. Run scan with a wider tolerance or pass --viewtarget.");
            return 2;
        }
        viewtarget = candidates.front().viewtarget;
    }

    if (viewtarget == 0) {
        std::puts("Invalid viewTarget address.");
        return 2;
    }

    const uintptr_t pos_address = viewtarget + 0x54;
    Vec3 original{};
    if (!read_vec3(process, pos_address, original)) {
        std::printf("Failed to read position at 0x%p\n", reinterpret_cast<void*>(pos_address));
        return 3;
    }
    Vec3 target{original.x + delta.x, original.y + delta.y, original.z + delta.z};
    std::printf("viewTarget=0x%p pos=0x%p original=(%.3f %.3f %.3f) target=(%.3f %.3f %.3f)\n",
                reinterpret_cast<void*>(viewtarget), reinterpret_cast<void*>(pos_address),
                original.x, original.y, original.z, target.x, target.y, target.z);

    const auto delay = std::chrono::duration<double>(1.0 / std::max(1.0, hz));
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    size_t writes = 0;
    while (std::chrono::steady_clock::now() < end) {
        if (!write_vec3(process, pos_address, target)) {
            std::puts("Write failed during test.");
            return 4;
        }
        ++writes;
        std::this_thread::sleep_for(delay);
    }

    write_vec3(process, pos_address, original);
    Vec3 after{};
    read_vec3(process, pos_address, after);
    std::printf("writes=%zu restored=(%.3f %.3f %.3f) current=(%.3f %.3f %.3f)\n",
                writes, original.x, original.y, original.z, after.x, after.y, after.z);
    return 0;
}

int run_testaddr(HANDLE process, int argc, wchar_t** argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    const uintptr_t address = parse_hex(argv[2]);
    Vec3 delta{5.0f, 0.0f, 0.0f};
    double seconds = 2.0;
    double hz = 120.0;
    if (argc >= 6) {
        delta.x = parse_float(argv[3], delta.x);
        delta.y = parse_float(argv[4], delta.y);
        delta.z = parse_float(argv[5], delta.z);
    }
    if (argc >= 7) seconds = parse_double(argv[6], seconds);
    if (argc >= 8) hz = parse_double(argv[7], hz);

    Vec3 original{};
    if (!read_vec3(process, address, original)) {
        std::printf("Failed to read vec3 at 0x%p\n", reinterpret_cast<void*>(address));
        return 1;
    }

    Vec3 target{original.x + delta.x, original.y + delta.y, original.z + delta.z};
    std::printf("addr=0x%p original=(%.3f %.3f %.3f) target=(%.3f %.3f %.3f)\n",
                reinterpret_cast<void*>(address), original.x, original.y, original.z, target.x,
                target.y, target.z);

    const auto delay = std::chrono::duration<double>(1.0 / std::max(1.0, hz));
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    size_t writes = 0;
    while (std::chrono::steady_clock::now() < end) {
        if (write_vec3(process, address, target)) ++writes;
        std::this_thread::sleep_for(delay);
    }

    write_vec3(process, address, original);
    Vec3 current{};
    read_vec3(process, address, current);
    std::printf("writes=%zu restored=(%.3f %.3f %.3f) current=(%.3f %.3f %.3f)\n", writes,
                original.x, original.y, original.z, current.x, current.y, current.z);
    return writes == 0 ? 2 : 0;
}

int run_spray(HANDLE process, int argc, wchar_t** argv) {
    Vec3 center{158.0f, 122.0f, 256.0f};
    float tolerance = 25.0f;
    Vec3 delta{50.0f, 0.0f, 0.0f};
    double seconds = 5.0;
    double hz = 120.0;

    if (argc >= 6) {
        center.x = parse_float(argv[2], center.x);
        center.y = parse_float(argv[3], center.y);
        center.z = parse_float(argv[4], center.z);
        tolerance = parse_float(argv[5], tolerance);
    }
    if (argc >= 9) {
        delta.x = parse_float(argv[6], delta.x);
        delta.y = parse_float(argv[7], delta.y);
        delta.z = parse_float(argv[8], delta.z);
    }
    if (argc >= 10) seconds = parse_double(argv[9], seconds);
    if (argc >= 11) hz = parse_double(argv[10], hz);

    auto candidates = scan_for_viewtargets(process, center, tolerance, 128);
    if (candidates.empty()) {
        std::puts("No candidates found.");
        return 2;
    }

    std::vector<std::pair<uintptr_t, Vec3>> targets;
    std::unordered_map<uintptr_t, Vec3> originals;
    for (const auto& c : candidates) {
        const uintptr_t pos_address = c.viewtarget + 0x54;
        if (originals.find(pos_address) != originals.end()) continue;
        originals[pos_address] = c.value;
        targets.push_back({pos_address, Vec3{c.value.x + delta.x, c.value.y + delta.y, c.value.z + delta.z}});
    }

    const auto delay = std::chrono::duration<double>(1.0 / std::max(1.0, hz));
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    size_t writes = 0;

    std::printf("spray center=(%.3f %.3f %.3f) tolerance=%.3f delta=(%.3f %.3f %.3f) seconds=%.2f hz=%.1f candidates=%zu\n",
                center.x, center.y, center.z, tolerance, delta.x, delta.y, delta.z, seconds, hz,
                targets.size());

    while (std::chrono::steady_clock::now() < end) {
        for (const auto& target : targets) {
            if (write_vec3(process, target.first, target.second)) {
                ++writes;
            }
        }
        std::this_thread::sleep_for(delay);
    }

    size_t restores = 0;
    for (const auto& it : originals) {
        if (write_vec3(process, it.first, it.second)) {
            ++restores;
        }
    }

    std::printf("uniquePositions=%zu writes=%zu restores=%zu\n", originals.size(), writes, restores);
    return writes == 0 ? 2 : 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    DWORD pid = find_process_id(L"HytaleClient.exe");
    if (pid == 0) {
        std::puts("HytaleClient.exe not found.");
        return 1;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
                                     PROCESS_VM_OPERATION,
                                 FALSE, pid);
    if (!process) {
        std::printf("OpenProcess failed: %lu\n", GetLastError());
        return 1;
    }

    std::printf("Attached to HytaleClient.exe pid=%lu\n", pid);
    int result = 1;
    if (_wcsicmp(argv[1], L"scan") == 0) {
        result = run_scan(process, argc, argv);
    } else if (_wcsicmp(argv[1], L"rawscan") == 0) {
        result = run_rawscan(process, argc, argv);
    } else if (_wcsicmp(argv[1], L"test") == 0) {
        result = run_test(process, argc, argv);
    } else if (_wcsicmp(argv[1], L"testaddr") == 0) {
        result = run_testaddr(process, argc, argv);
    } else if (_wcsicmp(argv[1], L"spray") == 0) {
        result = run_spray(process, argc, argv);
    } else if (_wcsicmp(argv[1], L"glscan") == 0) {
        result = run_glscan(process, pid);
    } else if (_wcsicmp(argv[1], L"camchain") == 0) {
        result = run_camchain(process, argc, argv);
    } else if (_wcsicmp(argv[1], L"rolltest") == 0) {
        result = run_rolltest(process, argc, argv);
    } else {
        print_usage();
    }

    CloseHandle(process);
    return result;
}
