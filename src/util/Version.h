// Version information for the application.

#pragma once

namespace cosmo::util {

#ifndef COSMO_VERSION_MAJOR
#define COSMO_VERSION_MAJOR 1
#endif
#ifndef COSMO_VERSION_MINOR
#define COSMO_VERSION_MINOR 1
#endif
#ifndef COSMO_VERSION_PATCH
#define COSMO_VERSION_PATCH 0
#endif
#ifndef COSMO_VERSION_BUILD
#define COSMO_VERSION_BUILD 0
#endif

constexpr int kVersionMajor = COSMO_VERSION_MAJOR;
constexpr int kVersionMinor = COSMO_VERSION_MINOR;
constexpr int kVersionPatch = COSMO_VERSION_PATCH;
constexpr int kVersionBuild = COSMO_VERSION_BUILD;

// Returns the application description string.
constexpr const char* GetProgramDesc() {
    return "Algorithm Analysis Engine";
}

// Returns the full version string, e.g. "Version 1.1.0.0 Mar 27 2026".
[[nodiscard]] const char* GetVersion();

// Returns the abbreviated version string, e.g. "V1.1.0.0".
[[nodiscard]] const char* GetAbbrVersion();

}  // namespace cosmo::util
