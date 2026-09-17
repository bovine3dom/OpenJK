#include "jo_import.h"

#include <iostream>

namespace {
int run(int argc, const std::filesystem::path* args) {
    if (argc != 4) {
        std::cerr << "Usage: import_jo <academy> <outcast> <profile>\n"
                     "Select game folders that contain base/assets*.pk3.\n"
                     "Select a profile folder outside both game folders.\n";
        return 2;
    }
    jo_import::Phase previous = jo_import::Phase::finalizing;
    auto result = jo_import::import_campaign(args[1], args[2], args[3],
        [&](const jo_import::Progress& progress) {
            if (progress.phase != previous) {
                previous = progress.phase;
                switch (progress.phase) {
                case jo_import::Phase::validating: std::cout << "Checking game files...\n"; break;
                case jo_import::Phase::indexing: std::cout << "Reading PK3 indexes...\n"; break;
                case jo_import::Phase::importing: std::cout << "Importing JO campaign assets...\n"; break;
                case jo_import::Phase::finalizing: std::cout << "Finalizing campaign archive...\n"; break;
                }
                std::cout.flush();
            }
            return true;
        });
    if (!result) {
        std::cerr << "JO import failed: " << result.error->message << '\n';
        if (!result.error->path.empty()) std::cerr << "Path: " << result.error->path.u8string() << '\n';
        if (!result.error->asset.empty()) std::cerr << "Asset: " << result.error->asset << '\n';
        return 1;
    }
    std::cout << (result.reused ? "Using cached archive: " : "Created archive: ") << result.output.u8string() << '\n';
    if (!result.warning.empty()) std::cerr << result.warning << '\n';
    return 0;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    try {
        std::filesystem::path args[4];
        if (argc == 4) for (int i = 0; i < 4; ++i) args[i] = argv[i];
        return run(argc, args);
    } catch (const std::exception& e) {
        std::cerr << "Cannot start JO import: " << e.what() << '\n';
        return 1;
    }
}
