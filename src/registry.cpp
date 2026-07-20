// The explicit table of every plugin shipped in this binary.
// Adding a plugin: create its directory under src/plugins/ and add one line here.

#include "registry.h"

#include <cstring>

#include "plugins/audioports_zero/audioports_zero.h"
#include "plugins/effect/effect.h"
#include "plugins/hostcheck/hostcheck.h"
#if defined(CVP_HAS_GUI) && CVP_HAS_GUI
#include "plugins/gui/gui_plugin.h"
#endif
#include "plugins/multiout_fx/multiout_fx.h"
#include "plugins/multiout_gen/multiout_gen.h"
#include "plugins/notefx/notefx.h"
#include "plugins/params_zoo/params_zoo.h"
#include "plugins/preset/preset_plugin.h"
#include "plugins/sidechain_synth/sidechain_synth.h"
#include "plugins/slow/slow.h"
#include "plugins/surround/surround.h"
#include "plugins/synth/synth.h"

namespace cvp {

namespace {

const PluginEntry kEntries[] = {
    {&EffectPlugin::descriptor, &EffectPlugin::create},
    {&NoteFxPlugin::descriptor, &NoteFxPlugin::create},
    {&SynthPlugin::descriptor, &SynthPlugin::create},
    {&SidechainSynthPlugin::descriptor, &SidechainSynthPlugin::create},
    {&MultiOutGenPlugin::descriptor, &MultiOutGenPlugin::create},
    {&MultiOutFxPlugin::descriptor, &MultiOutFxPlugin::create},
    {&AudioPortsZeroPlugin::descriptor, &AudioPortsZeroPlugin::create},
    {&SlowPlugin::descriptor, &SlowPlugin::create},
    {&HostCheckPlugin::descriptor, &HostCheckPlugin::create},
    {&ParamsZooPlugin::descriptor, &ParamsZooPlugin::create},
    {&PresetPlugin::descriptor, &PresetPlugin::create},
    {&SurroundPlugin::descriptor, &SurroundPlugin::create},
#if defined(CVP_HAS_GUI) && CVP_HAS_GUI
    {&GuiPlugin::descriptor, &GuiPlugin::create},
#endif
};

} // namespace

const PluginEntry* registryEntries(uint32_t* count) noexcept {
    *count = static_cast<uint32_t>(sizeof(kEntries) / sizeof(kEntries[0]));
    return kEntries;
}

const PluginEntry* registryFind(const char* pluginId) noexcept {
    if (!pluginId)
        return nullptr;
    for (const auto& entry : kEntries)
        if (std::strcmp(entry.descriptor->id, pluginId) == 0)
            return &entry;
    return nullptr;
}

} // namespace cvp
