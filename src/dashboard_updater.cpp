#include "dashboard_updater.h"

#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "dashboard_update_logic.h"

#ifndef HYTALEVR_DASHBOARD_VERSION
#define HYTALEVR_DASHBOARD_VERSION "0.0.0"
#endif

namespace hytalevr::dashboard_updater {
namespace {

constexpr wchar_t kApiHost[] = L"api.github.com";
constexpr wchar_t kLatestReleasePath[] =
    L"/repos/heurazy/HytaleVRInjector-mod/releases/latest";
constexpr wchar_t kUserAgent[] = L"HytaleVR-Dashboard-Updater";
constexpr uint64_t kMaximumReleaseBytes = 512ull * 1024ull * 1024ull;

struct ReleaseInfo {
    std::string tag;
    std::wstring asset_url;
    uint64_t asset_size = 0;
    std::string digest;
};

struct CheckResult {
    bool update_available = false;
    ReleaseInfo release;
    std::wstring error;
};

struct DownloadResult {
    bool success = false;
    std::wstring archive_path;
    std::wstring error;
};

std::thread g_worker;
std::atomic_bool g_cancel{false};
std::mutex g_result_mutex;
CheckResult g_check_result;
DownloadResult g_download_result;
std::atomic<HWND> g_owner{nullptr};

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(),
                            size) != size) {
        return {};
    }
    return result;
}

std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                         value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(),
                            size, nullptr, nullptr) != size) {
        return {};
    }
    return result;
}

std::wstring windows_error_message(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result =
        length != 0 && buffer ? std::wstring(buffer, length) : L"Unknown error";
    if (buffer) LocalFree(buffer);
    while (!result.empty() &&
           (result.back() == L'\r' || result.back() == L'\n' ||
            result.back() == L' ')) {
        result.pop_back();
    }
    return result;
}

std::wstring current_executable_path() {
    std::wstring path(32768, L'\0');
    const DWORD length =
        GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return path;
}

std::wstring quote_argument(const std::wstring& value) {
    if (value.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return value;
    }
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

bool make_directory_tree(const std::filesystem::path& path,
                         std::wstring& error) {
    std::error_code code;
    std::filesystem::create_directories(path, code);
    if (!code || std::filesystem::is_directory(path, code)) return true;
    error = L"Could not create " + path.wstring() + L": " +
            utf8_to_wide(code.message());
    return false;
}

std::filesystem::path updater_temp_directory() {
    std::wstring temp(32768, L'\0');
    const DWORD length =
        GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
    if (length == 0 || length >= temp.size()) return {};
    temp.resize(length);
    std::wostringstream name;
    name << L"HytaleVRUpdater\\" << GetCurrentProcessId() << L"-"
         << GetTickCount64();
    return std::filesystem::path(temp) / name.str();
}

bool crack_url(const std::wstring& url, std::wstring& host,
               std::wstring& path, INTERNET_PORT& port, bool& secure) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0,
                         &components)) {
        return false;
    }
    host.assign(components.lpszHostName, components.dwHostNameLength);
    path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength != 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    port = components.nPort;
    secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    return !host.empty() && !path.empty();
}

struct HttpRequest {
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;

    ~HttpRequest() {
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
    }
};

bool open_get_request(const std::wstring& url, HttpRequest& handles,
                      std::wstring& error) {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
    bool secure = false;
    if (!crack_url(url, host, path, port, secure)) {
        error = L"Invalid update URL.";
        return false;
    }

    handles.session = WinHttpOpen(
        kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!handles.session) {
        error = L"WinHTTP initialization failed: " +
                windows_error_message(GetLastError());
        return false;
    }
    WinHttpSetTimeouts(handles.session, 5000, 5000, 10000, 10000);
    handles.connection =
        WinHttpConnect(handles.session, host.c_str(), port, 0);
    if (!handles.connection) {
        error = L"Connection failed: " +
                windows_error_message(GetLastError());
        return false;
    }
    handles.request = WinHttpOpenRequest(
        handles.connection, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0);
    if (!handles.request) {
        error = L"Request creation failed: " +
                windows_error_message(GetLastError());
        return false;
    }
    constexpr wchar_t headers[] =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpAddRequestHeaders(
            handles.request, headers, static_cast<DWORD>(-1),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) ||
        !WinHttpSendRequest(handles.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(handles.request, nullptr)) {
        error = L"Request failed: " + windows_error_message(GetLastError());
        return false;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(
            handles.request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
            WINHTTP_NO_HEADER_INDEX) ||
        status != 200) {
        std::wostringstream message;
        message << L"GitHub returned HTTP " << status << L".";
        error = message.str();
        return false;
    }
    return true;
}

