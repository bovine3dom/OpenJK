#pragma once
#include "bootstrap.h"

namespace launcher_ui {
int run(const std::filesystem::path& profile = {}, const bootstrap::BootstrapConfig& config = {});
}
