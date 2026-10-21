// The settings: RecompFrontend's stock General, Controls, Graphics and Sound tabs, with its default bindings.

#include <filesystem>

#include "recompui/config.h"
#include "util/file.h"
#include "pd.h"

void pd::init_config() {
    std::filesystem::path app_dir = recompui::file::get_app_folder_path();
    if (!app_dir.empty()) {
        std::filesystem::create_directories(app_dir);
    }

    recompui::config::GeneralTabOptions general_options{};
    general_options.has_rumble_strength = true;
    general_options.has_gyro_sensitivity = true;
    general_options.has_mouse_sensitivity = true;
    recompui::config::create_general_tab(general_options);
    recompui::config::create_controls_tab();
    recompui::config::create_graphics_tab();
    recompui::config::create_sound_tab();
    recompui::config::finalize();
}