bool read_response(HINTERNET request, std::vector<uint8_t>& output,
                   size_t maximum_size, std::wstring& error) {
    std::array<uint8_t, 32 * 1024> buffer{};
    for (;;) {
        if (g_cancel.load(std::memory_order_acquire)) {
            error = L"Update check cancelled.";
            return false;
        }
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(),
                             static_cast<DWORD>(buffer.size()), &read)) {
            error = L"Download failed: " +
                    windows_error_message(GetLastError());
            return false;
        }
        if (read == 0) break;
        if (output.size() + read > maximum_size) {
            error = L"GitHub response exceeded the safety limit.";
            return false;
        }
        output.insert(output.end(), buffer.begin(), buffer.begin() + read);
    }
    return true;
}

bool fetch_latest_release(ReleaseInfo& release, std::wstring& error) {
    HttpRequest request;
    const std::wstring url =
        std::wstring(L"https://") + kApiHost + kLatestReleasePath;
    if (!open_get_request(url, request, error)) return false;

    std::vector<uint8_t> response;
    if (!read_response(request.request, response, 2 * 1024 * 1024, error)) {
        return false;
    }

    try {
        const nlohmann::json json =
            nlohmann::json::parse(response.begin(), response.end());
        release.tag = json.at("tag_name").get<std::string>();
        if (!dashboard_update::release_is_newer(
                HYTALEVR_DASHBOARD_VERSION, release.tag)) {
            return true;
        }

        for (const auto& asset : json.at("assets")) {
            const std::string name = asset.at("name").get<std::string>();
            if (!dashboard_update::is_windows_x64_zip(name)) continue;
            release.asset_url = utf8_to_wide(
                asset.at("browser_download_url").get<std::string>());
            release.asset_size = asset.value("size", uint64_t{0});
            if (asset.contains("digest") && asset["digest"].is_string()) {
                release.digest = asset["digest"].get<std::string>();
            }
            break;
        }
    } catch (const std::exception& exception) {
        error = L"Invalid GitHub release metadata: " +
                utf8_to_wide(exception.what());
        return false;
    }
    if (release.asset_url.empty()) {
        error = L"The new release does not contain a Windows x64 ZIP.";
        return false;
    }
    if (release.asset_size == 0 ||
        release.asset_size > kMaximumReleaseBytes) {
        error = L"The release archive has an invalid size.";
        return false;
    }
    return true;
}

bool hash_file_sha256(const std::filesystem::path& path,
                      std::string& digest, std::wstring& error) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<uint8_t> hash_object;
    std::array<uint8_t, 32> hash_value{};

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) {
        error = L"SHA-256 initialization failed.";
        return false;
    }
    DWORD object_size = 0;
    DWORD result_size = 0;
    status = BCryptGetProperty(
        algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
        &result_size, 0);
    if (status >= 0) {
        hash_object.resize(object_size);
        status = BCryptCreateHash(algorithm, &hash, hash_object.data(),
                                  object_size, nullptr, 0, 0);
    }

    std::ifstream input(path, std::ios::binary);
    std::array<char, 64 * 1024> buffer{};
    while (status >= 0 && input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            status = BCryptHashData(
                hash, reinterpret_cast<PUCHAR>(buffer.data()),
                static_cast<ULONG>(count), 0);
        }
    }
    if (!input.eof() && input.fail()) {
        error = L"Could not read the downloaded archive.";
        status = -1;
    }
    if (status >= 0) {
        status = BCryptFinishHash(hash, hash_value.data(),
                                  static_cast<ULONG>(hash_value.size()), 0);
    }
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) {
        if (error.empty()) error = L"SHA-256 verification failed.";
        return false;
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const uint8_t byte : hash_value) {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    digest = output.str();
    return true;
}

