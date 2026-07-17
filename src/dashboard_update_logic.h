#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace hytalevr::dashboard_update {

struct SemanticVersion {
    std::array<uint32_t, 4> components{};
    bool prerelease = false;
};

inline std::optional<SemanticVersion> parse_version(std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    if (!value.empty() && (value.front() == 'v' || value.front() == 'V')) {
        value.remove_prefix(1);
    }
    if (value.empty()) return std::nullopt;

    SemanticVersion parsed{};
    size_t component = 0;
    size_t cursor = 0;
    while (cursor < value.size() && component < parsed.components.size()) {
        if (std::isdigit(static_cast<unsigned char>(value[cursor])) == 0) {
            return std::nullopt;
        }
        uint64_t number = 0;
        while (cursor < value.size() &&
               std::isdigit(static_cast<unsigned char>(value[cursor])) != 0) {
            number = number * 10u +
                     static_cast<uint64_t>(value[cursor] - '0');
            if (number > (std::numeric_limits<uint32_t>::max)()) {
                return std::nullopt;
            }
            ++cursor;
        }
        parsed.components[component++] = static_cast<uint32_t>(number);
        if (cursor >= value.size()) break;
        if (value[cursor] == '.') {
            ++cursor;
            if (cursor >= value.size()) return std::nullopt;
            continue;
        }
        if (value[cursor] == '-' || value[cursor] == '+') {
            parsed.prerelease = value[cursor] == '-';
            ++cursor;
            break;
        }
        return std::nullopt;
    }
    if (component == 0 ||
        (component == parsed.components.size() &&
         cursor < value.size() && value[cursor] == '.')) {
        return std::nullopt;
    }
    return parsed;
}

inline int compare_versions(const SemanticVersion& left,
                            const SemanticVersion& right) {
    for (size_t index = 0; index < left.components.size(); ++index) {
        if (left.components[index] < right.components[index]) return -1;
        if (left.components[index] > right.components[index]) return 1;
    }
    if (left.prerelease != right.prerelease) {
        return left.prerelease ? -1 : 1;
    }
    return 0;
}

inline bool release_is_newer(std::string_view current,
                             std::string_view available) {
    const auto current_version = parse_version(current);
    const auto available_version = parse_version(available);
    if (!current_version || !available_version) return false;
    return compare_versions(*current_version, *available_version) < 0;
}

inline std::string ascii_lower(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

inline bool is_windows_x64_zip(std::string_view name) {
    const std::string lower = ascii_lower(std::string(name));
    return lower.size() > 4 &&
           lower.ends_with(".zip") &&
           lower.find("windows") != std::string::npos &&
           (lower.find("x64") != std::string::npos ||
            lower.find("win64") != std::string::npos);
}

inline bool archive_entry_is_safe(std::string_view entry) {
    if (entry.empty() || entry.front() == '/' || entry.front() == '\\') {
        return false;
    }
    if (entry.size() >= 2 &&
        std::isalpha(static_cast<unsigned char>(entry[0])) != 0 &&
        entry[1] == ':') {
        return false;
    }

    size_t start = 0;
    while (start <= entry.size()) {
        const size_t separator = entry.find_first_of("/\\", start);
        const size_t end =
            separator == std::string_view::npos ? entry.size() : separator;
        const std::string_view component = entry.substr(start, end - start);
        if (component == "..") return false;
        if (separator == std::string_view::npos) break;
        start = separator + 1;
    }
    return true;
}

} // namespace hytalevr::dashboard_update
