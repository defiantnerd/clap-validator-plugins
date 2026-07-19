#pragma once

#include <cstdint>
#include <string>

namespace cvp::preset {

// Data shared between the Validator Preset plugin (clap.preset-load) and the
// preset-discovery provider: the internal preset table, the .cvpreset file
// format, and the user preset directory.

inline constexpr const char* kPresetPluginId = "org.clap-validator.preset";
inline constexpr const char* kFileExtension = "cvpreset"; // without the dot

struct PresetData {
    char name[64];
    double gain;  // dB
    double color; // 0..3
};

// Internal ("factory") presets, addressed by load_key with the PLUGIN
// location kind.
struct InternalPreset {
    const char* loadKey;
    const char* name;
    const char* description;
    double gain;
    double color;
};

const InternalPreset* internalPresets(uint32_t* count) noexcept;
const InternalPreset* findInternalPreset(const char* loadKey) noexcept;

// .cvpreset file format (plain text):
//   CVP-PRESET v1
//   name=<display name>
//   gain=<double, dB>
//   color=<double, 0..3>
bool parsePresetFile(const char* path, PresetData* out) noexcept;

// Per-user preset directory that the discovery provider declares as its FILE
// location (created on demand).
std::string presetDirectory();

// The directory in the form declared as the clap FILE location (the plain OS
// path, per the 1.2.10 header — see the note in the implementation about
// clap-validator's divergent leading-slash expectation on Windows).
std::string presetDirectoryLocation();

// Converts a received clap location string back to an OS path — accepts both
// the plain form and the leading-slash Windows form.
std::string locationToPath(const char* location);

// Creates the preset directory and, if absent, two sample .cvpreset files so
// hosts have real files to crawl. Idempotent; never overwrites user files.
// Returns false if the directory is not usable (e.g. sandboxed host) — the
// provider then skips declaring the FILE location.
bool ensureSamplePresetFiles();

} // namespace cvp::preset
