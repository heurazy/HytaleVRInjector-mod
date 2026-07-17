#include "vr_item_assets.h"

#include "../third_party/nlohmann/json.hpp"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <set>
#include <sstream>

namespace hytalevr {
namespace {

using json = nlohmann::json;

constexpr uint32_t kLocalHeaderSignature = 0x04034b50u;
constexpr uint32_t kCentralHeaderSignature = 0x02014b50u;
constexpr uint32_t kEndOfCentralDirectorySignature = 0x06054b50u;

uint16_t read_u16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(data[1] << 8);
}

uint32_t read_u32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

std::string normalize_zip_path(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    return path;
}

bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool read_exact(std::ifstream& file, uint64_t offset, void* output, size_t size) {
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file.good()) return false;
    file.read(static_cast<char*>(output), static_cast<std::streamsize>(size));
    return file.good() || file.gcount() == static_cast<std::streamsize>(size);
}

bool index_zip(const std::wstring& path,
               std::unordered_map<std::string, VrZipEntry>& entries,
               std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "could not open Assets.zip";
        return false;
    }
    file.seekg(0, std::ios::end);
    const uint64_t file_size = static_cast<uint64_t>(file.tellg());
    const uint64_t tail_size = (std::min<uint64_t>)(file_size, 65557u);
    std::vector<uint8_t> tail(static_cast<size_t>(tail_size));
    if (!read_exact(file, file_size - tail_size, tail.data(), tail.size())) {
        error = "could not read ZIP footer";
        return false;
    }

    size_t eocd_offset = std::string::npos;
    for (size_t index = tail.size() >= 22 ? tail.size() - 22 : 0;; --index) {
        if (read_u32(tail.data() + index) == kEndOfCentralDirectorySignature) {
            eocd_offset = index;
            break;
        }
        if (index == 0) break;
    }
    if (eocd_offset == std::string::npos || eocd_offset + 22 > tail.size()) {
        error = "ZIP footer was not found";
        return false;
    }

    const uint16_t entry_count = read_u16(tail.data() + eocd_offset + 10);
    const uint32_t central_size = read_u32(tail.data() + eocd_offset + 12);
    const uint32_t central_offset = read_u32(tail.data() + eocd_offset + 16);
    if (entry_count == 0xffffu || central_size == 0xffffffffu ||
        central_offset == 0xffffffffu) {
        error = "ZIP64 Assets.zip is not supported by this experimental loader";
        return false;
    }

    std::vector<uint8_t> central(central_size);
    if (!read_exact(file, central_offset, central.data(), central.size())) {
        error = "could not read ZIP directory";
        return false;
    }

    entries.clear();
    size_t cursor = 0;
    for (uint32_t entry_index = 0;
         entry_index < entry_count && cursor + 46 <= central.size();
         ++entry_index) {
        if (read_u32(central.data() + cursor) != kCentralHeaderSignature) {
            error = "invalid ZIP directory entry";
            return false;
        }
        const uint16_t method = read_u16(central.data() + cursor + 10);
        const uint32_t compressed_size = read_u32(central.data() + cursor + 20);
        const uint32_t uncompressed_size = read_u32(central.data() + cursor + 24);
        const uint16_t name_length = read_u16(central.data() + cursor + 28);
        const uint16_t extra_length = read_u16(central.data() + cursor + 30);
        const uint16_t comment_length = read_u16(central.data() + cursor + 32);
        const uint32_t local_offset = read_u32(central.data() + cursor + 42);
        const size_t total_size =
            46u + name_length + extra_length + comment_length;
        if (cursor + total_size > central.size()) {
            error = "truncated ZIP directory entry";
            return false;
        }
        std::string name(
            reinterpret_cast<const char*>(central.data() + cursor + 46),
            name_length);
        name = normalize_zip_path(std::move(name));
        entries[name] = VrZipEntry{
            local_offset, compressed_size, uncompressed_size, method};
        cursor += total_size;
    }
    if (entries.empty()) {
        error = "Assets.zip did not contain any entries";
        return false;
    }
    return true;
}

bool read_zip_entry(const std::wstring& zip_path,
                    const std::unordered_map<std::string, VrZipEntry>& entries,
                    const std::string& requested_path,
                    std::vector<uint8_t>& output,
                    std::string& error) {
    const std::string path = normalize_zip_path(requested_path);
    const auto found = entries.find(path);
    if (found == entries.end()) {
        error = "missing ZIP entry: " + path;
        return false;
    }
    const VrZipEntry& entry = found->second;
    if (entry.method != 0 || entry.compressed_size != entry.uncompressed_size) {
        error = "compressed ZIP entry is unsupported: " + path;
        return false;
    }

    std::ifstream file(zip_path, std::ios::binary);
    if (!file.is_open()) {
        error = "could not reopen Assets.zip";
        return false;
    }
    std::array<uint8_t, 30> header{};
    if (!read_exact(file, entry.local_header_offset, header.data(), header.size()) ||
        read_u32(header.data()) != kLocalHeaderSignature) {
        error = "invalid local ZIP header: " + path;
        return false;
    }
    const uint16_t name_length = read_u16(header.data() + 26);
    const uint16_t extra_length = read_u16(header.data() + 28);
    const uint64_t data_offset =
        entry.local_header_offset + header.size() + name_length + extra_length;
    if (entry.uncompressed_size >
        static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
        error = "ZIP entry is too large: " + path;
        return false;
    }
    output.resize(static_cast<size_t>(entry.uncompressed_size));
    if (!output.empty() &&
        !read_exact(file, data_offset, output.data(), output.size())) {
        error = "could not read ZIP entry: " + path;
        return false;
    }
    return true;
}

