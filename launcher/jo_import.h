#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace jo_import {

// Increase this version when the output format or conversion rules change.
inline constexpr unsigned format_version = 2;

enum class ErrorCode { invalid_source, invalid_profile, archive, invalid_data,
                       io, busy, cancelled, internal };

struct Error {
    ErrorCode code;
    std::string message;
    std::filesystem::path path;
    std::string asset;
};

enum class Phase { validating, indexing, importing, finalizing };

struct Progress {
    Phase phase;
    std::size_t completed = 0;
    std::size_t total = 0; // Zero means that the total is not known.
    std::string item;
};

// Called on the caller's thread. Return false to cancel before replacement.
using ProgressCallback = std::function<bool(const Progress&)>;

struct Result {
    std::filesystem::path output;
    bool reused = false;
    std::optional<Error> error;
    std::string warning;
    explicit operator bool() const { return !error.has_value(); }
};

inline constexpr std::uintmax_t minimum_free_space = std::uintmax_t{1} << 30;

struct SpaceEstimate {
    std::uintmax_t required = minimum_free_space;
    std::uintmax_t available = 0;
    std::optional<Error> error;
    explicit operator bool() const { return !error && available >= required; }
};

SpaceEstimate estimate_space(const std::filesystem::path& profile);

Result import_campaign(const std::filesystem::path& academy,
                       const std::filesystem::path& outcast,
                       const std::filesystem::path& profile,
                       const ProgressCallback& progress = {});

} // namespace jo_import
