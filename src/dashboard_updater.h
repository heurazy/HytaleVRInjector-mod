#pragma once

#include <windows.h>

#include <optional>

namespace hytalevr::dashboard_updater {

inline constexpr UINT kCheckCompleteMessage = WM_APP + 40;
inline constexpr UINT kDownloadCompleteMessage = WM_APP + 41;

std::optional<int> run_apply_mode();
void start_update_check(HWND owner);
bool handle_message(HWND owner, UINT message);
void shutdown();

} // namespace hytalevr::dashboard_updater