bool ensure_gdiplus() {
    static std::once_flag once;
    static bool ready = false;
    static ULONG_PTR token = 0;
    std::call_once(once, [&]() {
        Gdiplus::GdiplusStartupInput input{};
        ready = Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok;
    });
    return ready;
}

bool decode_png(const std::vector<uint8_t>& bytes,
                uint32_t& width,
                uint32_t& height,
                std::vector<uint8_t>& rgba) {
    if (bytes.empty() || !ensure_gdiplus()) return false;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) return false;
    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(destination, bytes.data(), bytes.size());
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK || !stream) {
        GlobalFree(memory);
        return false;
    }
    Gdiplus::Bitmap bitmap(stream);
    if (bitmap.GetLastStatus() != Gdiplus::Ok ||
        bitmap.GetWidth() == 0 || bitmap.GetHeight() == 0) {
        stream->Release();
        return false;
    }

    width = bitmap.GetWidth();
    height = bitmap.GetHeight();
    rgba.resize(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            Gdiplus::Color color{};
            if (bitmap.GetPixel(static_cast<INT>(x), static_cast<INT>(y), &color) !=
                Gdiplus::Ok) {
                stream->Release();
                return false;
            }
            const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
            rgba[offset + 0] = color.GetRed();
            rgba[offset + 1] = color.GetGreen();
            rgba[offset + 2] = color.GetBlue();
            rgba[offset + 3] = color.GetAlpha();
        }
    }
    stream->Release();
    return true;
}

struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Mat4d {
    double value[16]{};
};

Mat4d identity_matrix() {
    Mat4d result{};
    result.value[0] = result.value[5] = result.value[10] = result.value[15] = 1.0;
    return result;
}

Mat4d multiply(const Mat4d& a, const Mat4d& b) {
    Mat4d result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            double value = 0.0;
            for (int axis = 0; axis < 4; ++axis) {
                value += a.value[axis * 4 + row] *
                         b.value[column * 4 + axis];
            }
            result.value[column * 4 + row] = value;
        }
    }
    return result;
}

Mat4d node_matrix(const json& node) {
    const json position = node.value("position", json::object());
    const json orientation = node.value("orientation", json::object());
    const double x = orientation.value("x", 0.0);
    const double y = orientation.value("y", 0.0);
    const double z = orientation.value("z", 0.0);
    const double w = orientation.value("w", 1.0);
    const double length = std::sqrt(x * x + y * y + z * z + w * w);
    const double qx = length > 0.000001 ? x / length : 0.0;
    const double qy = length > 0.000001 ? y / length : 0.0;
    const double qz = length > 0.000001 ? z / length : 0.0;
    const double qw = length > 0.000001 ? w / length : 1.0;

    Mat4d result = identity_matrix();
    result.value[0] = 1.0 - 2.0 * (qy * qy + qz * qz);
    result.value[1] = 2.0 * (qx * qy + qz * qw);
    result.value[2] = 2.0 * (qx * qz - qy * qw);
    result.value[4] = 2.0 * (qx * qy - qz * qw);
    result.value[5] = 1.0 - 2.0 * (qx * qx + qz * qz);
    result.value[6] = 2.0 * (qy * qz + qx * qw);
    result.value[8] = 2.0 * (qx * qz + qy * qw);
    result.value[9] = 2.0 * (qy * qz - qx * qw);
    result.value[10] = 1.0 - 2.0 * (qx * qx + qy * qy);
    result.value[12] = position.value("x", 0.0);
    result.value[13] = position.value("y", 0.0);
    result.value[14] = position.value("z", 0.0);
    return result;
}

Vec3d transform_point(const Mat4d& matrix, const Vec3d& point) {
    return {
        matrix.value[0] * point.x + matrix.value[4] * point.y +
            matrix.value[8] * point.z + matrix.value[12],
        matrix.value[1] * point.x + matrix.value[5] * point.y +
            matrix.value[9] * point.z + matrix.value[13],
        matrix.value[2] * point.x + matrix.value[6] * point.y +
            matrix.value[10] * point.z + matrix.value[14]};
}

Vec3d cross(Vec3d a, Vec3d b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

Vec3d normalize(Vec3d value) {
    const double length =
        std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.000001) return {0.0, 1.0, 0.0};
    return {value.x / length, value.y / length, value.z / length};
}

struct FaceUv {
    bool valid = false;
    std::array<std::pair<float, float>, 4> uv{};
};