bool download_release(const ReleaseInfo& release,
                      std::wstring& archive_path, std::wstring& error) {
    const std::filesystem::path directory = updater_temp_directory();
    if (directory.empty() || !make_directory_tree(directory, error)) {
        if (error.empty()) error = L"Could not create the updater directory.";
        return false;
    }
    const std::filesystem::path archive = directory / L"update.zip";

    HttpRequest request;
    if (!open_get_request(release.asset_url, request, error)) return false;

    std::ofstream output(archive, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = L"Could not create the update archive.";
        return false;
    }
    uint64_t total = 0;
    std::array<uint8_t, 64 * 1024> buffer{};
    for (;;) {
        if (g_cancel.load(std::memory_order_acquire)) {
            error = L"Update download cancelled.";
            return false;
        }
        DWORD read = 0;
        if (!WinHttpReadData(request.request, buffer.data(),
                             static_cast<DWORD>(buffer.size()), &read)) {
            error = L"Update download failed: " +
                    windows_error_message(GetLastError());
            return false;
        }
        if (read == 0) break;
        total += read;
        if (total > kMaximumReleaseBytes ||
            total > release.asset_size) {
            error = L"The downloaded archive exceeded its expected size.";
            return false;
        }
        output.write(reinterpret_cast<const char*>(buffer.data()), read);
        if (!output) {
            error = L"Writing the update archive failed.";
            return false;
        }
    }
    output.close();
    if (total != release.asset_size) {
        error = L"The downloaded archive is incomplete.";
        return false;
    }

    std::ifstream signature(archive, std::ios::binary);
    std::array<unsigned char, 4> magic{};
    signature.read(reinterpret_cast<char*>(magic.data()), magic.size());
    if (signature.gcount() != static_cast<std::streamsize>(magic.size()) ||
        magic[0] != 'P' || magic[1] != 'K') {
        error = L"The downloaded file is not a ZIP archive.";
        return false;
    }

    const std::string expected_prefix = "sha256:";
    if (release.digest.starts_with(expected_prefix)) {
        std::string actual_digest;
        if (!hash_file_sha256(archive, actual_digest, error)) return false;
        const std::string expected =
            dashboard_update::ascii_lower(
                release.digest.substr(expected_prefix.size()));
        if (actual_digest != expected) {
            error = L"The update SHA-256 checksum does not match GitHub.";
            return false;
        }
    }
    archive_path = archive.wstring();
    return true;
}

bool start_process(const std::wstring& executable,
                   const std::vector<std::wstring>& arguments,
                   const std::wstring& working_directory,
                   PROCESS_INFORMATION& process,
                   HANDLE standard_output = nullptr) {
    std::wstring command = quote_argument(executable);
    for (const std::wstring& argument : arguments) {
        command.push_back(L' ');
        command += quote_argument(argument);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    BOOL inherit_handles = FALSE;
    if (standard_output) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = standard_output;
        startup.hStdError = standard_output;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        inherit_handles = TRUE;
    }
    return CreateProcessW(
               executable.c_str(), command.data(), nullptr, nullptr,
               inherit_handles, CREATE_NO_WINDOW, nullptr,
               working_directory.empty() ? nullptr : working_directory.c_str(),
               &startup, &process) != FALSE;
}

