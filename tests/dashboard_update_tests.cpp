#include <cstdlib>
#include <iostream>
#include <string>

#include "dashboard_update_logic.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace hytalevr::dashboard_update;

    require(release_is_newer("0.9.5", "v0.9.6"),
            "patch releases must update");
    require(release_is_newer("0.9.5", "v0.10.0"),
            "minor releases must compare numerically");
    require(release_is_newer("0.9.5", "v1.0.0"),
            "major releases must update");
    require(!release_is_newer("0.9.5", "v0.9.5"),
            "equal versions must not update");
    require(!release_is_newer("0.9.5", "v0.9.4"),
            "older releases must not update");
    require(is_windows_x64_zip(
                "HytaleVRInjector-mod-v0.9.5-windows-x64.zip"),
            "Windows x64 release asset must be accepted");
    require(!is_windows_x64_zip("HytaleVRInjector-mod-v0.9.5-linux-x64.zip"),
            "Linux release asset must be rejected");
    require(archive_entry_is_safe("assets/textures/hand.png"),
            "normal archive path must be accepted");
    require(!archive_entry_is_safe("../dashboard.exe"),
            "parent traversal must be rejected");
    require(!archive_entry_is_safe("C:/Windows/file.dll"),
            "absolute drive path must be rejected");

    std::cout << "dashboard update tests passed\n";
    return 0;
}