FaceUv face_uv(const json& texture_layout,
               const char* face_name,
               double face_width,
               double face_height,
               uint32_t texture_width,
               uint32_t texture_height) {
    FaceUv result{};
    if (!texture_layout.contains(face_name) ||
        !texture_layout[face_name].is_object() ||
        texture_width == 0 || texture_height == 0) {
        return result;
    }
    const json& face = texture_layout[face_name];
    const json offset = face.value("offset", json::object());
    const json mirror = face.value("mirror", json::object());
    const double ox = offset.value("x", 0.0);
    const double oy = offset.value("y", 0.0);
    const bool mirror_x = mirror.value("x", false);
    const bool mirror_y = mirror.value("y", false);
    int angle = face.value("angle", 0);
    angle = ((angle % 360) + 360) % 360;

    double uv_width = face_width;
    double uv_height = face_height;
    double mirror_sign_x = mirror_x ? -1.0 : 1.0;
    double mirror_sign_y = mirror_y ? -1.0 : 1.0;
    std::array<double, 4> rectangle{};
    if (angle == 90) {
        std::swap(uv_width, uv_height);
        std::swap(mirror_sign_x, mirror_sign_y);
        mirror_sign_x *= -1.0;
        rectangle = {
            ox,
            oy + uv_height * mirror_sign_y,
            ox + uv_width * mirror_sign_x,
            oy};
    } else if (angle == 180) {
        mirror_sign_x *= -1.0;
        mirror_sign_y *= -1.0;
        rectangle = {
            ox + uv_width * mirror_sign_x,
            oy + uv_height * mirror_sign_y,
            ox,
            oy};
    } else if (angle == 270) {
        std::swap(uv_width, uv_height);
        std::swap(mirror_sign_x, mirror_sign_y);
        mirror_sign_y *= -1.0;
        rectangle = {
            ox + uv_width * mirror_sign_x,
            oy,
            ox,
            oy + uv_height * mirror_sign_y};
    } else {
        rectangle = {
            ox,
            oy,
            ox + uv_width * mirror_sign_x,
            oy + uv_height * mirror_sign_y};
    }

    std::array<std::pair<double, double>, 4> blockbench_order{{
        {rectangle[0], rectangle[1]},
        {rectangle[2], rectangle[1]},
        {rectangle[0], rectangle[3]},
        {rectangle[2], rectangle[3]}}};
    for (int remaining = angle; remaining > 0; remaining -= 90) {
        const auto top_left = blockbench_order[0];
        blockbench_order[0] = blockbench_order[2];
        blockbench_order[2] = blockbench_order[3];
        blockbench_order[3] = blockbench_order[1];
        blockbench_order[1] = top_left;
    }

    // emit_face uses bottom-left, bottom-right, top-right, top-left.
    constexpr std::array<size_t, 4> kEmitOrder{2, 3, 1, 0};
    for (size_t index = 0; index < kEmitOrder.size(); ++index) {
        const auto& corner = blockbench_order[kEmitOrder[index]];
        result.uv[index] = {
            static_cast<float>(corner.first / texture_width),
            static_cast<float>(corner.second / texture_height)};
    }
    result.valid = true;
    return result;
}

