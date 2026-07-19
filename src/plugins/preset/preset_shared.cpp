#include "plugins/preset/preset_shared.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace cvp::preset {

namespace {

const InternalPreset kInternalPresets[] = {
    {"internal:unity", "Unity", "Gain 0 dB, neutral color", 0.0, 0.0},
    {"internal:quiet", "Quiet", "Gain -12 dB, warm color", -12.0, 1.0},
    {"internal:loud", "Loud", "Gain +6 dB, bright color", 6.0, 2.0},
};

struct SampleFile {
    const char* fileName;
    const char* content;
};

const SampleFile kSampleFiles[] = {
    {"validator-sample-soft.cvpreset", "CVP-PRESET v1\nname=Sample Soft\ngain=-6.0\ncolor=3\n"},
    {"validator-sample-hot.cvpreset", "CVP-PRESET v1\nname=Sample Hot\ngain=12.0\ncolor=2\n"},
};

} // namespace

const InternalPreset* internalPresets(uint32_t* count) noexcept {
    *count = static_cast<uint32_t>(sizeof(kInternalPresets) / sizeof(kInternalPresets[0]));
    return kInternalPresets;
}

const InternalPreset* findInternalPreset(const char* loadKey) noexcept {
    if (!loadKey)
        return nullptr;
    for (const auto& preset : kInternalPresets)
        if (std::strcmp(preset.loadKey, loadKey) == 0)
            return &preset;
    return nullptr;
}

std::string presetDirectoryLocation() {
    // Per clap 1.2.10 (factory/preset-discovery.h) a FILE location is a path
    // that works with plain OS filesystem functions — on Windows that means
    // "C:\...". clap-validator (<= 0.3.2) instead demands a leading slash and
    // then uses the string verbatim, which cannot work on Windows; it rejects
    // this declaration (non-fatal for us) and skips file crawling there.
    return presetDirectory();
}

std::string locationToPath(const char* location) {
    if (!location)
        return {};
    std::string path = location;
#if defined(_WIN32)
    // Strip the clap-style leading slash before a drive letter: "/C:\..." .
    if (path.size() >= 3 && path[0] == '/' && path[2] == ':')
        path.erase(0, 1);
#endif
    return path;
}

bool parsePresetFile(const char* path, PresetData* out) noexcept {
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    std::string line;
    if (!std::getline(file, line))
        return false;
    // Tolerate trailing CR from Windows-authored files.
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    if (line != "CVP-PRESET v1")
        return false;

    std::memset(out, 0, sizeof(*out));
    std::snprintf(out->name, sizeof(out->name), "Unnamed");
    bool haveGain = false;

    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "name") {
            std::snprintf(out->name, sizeof(out->name), "%s", value.c_str());
        } else if (key == "gain") {
            out->gain = std::strtod(value.c_str(), nullptr);
            haveGain = true;
        } else if (key == "color") {
            out->color = std::strtod(value.c_str(), nullptr);
        }
        // Unknown keys are skipped: forward compatibility.
    }
    return haveGain;
}

std::string presetDirectory() {
#if defined(__APPLE__)
    // Not ~/Library/Audio/Presets: that directory is root-owned on some
    // systems; Application Support is reliably user-writable.
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") +
           "/Library/Application Support/clap-validator-plugin/presets";
#elif defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    return std::string(appData ? appData : ".") + "\\clap-validator-plugin\\presets";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        return std::string(xdg) + "/clap-validator-plugin/presets";
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.local/share/clap-validator-plugin/presets";
#endif
}

bool ensureSamplePresetFiles() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = presetDirectory();
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec))
        return false;
    for (const auto& sample : kSampleFiles) {
        const fs::path path = dir / sample.fileName;
        if (fs::exists(path, ec))
            continue;
        std::ofstream file(path);
        if (file.is_open())
            file << sample.content;
    }
    return true;
}

} // namespace cvp::preset