bool run_process_and_wait(const std::wstring& executable,
                          const std::vector<std::wstring>& arguments,
                          const std::wstring& working_directory,
                          const std::filesystem::path& output_path,
                          DWORD& exit_code, std::wstring& error) {
    HANDLE output = CreateFileW(
        output_path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = L"Could not create updater process output.";
        return false;
    }
    SetHandleInformation(output, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    PROCESS_INFORMATION process{};
    const bool started = start_process(
        executable, arguments, working_directory, process, output);
    CloseHandle(output);
    if (!started) {
        error = L"Could not launch " + executable + L": " +
                windows_error_message(GetLastError());
        return false;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    const bool queried =
        GetExitCodeProcess(process.hProcess, &exit_code) != FALSE;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (!queried) {
        error = L"Could not read updater process result.";
        return false;
    }
    return true;
}

bool validate_archive_listing(const std::filesystem::path& listing,
                              std::wstring& error) {
    std::ifstream input(listing, std::ios::binary);
    if (!input) {
        error = L"Could not inspect the update archive.";
        return false;
    }
    bool dashboard_found = false;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (!dashboard_update::archive_entry_is_safe(line)) {
            error = L"The update archive contains an unsafe path.";
            return false;
        }
        const std::string lower = dashboard_update::ascii_lower(line);
        const size_t separator = lower.find_last_of("/\\");
        const std::string_view name =
            separator == std::string::npos
                ? std::string_view(lower)
                : std::string_view(lower).substr(separator + 1);
        if (name == "hytale_camera_dashboard.exe") dashboard_found = true;
    }
    if (!dashboard_found) {
        error = L"The update archive does not contain the dashboard.";
        return false;
    }
    return true;
}

bool copy_update_tree(const std::filesystem::path& source_root,
                      const std::filesystem::path& destination_root,
                      std::wstring& error) {
    std::error_code code;
    for (std::filesystem::recursive_directory_iterator iterator(
             source_root, std::filesystem::directory_options::skip_permission_denied,
             code), end;
         iterator != end; iterator.increment(code)) {
        if (code) {
            error = L"Could not read extracted update files: " +
                    utf8_to_wide(code.message());
            return false;
        }
        const std::filesystem::path relative =
            std::filesystem::relative(iterator->path(), source_root, code);
        if (code) {
            error = L"Could not resolve an extracted update path.";
            return false;
        }
        const std::filesystem::path destination =
            destination_root / relative;
        if (iterator->is_directory(code)) {
            std::filesystem::create_directories(destination, code);
        } else if (iterator->is_regular_file(code)) {
            std::filesystem::create_directories(destination.parent_path(), code);
            if (!code) {
                std::filesystem::copy_file(
                    iterator->path(), destination,
                    std::filesystem::copy_options::overwrite_existing, code);
            }
        }
        if (code) {
            error = L"Could not install " + destination.wstring() + L": " +
                    utf8_to_wide(code.message());
            return false;
        }
    }
    return true;
}

int apply_update(const std::filesystem::path& archive,
                 const std::filesystem::path& install_directory,
                 const std::filesystem::path& launch_executable,
                 DWORD parent_pid) {
    if (parent_pid != 0) {
        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parent_pid);
        if (parent) {
            WaitForSingleObject(parent, 30000);
            CloseHandle(parent);
        }
    }

    std::wstring system_directory(32768, L'\0');
    const UINT system_length =
        GetSystemDirectoryW(system_directory.data(),
                            static_cast<UINT>(system_directory.size()));
    if (system_length == 0 || system_length >= system_directory.size()) {
        MessageBoxW(nullptr, L"Windows tar.exe could not be located.",
                    L"Hytale VR update failed", MB_ICONERROR | MB_OK);
        return 2;
    }
    system_directory.resize(system_length);
    const std::filesystem::path tar =
        std::filesystem::path(system_directory) / L"tar.exe";
    const std::filesystem::path working = archive.parent_path();
    const std::filesystem::path listing = working / L"archive-list.txt";
    const std::filesystem::path staging = working / L"staging";
    std::wstring error;
    if (!make_directory_tree(staging, error)) {
        MessageBoxW(nullptr, error.c_str(), L"Hytale VR update failed",
                    MB_ICONERROR | MB_OK);
        return 3;
    }

    DWORD exit_code = 0;
    if (!run_process_and_wait(
            tar.wstring(), {L"-tf", archive.wstring()}, working.wstring(),
            listing, exit_code, error) ||
        exit_code != 0 || !validate_archive_listing(listing, error)) {
        if (error.empty()) error = L"The update archive could not be inspected.";
        MessageBoxW(nullptr, error.c_str(), L"Hytale VR update failed",
                    MB_ICONERROR | MB_OK);
        return 4;
    }
    if (!run_process_and_wait(
            tar.wstring(),
            {L"-xf", archive.wstring(), L"-C", staging.wstring()},
            working.wstring(), working / L"extract-output.txt",
            exit_code, error) ||
        exit_code != 0) {
        if (error.empty()) error = L"The update archive could not be extracted.";
        MessageBoxW(nullptr, error.c_str(), L"Hytale VR update failed",
                    MB_ICONERROR | MB_OK);
        return 5;
    }

    std::filesystem::path source_root;
    std::error_code code;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(staging, code)) {
        if (!entry.is_regular_file(code)) continue;
        std::wstring name = entry.path().filename().wstring();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](wchar_t character) {
                           return static_cast<wchar_t>(std::towlower(character));
                       });
        if (name == L"hytale_camera_dashboard.exe") {
            if (!source_root.empty()) {
                error = L"The update archive contains multiple dashboards.";
                break;
            }
            source_root = entry.path().parent_path();
        }
    }
    if (source_root.empty() || !error.empty() ||
        !copy_update_tree(source_root, install_directory, error)) {
        if (error.empty()) error = L"The extracted dashboard could not be found.";
        MessageBoxW(nullptr, error.c_str(), L"Hytale VR update failed",
                    MB_ICONERROR | MB_OK);
        return 6;
    }

    PROCESS_INFORMATION launched{};
    if (!start_process(launch_executable.wstring(), {},
                       install_directory.wstring(), launched)) {
        error = L"The update was installed, but the dashboard could not restart.";
        MessageBoxW(nullptr, error.c_str(), L"Hytale VR update",
                    MB_ICONWARNING | MB_OK);
        return 7;
    }
    CloseHandle(launched.hThread);
    CloseHandle(launched.hProcess);
    std::filesystem::remove_all(staging, code);
    std::filesystem::remove(archive, code);
    std::filesystem::remove(listing, code);
    MoveFileExW(current_executable_path().c_str(), nullptr,
                MOVEFILE_DELAY_UNTIL_REBOOT);
    return 0;
}