void emit_triangle(std::vector<VrItemVertex>& vertices,
                   const Vec3d& a,
                   const Vec3d& b,
                   const Vec3d& c,
                   std::pair<float, float> uv_a,
                   std::pair<float, float> uv_b,
                   std::pair<float, float> uv_c) {
    const Vec3d ab{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3d ac{c.x - a.x, c.y - a.y, c.z - a.z};
    const Vec3d normal = normalize(cross(ab, ac));
    const auto append = [&](const Vec3d& point, std::pair<float, float> uv) {
        vertices.push_back({
            static_cast<float>(point.x),
            static_cast<float>(point.y),
            static_cast<float>(point.z),
            static_cast<float>(normal.x),
            static_cast<float>(normal.y),
            static_cast<float>(normal.z),
            uv.first,
            uv.second});
    };
    append(a, uv_a);
    append(b, uv_b);
    append(c, uv_c);
}

void emit_face(std::vector<VrItemVertex>& vertices,
               const Mat4d& transform,
               const std::array<Vec3d, 4>& local,
               const FaceUv& uv) {
    if (!uv.valid) return;
    std::array<Vec3d, 4> world{};
    for (size_t index = 0; index < world.size(); ++index) {
        world[index] = transform_point(transform, local[index]);
    }
    emit_triangle(vertices, world[0], world[1], world[2],
                  uv.uv[0], uv.uv[1], uv.uv[2]);
    emit_triangle(vertices, world[0], world[2], world[3],
                  uv.uv[0], uv.uv[2], uv.uv[3]);
}

void emit_box(const json& shape,
              const Mat4d& transform,
              uint32_t texture_width,
              uint32_t texture_height,
              std::vector<VrItemVertex>& vertices) {
    const json settings = shape.value("settings", json::object());
    const json size = settings.value("size", json::object());
    const json offset = shape.value("offset", json::object());
    const json stretch = shape.value("stretch", json::object());
    const double texture_sx = size.value("x", 0.0);
    const double texture_sy = size.value("y", 0.0);
    const double texture_sz = size.value("z", 0.0);
    const double sx = texture_sx * stretch.value("x", 1.0);
    const double sy = texture_sy * stretch.value("y", 1.0);
    const double sz = texture_sz * stretch.value("z", 1.0);
    const double ox = offset.value("x", 0.0);
    const double oy = offset.value("y", 0.0);
    const double oz = offset.value("z", 0.0);
    const double x0 = ox - sx * 0.5;
    const double x1 = ox + sx * 0.5;
    const double y0 = oy - sy * 0.5;
    const double y1 = oy + sy * 0.5;
    const double z0 = oz - sz * 0.5;
    const double z1 = oz + sz * 0.5;
    const json layout = shape.value("textureLayout", json::object());

    emit_face(vertices, transform,
              {{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}},
              face_uv(layout, "front", texture_sx, texture_sy,
                      texture_width, texture_height));
    emit_face(vertices, transform,
              {{{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}},
              face_uv(layout, "back", texture_sx, texture_sy,
                      texture_width, texture_height));
    emit_face(vertices, transform,
              {{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}},
              face_uv(layout, "left", texture_sz, texture_sy,
                      texture_width, texture_height));
    emit_face(vertices, transform,
              {{{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}}},
              face_uv(layout, "right", texture_sz, texture_sy,
                      texture_width, texture_height));
    emit_face(vertices, transform,
              {{{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}}},
              face_uv(layout, "top", texture_sx, texture_sz,
                      texture_width, texture_height));
    emit_face(vertices, transform,
              {{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}},
              face_uv(layout, "bottom", texture_sx, texture_sz,
                      texture_width, texture_height));
}

void emit_textured_cube(std::vector<VrItemVertex>& vertices) {
    constexpr double kHalfSize = 16.0;
    const double x0 = -kHalfSize;
    const double x1 = kHalfSize;
    const double y0 = -kHalfSize;
    const double y1 = kHalfSize;
    const double z0 = -kHalfSize;
    const double z1 = kHalfSize;
    FaceUv uv{};
    uv.valid = true;
    uv.uv = {{{0.0f, 1.0f}, {1.0f, 1.0f},
              {1.0f, 0.0f}, {0.0f, 0.0f}}};
    const Mat4d transform = identity_matrix();

    emit_face(vertices, transform,
              {{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}},
              uv);
    emit_face(vertices, transform,
              {{{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}},
              uv);
    emit_face(vertices, transform,
              {{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}},
              uv);
    emit_face(vertices, transform,
              {{{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}}},
              uv);
    emit_face(vertices, transform,
              {{{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}}},
              uv);
    emit_face(vertices, transform,
              {{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}},
              uv);
}

void emit_quad(const json& shape,
               const Mat4d& transform,
               uint32_t texture_width,
               uint32_t texture_height,
               std::vector<VrItemVertex>& vertices) {
    const json settings = shape.value("settings", json::object());
    const json size = settings.value("size", json::object());
    const json offset = shape.value("offset", json::object());
    const json stretch = shape.value("stretch", json::object());
    const double texture_sx = size.value("x", 0.0);
    const double texture_sy = size.value("y", 0.0);
    const double sx = texture_sx * stretch.value("x", 1.0);
    const double sy = texture_sy * stretch.value("y", 1.0);
    const double ox = offset.value("x", 0.0);
    const double oy = offset.value("y", 0.0);
    const double oz = offset.value("z", 0.0);
    const std::string normal = settings.value("normal", std::string("+Z"));
    const json layout = shape.value("textureLayout", json::object());
    const FaceUv uv =
        face_uv(layout, "front", texture_sx, texture_sy,
                texture_width, texture_height);
    std::array<Vec3d, 4> points{};
    if (normal == "+X" || normal == "-X") {
        points = {{{ox, oy - sy * 0.5, oz - sx * 0.5},
                   {ox, oy - sy * 0.5, oz + sx * 0.5},
                   {ox, oy + sy * 0.5, oz + sx * 0.5},
                   {ox, oy + sy * 0.5, oz - sx * 0.5}}};
    } else if (normal == "+Y" || normal == "-Y") {
        points = {{{ox - sx * 0.5, oy, oz - sy * 0.5},
                   {ox + sx * 0.5, oy, oz - sy * 0.5},
                   {ox + sx * 0.5, oy, oz + sy * 0.5},
                   {ox - sx * 0.5, oy, oz + sy * 0.5}}};
    } else {
        points = {{{ox - sx * 0.5, oy - sy * 0.5, oz},
                   {ox + sx * 0.5, oy - sy * 0.5, oz},
                   {ox + sx * 0.5, oy + sy * 0.5, oz},
                   {ox - sx * 0.5, oy + sy * 0.5, oz}}};
    }
    if (normal == "-X" || normal == "-Y" || normal == "-Z") {
        std::swap(points[1], points[3]);
    }
    emit_face(vertices, transform, points, uv);
}

void parse_nodes(const json& nodes,
                 const Mat4d& parent,
                 uint32_t texture_width,
                 uint32_t texture_height,
                 std::vector<VrItemVertex>& vertices,
                 VrItemAttachmentPoint* flame_anchor,
                 bool* needs_flame_effect,
                 bool top_level) {
    if (!nodes.is_array()) return;
    for (const json& node : nodes) {
        if (!node.is_object()) continue;
        const std::string name = node.value("name", std::string());
        const bool attachment_root =
            top_level && name.find("Attachment") != std::string::npos;
        const Mat4d world = attachment_root
            ? identity_matrix()
            : multiply(parent, node_matrix(node));
        const json shape = node.value("shape", json::object());
        if (name == "Flame" && flame_anchor && !flame_anchor->valid) {
            const Vec3d anchor = transform_point(world, {0.0, 0.0, 0.0});
            flame_anchor->valid = true;
            flame_anchor->x = static_cast<float>(anchor.x);
            flame_anchor->y = static_cast<float>(anchor.y);
            flame_anchor->z = static_cast<float>(anchor.z);
            if (needs_flame_effect) {
                *needs_flame_effect =
                    shape.value("type", std::string("none")) == "none";
            }
        }
        if (shape.value("visible", true)) {
            const std::string type = shape.value("type", std::string("none"));
            if (type == "box") {
                emit_box(shape, world, texture_width, texture_height, vertices);
            } else if (type == "quad") {
                emit_quad(shape, world, texture_width, texture_height, vertices);
            }
        }
        if (node.contains("children")) {
            parse_nodes(node["children"], world, texture_width, texture_height,
                        vertices, flame_anchor, needs_flame_effect, false);
        }
    }
}

bool parse_blockymodel(const std::vector<uint8_t>& bytes,
                       uint32_t texture_width,
                       uint32_t texture_height,
                       std::vector<VrItemVertex>& vertices,
                       VrItemAttachmentPoint* flame_anchor,
                       bool* needs_flame_effect,
                       std::string& error) {
    try {
        const json model = json::parse(bytes.begin(), bytes.end());
        vertices.clear();
        if (flame_anchor) *flame_anchor = {};
        if (needs_flame_effect) *needs_flame_effect = false;
        parse_nodes(model.value("nodes", json::array()), identity_matrix(),
                    texture_width, texture_height, vertices,
                    flame_anchor, needs_flame_effect, true);
        if (vertices.empty()) {
            error = "BlockyModel did not produce any triangles";
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = std::string("BlockyModel parse failed: ") + exception.what();
        return false;
    }
}

int definition_preference(const std::string& id,
                          const std::string& source_path) {
    int score = 100;
    if (id.rfind("Template_", 0) == 0) score -= 60;
    if (id.rfind("Debug_", 0) == 0 || id.rfind("_Debug", 0) == 0) score -= 50;
    if (source_path.find("/_Debug/") != std::string::npos) score -= 50;
    if (source_path.find("/Weapon/") != std::string::npos ||
        source_path.find("/Tool/") != std::string::npos) {
        score += 10;
    }
    return score;
}

std::string common_asset_path(std::string path) {
    path = normalize_zip_path(std::move(path));
    if (!path.empty() && path.rfind("Common/", 0) != 0) {
        path = "Common/" + path;
    }
    return path;
}

std::string first_weighted_texture(const json& value) {
    const auto texture_from_object = [](const json& object) {
        if (!object.is_object()) return std::string();
        if (object.contains("Texture") && object["Texture"].is_string()) {
            return object["Texture"].get<std::string>();
        }
        constexpr const char* kFaceNames[] = {
            "All", "Sides", "UpDown", "North", "South",
            "East", "West", "Up", "Down"};
        for (const char* name : kFaceNames) {
            if (object.contains(name) && object[name].is_string()) {
                return object[name].get<std::string>();
            }
        }
        return std::string();
    };
    if (value.is_array()) {
        for (const json& entry : value) {
            const std::string texture = texture_from_object(entry);
            if (!texture.empty()) return texture;
        }
    }
    return texture_from_object(value);
}

void update_definition_metadata(VrItemAssetDefinition& definition,
                                const std::vector<VrItemVertex>& vertices,
                                uint32_t texture_width,
                                uint32_t texture_height) {
    definition.expected_vertex_count = vertices.size();
    if (vertices.empty() || texture_width == 0 || texture_height == 0) return;

    float min_u = (std::numeric_limits<float>::max)();
    float min_v = (std::numeric_limits<float>::max)();
    float max_u = (std::numeric_limits<float>::lowest)();
    float max_v = (std::numeric_limits<float>::lowest)();
    for (const VrItemVertex& vertex : vertices) {
        min_u = (std::min)(min_u, vertex.u);
        min_v = (std::min)(min_v, vertex.v);
        max_u = (std::max)(max_u, vertex.u);
        max_v = (std::max)(max_v, vertex.v);
    }
    definition.uv_min_x = static_cast<int>(
        std::floor(min_u * static_cast<float>(texture_width) + 0.001f));
    definition.uv_min_y = static_cast<int>(
        std::floor(min_v * static_cast<float>(texture_height) + 0.001f));
    definition.uv_max_x = static_cast<int>(
        std::ceil(max_u * static_cast<float>(texture_width) - 0.001f));
    definition.uv_max_y = static_cast<int>(
        std::ceil(max_v * static_cast<float>(texture_height) - 0.001f));
}

} // namespace

bool load_vr_item_asset_catalog(const std::wstring& assets_zip,
                                VrItemAssetCatalog& catalog,
                                std::string& error) {
    catalog = {};
    catalog.zip_path = assets_zip;
    if (!index_zip(assets_zip, catalog.zip_entries, error)) return false;

    struct RawItem {
        std::string id;
        std::string source_path;
        std::string parent;
        std::string model;
        std::string texture;
        std::string block_draw_type;
        std::string block_model;
        std::string block_model_texture;
        std::string block_texture;
    };
    std::unordered_map<std::string, RawItem> raw_items;
    for (const auto& [path, entry] : catalog.zip_entries) {
        (void)entry;
        if (path.rfind("Server/Item/Items/", 0) != 0 ||
            !ends_with(path, ".json")) {
            continue;
        }
        std::vector<uint8_t> bytes;
        std::string read_error;
        if (!read_zip_entry(assets_zip, catalog.zip_entries, path, bytes,
                            read_error)) {
            continue;
        }
        try {
            const json item = json::parse(bytes.begin(), bytes.end());
            const size_t slash = path.find_last_of('/');
            const size_t dot = path.find_last_of('.');
            const std::string id = path.substr(
                slash == std::string::npos ? 0 : slash + 1,
                dot == std::string::npos ? std::string::npos :
                    dot - (slash == std::string::npos ? 0 : slash + 1));
            RawItem raw{};
            raw.id = id;
            raw.source_path = path;
            if (item.contains("Parent") && item["Parent"].is_string()) {
                raw.parent = item["Parent"].get<std::string>();
            }
            if (item.contains("Model") && item["Model"].is_string()) {
                raw.model = item["Model"].get<std::string>();
            }
            if (item.contains("Texture") && item["Texture"].is_string()) {
                raw.texture = item["Texture"].get<std::string>();
            }
            const json block_type =
                item.value("BlockType", json::object());
            if (block_type.is_object()) {
                if (block_type.contains("DrawType") &&
                    block_type["DrawType"].is_string()) {
                    raw.block_draw_type =
                        block_type["DrawType"].get<std::string>();
                }
                if (block_type.contains("CustomModel") &&
                    block_type["CustomModel"].is_string()) {
                    raw.block_model =
                        block_type["CustomModel"].get<std::string>();
                }
                if (block_type.contains("CustomModelTexture")) {
                    raw.block_model_texture = first_weighted_texture(
                        block_type["CustomModelTexture"]);
                }
                if (block_type.contains("Textures")) {
                    raw.block_texture =
                        first_weighted_texture(block_type["Textures"]);
                }
            }
            raw_items[id] = std::move(raw);
        } catch (...) {
        }
    }

    struct ResolvedItem {
        bool attempted = false;
        bool valid = false;
        std::string model;
        std::string texture;
        std::string block_draw_type;
        std::string block_model;
        std::string block_model_texture;
        std::string block_texture;
    };
    std::unordered_map<std::string, ResolvedItem> resolved_items;
    std::function<bool(const std::string&, std::set<std::string>&,
                       ResolvedItem&)> resolve_item;
    resolve_item = [&](const std::string& id,
                       std::set<std::string>& visiting,
                       ResolvedItem& output) {
        const auto cached = resolved_items.find(id);
        if (cached != resolved_items.end() && cached->second.attempted) {
            output = cached->second;
            return cached->second.valid;
        }
        const auto found = raw_items.find(id);
        if (found == raw_items.end() || !visiting.insert(id).second) {
            return false;
        }
        const RawItem& raw = found->second;
        ResolvedItem inherited{};
        if (!raw.parent.empty()) {
            resolve_item(raw.parent, visiting, inherited);
        }
        visiting.erase(id);

        ResolvedItem resolved{};
        resolved.attempted = true;
        resolved.model = raw.model.empty() ? inherited.model : raw.model;
        resolved.texture =
            raw.texture.empty() ? inherited.texture : raw.texture;
        resolved.block_draw_type = raw.block_draw_type.empty()
            ? inherited.block_draw_type
            : raw.block_draw_type;
        resolved.block_model = raw.block_model.empty()
            ? inherited.block_model
            : raw.block_model;
        resolved.block_model_texture = raw.block_model_texture.empty()
            ? inherited.block_model_texture
            : raw.block_model_texture;
        resolved.block_texture = raw.block_texture.empty()
            ? inherited.block_texture
            : raw.block_texture;
        resolved.valid =
            (!resolved.model.empty() && !resolved.texture.empty()) ||
            (!resolved.block_model.empty() &&
             !resolved.block_model_texture.empty()) ||
            !resolved.block_texture.empty();
        resolved_items[id] = std::move(resolved);
        output = resolved_items[id];
        return output.valid;
    };

    std::vector<VrItemAssetDefinition> preliminary_definitions;
    std::unordered_map<std::string, size_t> definition_by_visual;
    for (const auto& [id, raw] : raw_items) {
        std::set<std::string> visiting;
        ResolvedItem resolved{};
        if (!resolve_item(id, visiting, resolved)) continue;

        std::string model;
        std::string texture;
        bool generated_cube = false;
        if (!resolved.model.empty() && !resolved.texture.empty()) {
            model = resolved.model;
            texture = resolved.texture;
        } else if (!resolved.block_model.empty() &&
                   !resolved.block_model_texture.empty()) {
            model = resolved.block_model;
            texture = resolved.block_model_texture;
        } else if (!resolved.block_texture.empty()) {
            texture = resolved.block_texture;
            generated_cube = true;
        }

        model = common_asset_path(std::move(model));
        texture = common_asset_path(std::move(texture));
        if (!ends_with(texture, ".png") ||
            (!generated_cube && !ends_with(model, ".blockymodel"))) {
            continue;
        }
        if ((!generated_cube &&
             catalog.zip_entries.find(model) == catalog.zip_entries.end()) ||
            catalog.zip_entries.find(texture) == catalog.zip_entries.end()) {
            continue;
        }

        VrItemAssetDefinition definition{};
        definition.id = id;
        definition.model_path = model;
        definition.texture_path = texture;
        definition.preference = definition_preference(id, raw.source_path);
        definition.generated_cube = generated_cube;
        const std::string visual_key =
            (generated_cube ? "cube|" : "model|") + model + "|" + texture;
        const auto existing = definition_by_visual.find(visual_key);
        if (existing == definition_by_visual.end()) {
            definition_by_visual[visual_key] =
                preliminary_definitions.size();
            preliminary_definitions.push_back(std::move(definition));
        } else if (
            definition.preference >
            preliminary_definitions[existing->second].preference) {
            preliminary_definitions[existing->second] =
                std::move(definition);
        }
    }

    std::unordered_map<std::string, size_t> candidate_by_texture;
    std::set<std::pair<uint32_t, uint32_t>> dimensions;
    for (VrItemAssetDefinition definition : preliminary_definitions) {
        size_t candidate_index = 0;
        const auto existing_candidate =
            candidate_by_texture.find(definition.texture_path);
        if (existing_candidate == candidate_by_texture.end()) {
            std::vector<uint8_t> png;
            std::string read_error;
            if (!read_zip_entry(
                    assets_zip, catalog.zip_entries,
                    definition.texture_path, png, read_error)) {
                continue;
            }
            VrItemTextureCandidate candidate{};
            if (!decode_png(
                    png, candidate.width, candidate.height,
                    candidate.rgba)) {
                continue;
            }
            candidate.fingerprint = vr_item_texture_fingerprint(
                candidate.rgba.data(), candidate.width, candidate.height,
                static_cast<size_t>(candidate.width) * 4, false);
            candidate.flipped_fingerprint = vr_item_texture_fingerprint(
                candidate.rgba.data(), candidate.width, candidate.height,
                static_cast<size_t>(candidate.width) * 4, true);
            candidate.alpha_fingerprint = vr_item_alpha_fingerprint(
                candidate.rgba.data(), candidate.width, candidate.height,
                static_cast<size_t>(candidate.width) * 4, false);
            candidate.flipped_alpha_fingerprint = vr_item_alpha_fingerprint(
                candidate.rgba.data(), candidate.width, candidate.height,
                static_cast<size_t>(candidate.width) * 4, true);
            candidate_index = catalog.texture_candidates.size();
            candidate_by_texture[definition.texture_path] = candidate_index;
            catalog.texture_candidates.push_back(std::move(candidate));
        } else {
            candidate_index = existing_candidate->second;
        }
        VrItemTextureCandidate& candidate =
            catalog.texture_candidates[candidate_index];

        std::vector<VrItemVertex> vertices;
        if (definition.generated_cube) {
            emit_textured_cube(vertices);
        } else {
            std::vector<uint8_t> model;
            std::string model_error;
            if (!read_zip_entry(
                    assets_zip, catalog.zip_entries,
                    definition.model_path, model, model_error) ||
                !parse_blockymodel(
                    model, candidate.width, candidate.height,
                    vertices, nullptr, nullptr, model_error)) {
                continue;
            }
        }
        update_definition_metadata(
            definition, vertices, candidate.width, candidate.height);
        const size_t definition_index = catalog.definitions.size();
        catalog.definitions.push_back(std::move(definition));
        candidate.definition_indices.push_back(definition_index);
        dimensions.emplace(candidate.width, candidate.height);
    }

    catalog.texture_candidates.erase(
        std::remove_if(
            catalog.texture_candidates.begin(),
            catalog.texture_candidates.end(),
            [](const VrItemTextureCandidate& candidate) {
                return candidate.definition_indices.empty();
            }),
        catalog.texture_candidates.end());
    catalog.texture_dimensions.assign(dimensions.begin(), dimensions.end());
    if (catalog.texture_candidates.empty()) {
        error = "no item textures could be decoded from Assets.zip";
        return false;
    }
    return true;
}

bool load_vr_item_render_asset(const VrItemAssetCatalog& catalog,
                               size_t definition_index,
                               VrItemRenderAsset& asset,
                               std::string& error) {
    if (definition_index >= catalog.definitions.size()) {
        error = "item definition index is out of range";
        return false;
    }
    asset = {};
    asset.definition = catalog.definitions[definition_index];

    std::vector<uint8_t> png;
    if (!read_zip_entry(catalog.zip_path, catalog.zip_entries,
                        asset.definition.texture_path, png, error) ||
        !decode_png(png, asset.texture_width, asset.texture_height,
                    asset.texture_rgba)) {
        if (error.empty()) error = "item texture could not be decoded";
        return false;
    }

    if (asset.definition.generated_cube) {
        emit_textured_cube(asset.vertices);
        return !asset.vertices.empty();
    }

    std::vector<uint8_t> model;
    if (!read_zip_entry(catalog.zip_path, catalog.zip_entries,
                        asset.definition.model_path, model, error)) {
        return false;
    }
    return parse_blockymodel(
        model, asset.texture_width, asset.texture_height, asset.vertices,
        &asset.flame_anchor, &asset.needs_flame_effect, error);
}

bool load_vr_asset_texture(const VrItemAssetCatalog& catalog,
                           const std::string& asset_path,
                           uint32_t& width,
                           uint32_t& height,
                           std::vector<uint8_t>& rgba,
                           std::string& error) {
    width = 0;
    height = 0;
    rgba.clear();
    std::vector<uint8_t> png;
    if (!read_zip_entry(
            catalog.zip_path, catalog.zip_entries,
            normalize_zip_path(asset_path), png, error) ||
        !decode_png(png, width, height, rgba)) {
        if (error.empty()) error = "asset texture could not be decoded";
        return false;
    }
    return width != 0 && height != 0 && !rgba.empty();
}

bool build_vr_item_fallback_cube(const std::vector<uint8_t>& rgba,
                                 uint32_t width,
                                 uint32_t height,
                                 VrItemRenderAsset& asset,
                                 std::string& error) {
    if (width == 0 || height == 0 ||
        rgba.size() < static_cast<size_t>(width) * height * 4) {
        error = "fallback item texture is invalid";
        return false;
    }
    asset = {};
    asset.definition.id = "Captured_Item";
    asset.definition.generated_cube = true;
    asset.texture_width = width;
    asset.texture_height = height;
    asset.texture_rgba.assign(
        rgba.begin(),
        rgba.begin() + static_cast<size_t>(width) * height * 4);
    emit_textured_cube(asset.vertices);
    if (asset.vertices.empty()) {
        error = "fallback item cube did not produce any triangles";
        return false;
    }
    update_definition_metadata(
        asset.definition, asset.vertices, width, height);
    return true;
}

double vr_item_texture_difference(const uint8_t* atlas_rgba,
                                  size_t atlas_stride,
                                  bool flip_rows,
                                  const VrItemTextureCandidate& candidate) {
    if (!atlas_rgba || candidate.rgba.empty() ||
        candidate.width == 0 || candidate.height == 0 ||
        atlas_stride < static_cast<size_t>(candidate.width) * 4) {
        return (std::numeric_limits<double>::infinity)();
    }
    uint64_t difference = 0;
    uint64_t samples = 0;
    for (uint32_t y = 0; y < candidate.height; ++y) {
        const uint32_t source_y = flip_rows ? candidate.height - 1 - y : y;
        const uint8_t* atlas_row = atlas_rgba + static_cast<size_t>(y) * atlas_stride;
        const uint8_t* candidate_row =
            candidate.rgba.data() +
            static_cast<size_t>(source_y) * candidate.width * 4;
        for (uint32_t x = 0; x < candidate.width; ++x) {
            const uint8_t* expected = candidate_row + static_cast<size_t>(x) * 4;
            const uint8_t* actual = atlas_row + static_cast<size_t>(x) * 4;
            if (expected[3] == 0 && actual[3] == 0) continue;
            for (int channel = 0; channel < 4; ++channel) {
                difference += static_cast<uint64_t>(
                    std::abs(static_cast<int>(expected[channel]) -
                             static_cast<int>(actual[channel])));
            }
            samples += 4;
        }
    }
    if (samples == 0) return (std::numeric_limits<double>::infinity)();
    return static_cast<double>(difference) / static_cast<double>(samples);
}

uint64_t vr_item_texture_fingerprint(const uint8_t* rgba,
                                     uint32_t width,
                                     uint32_t height,
                                     size_t stride,
                                     bool flip_rows) {
    if (!rgba || width == 0 || height == 0 ||
        stride < static_cast<size_t>(width) * 4) {
        return 0;
    }
    constexpr uint64_t kOffset = 1469598103934665603ull;
    constexpr uint64_t kPrime = 1099511628211ull;
    uint64_t hash = kOffset;
    const auto append = [&](uint8_t value, uint64_t& current) {
        current ^= value;
        current *= kPrime;
    };
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t source_y = flip_rows ? height - 1 - y : y;
        const uint8_t* row = rgba + static_cast<size_t>(source_y) * stride;
        for (uint32_t x = 0; x < width * 4; ++x) {
            append(row[x], hash);
        }
    }
    append(static_cast<uint8_t>(width & 0xff), hash);
    append(static_cast<uint8_t>((width >> 8) & 0xff), hash);
    append(static_cast<uint8_t>(height & 0xff), hash);
    append(static_cast<uint8_t>((height >> 8) & 0xff), hash);
    return hash;
}

uint64_t vr_item_alpha_fingerprint(const uint8_t* rgba,
                                   uint32_t width,
                                   uint32_t height,
                                   size_t stride,
                                   bool flip_rows) {
    if (!rgba || width == 0 || height == 0 ||
        stride < static_cast<size_t>(width) * 4) {
        return 0;
    }
    constexpr uint64_t kOffset = 1469598103934665603ull;
    constexpr uint64_t kPrime = 1099511628211ull;
    uint64_t hash = kOffset;
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t source_y = flip_rows ? height - 1 - y : y;
        const uint8_t* row = rgba + static_cast<size_t>(source_y) * stride;
        for (uint32_t x = 0; x < width; ++x) {
            hash ^= row[static_cast<size_t>(x) * 4 + 3];
            hash *= kPrime;
        }
    }
    hash ^= width;
    hash *= kPrime;
    hash ^= height;
    hash *= kPrime;
    return hash;
}

} // namespace hytalevr