bool launch_apply_helper(const std::wstring& archive_path,
                         std::wstring& error) {
    const std::filesystem::path current = current_executable_path();
    if (current.empty()) {
        error = L"Could not locate the running dashboard.";
        return false;
    }
    const std::filesystem::path helper =
        std::filesystem::path(archive_path).parent_path() /
        L"hytale_vr_update_helper.exe";
    std::error_code code;
    std::filesystem::copy_file(
        current, helper, std::filesystem::copy_options::overwrite_existing,
        code);
    if (code) {
        error = L"Could not create the update helper: " +
                utf8_to_wide(code.message());
        return false;
    }
    const std::filesystem::path openvr_source =
        current.parent_path() / L"openvr_api.dll";
    const std::filesystem::path openvr_helper =
        helper.parent_path() / L"openvr_api.dll";
    std::filesystem::copy_file(
        openvr_source, openvr_helper,
        std::filesystem::copy_options::overwrite_existing, code);
    if (code) {
        error = L"Could not prepare OpenVR for the update helper: " +
                utf8_to_wide(code.message());
        return false;
    }

    PROCESS_INFORMATION process{};
    const std::vector<std::wstring> arguments{
        L"--apply-update",
        archive_path,
        current.parent_path().wstring(),
        current.wstring(),
        std::to_wstring(GetCurrentProcessId()),
    };
    if (!start_process(helper.wstring(), arguments,
                       helper.parent_path().wstring(), process)) {
        error = L"Could not start the update helper: " +
                windows_error_message(GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

void join_worker() {
    if (g_worker.joinable()) g_worker.join();
}

void begin_download(const ReleaseInfo& release) {
    g_cancel.store(false, std::memory_order_release);
    g_worker = std::thread([release]() {
        DownloadResult result;
        result.success =
            download_release(release, result.archive_path, result.error);
        {
            std::lock_guard<std::mutex> lock(g_result_mutex);
            g_download_result = std::move(result);
        }
        const HWND owner = g_owner.load(std::memory_order_acquire);
        if (!g_cancel.load(std::memory_order_acquire) && owner) {
            PostMessageW(owner, kDownloadCompleteMessage, 0, 0);
        }
    });
}

} // namespace

std::optional<int> run_apply_mode() {
    int argument_count = 0;
    wchar_t** arguments =
        CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (!arguments) return std::nullopt;
    if (argument_count < 2 ||
        std::wstring_view(arguments[1]) != L"--apply-update") {
        LocalFree(arguments);
        return std::nullopt;
    }
    if (argument_count != 6) {
        LocalFree(arguments);
        return 1;
    }
    const std::filesystem::path archive(arguments[2]);
    const std::filesystem::path install_directory(arguments[3]);
    const std::filesystem::path launch_executable(arguments[4]);
    wchar_t* end = nullptr;
    const unsigned long parsed_pid = std::wcstoul(arguments[5], &end, 10);
    const bool valid_pid = end && *end == L'\0';
    LocalFree(arguments);
    if (!valid_pid) return 1;
    return apply_update(archive, install_directory, launch_executable,
                        static_cast<DWORD>(parsed_pid));
}

void start_update_check(HWND owner) {
    if (!owner || g_worker.joinable()) return;
    g_owner.store(owner, std::memory_order_release);
    g_cancel.store(false, std::memory_order_release);
    g_worker = std::thread([]() {
        CheckResult result;
        ReleaseInfo release;
        if (fetch_latest_release(release, result.error)) {
            result.update_available =
                dashboard_update::release_is_newer(
                    HYTALEVR_DASHBOARD_VERSION, release.tag);
            result.release = std::move(release);
        }
        {
            std::lock_guard<std::mutex> lock(g_result_mutex);
            g_check_result = std::move(result);
        }
        const HWND owner = g_owner.load(std::memory_order_acquire);
        if (!g_cancel.load(std::memory_order_acquire) && owner) {
            PostMessageW(owner, kCheckCompleteMessage, 0, 0);
        }
    });
}

bool handle_message(HWND owner, UINT message) {
    if (message == kCheckCompleteMessage) {
        join_worker();
        CheckResult result;
        {
            std::lock_guard<std::mutex> lock(g_result_mutex);
            result = std::move(g_check_result);
            g_check_result = {};
        }
        if (!result.update_available) return true;

        const std::wstring available = utf8_to_wide(result.release.tag);
        const std::wstring current =
            utf8_to_wide(HYTALEVR_DASHBOARD_VERSION);
        const std::wstring prompt =
            L"Hytale VR " + available + L" is available.\n\n"
            L"Installed version: v" + current + L"\n\n"
            L"Download and install it now? The dashboard will restart.";
        if (MessageBoxW(owner, prompt.c_str(), L"Hytale VR update",
                        MB_ICONINFORMATION | MB_YESNO |
                            MB_DEFBUTTON1) == IDYES) {
            begin_download(result.release);
        }
        return true;
    }
    if (message == kDownloadCompleteMessage) {
        join_worker();
        DownloadResult result;
        {
            std::lock_guard<std::mutex> lock(g_result_mutex);
            result = std::move(g_download_result);
            g_download_result = {};
        }
        if (!result.success) {
            const std::wstring message_text =
                L"The update could not be downloaded.\n\n" + result.error;
            MessageBoxW(owner, message_text.c_str(), L"Hytale VR update",
                        MB_ICONERROR | MB_OK);
            return true;
        }
        std::wstring error;
        if (!launch_apply_helper(result.archive_path, error)) {
            MessageBoxW(owner, error.c_str(), L"Hytale VR update",
                        MB_ICONERROR | MB_OK);
            return true;
        }
        PostMessageW(owner, WM_CLOSE, 0, 0);
        return true;
    }
    return false;
}

void shutdown() {
    g_cancel.store(true, std::memory_order_release);
    g_owner.store(nullptr, std::memory_order_release);
    join_worker();
}

} // namespace hytalevr::dashboard_updater
