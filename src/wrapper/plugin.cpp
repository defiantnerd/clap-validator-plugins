#include "wrapper/plugin.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <cvp/violations.h>

#include "wrapper/earlylog.h"
#include "wrapper/ext/audio_ports.h"
#include "wrapper/ext/params.h"

namespace cvp {

namespace {

// --- org.clap-validator.violations query extension ---------------------------

static_assert(sizeof(cvp_violation_entry_t{}.last_message) == ContractMonitor::kMessageCapacity,
              "query-extension message size must match the monitor's");

uint32_t violationsTotal(const clap_plugin_t* p) {
    return Plugin::from(p)->contract().total();
}

uint32_t violationsDistinct(const clap_plugin_t* p) {
    return Plugin::from(p)->contract().distinct();
}

bool violationsGet(const clap_plugin_t* p, uint32_t index, cvp_violation_entry_t* entry) {
    if (!entry)
        return false;
    return Plugin::from(p)->contract().getEntry(index, entry->code, &entry->count,
                                                entry->last_message);
}

void violationsClear(const clap_plugin_t* p) {
    Plugin::from(p)->contract().clear();
}

const cvp_plugin_violations_t kViolationsVtable = {
    .total = violationsTotal,
    .distinct = violationsDistinct,
    .get = violationsGet,
    .clear = violationsClear,
};

} // namespace

Plugin::Plugin(const clap_plugin_descriptor* descriptor, const clap_host* host) : _host(host) {
    _plugin.desc = descriptor;
    _plugin.plugin_data = this;
    _plugin.init = &sInit;
    _plugin.destroy = &sDestroy;
    _plugin.activate = &sActivate;
    _plugin.deactivate = &sDeactivate;
    _plugin.start_processing = &sStartProcessing;
    _plugin.stop_processing = &sStopProcessing;
    _plugin.reset = &sReset;
    _plugin.process = &sProcess;
    _plugin.get_extension = &sGetExtension;
    _plugin.on_main_thread = &sOnMainThread;
    _contract.setup(host, &_logBuffer);
}

void Plugin::provideExtension(const char* extensionId, const void* vtable,
                              void* providerIface) noexcept {
    _extensions.push_back({extensionId, vtable, providerIface});
}

void* Plugin::findProvider(const char* extensionId) const noexcept {
    for (const auto& record : _extensions)
        if (record.id == extensionId || std::strcmp(record.id, extensionId) == 0)
            return record.iface;
    return nullptr;
}

const void* Plugin::findVtable(const char* extensionId) const noexcept {
    for (const auto& record : _extensions)
        if (record.id == extensionId || std::strcmp(record.id, extensionId) == 0)
            return record.vtable;
    return nullptr;
}

void Plugin::logToHost(clap_log_severity severity, const char* message) noexcept {
    _logBuffer.append(severity, message);
    if (_hostLog && _hostLog->log)
        _hostLog->log(_host, severity, message);
    else
        std::fprintf(stderr, "[clap-validator-plugin] [%s] %s\n", severityTag(severity), message);
}

void Plugin::logLifecycle(const char* message) noexcept {
    if (_logLifecycle)
        logToHost(CLAP_LOG_INFO, message);
}

void Plugin::requestCallback() noexcept {
    _contract.noteCallbackRequested();
    if (_host && _host->request_callback)
        _host->request_callback(_host);
}

// Captures everything the audio-thread contract checks compare against while
// the plugin is active: frame bounds, the declared port layout (P05) and the
// valid param ids (P08). Runs on the main thread inside activate, before any
// process() can happen — audio-thread reads are race-free.
void Plugin::captureActivateSnapshot(uint32_t minFrames, uint32_t maxFrames) noexcept {
    _contract.setFrameBounds(minFrames, maxFrames);

    std::vector<ContractMonitor::PortInfo> inputPorts;
    std::vector<ContractMonitor::PortInfo> outputPorts;
    if (auto* ports = extensionProvider<ext::AudioPortsProvider>(CLAP_EXT_AUDIO_PORTS)) {
        for (int direction = 0; direction < 2; ++direction) {
            const bool isInput = direction == 0;
            auto& layout = isInput ? inputPorts : outputPorts;
            const uint32_t count = ports->audioPortCount(isInput);
            for (uint32_t i = 0; i < count; ++i) {
                clap_audio_port_info info{};
                if (ports->audioPortInfo(i, isInput, &info))
                    layout.push_back({info.channel_count, info.flags});
                else
                    layout.push_back({0, 0});
            }
        }
    }
    _contract.setPortLayout(std::move(inputPorts), std::move(outputPorts));

    std::vector<ContractMonitor::ParamRecord> paramTable;
    if (auto* params = extensionProvider<ext::ParamsProvider>(CLAP_EXT_PARAMS)) {
        const uint32_t count = params->paramCount();
        for (uint32_t i = 0; i < count; ++i) {
            clap_param_info info{};
            if (params->paramInfo(i, &info))
                paramTable.push_back({info.id, info.flags, info.cookie});
        }
    }
    _contract.setParams(std::move(paramTable));
}

bool Plugin::sInit(const clap_plugin* p) {
    auto* self = from(p);
    CVP_ASSERT_MAIN_THREAD(self);
    if (self->_contract.isInitialized()) {
        self->_contract.report(Violation::L02, "init()", "called twice on the same instance");
        return true; // already initialized — don't run init logic again
    }
    self->_threads.onInit(self->_host, &self->_logBuffer, &self->_contract);
    if (self->_host)
        self->_hostLog =
            static_cast<const clap_host_log*>(self->_host->get_extension(self->_host, CLAP_EXT_LOG));
    self->_contract.onHostLog(self->_hostLog);
    earlylog::copyInto(self->_logBuffer);
    // Set before the virtual: get_extension() is legal from within init().
    self->_contract.setInitialized();
    self->logHostInfo();
    self->logLifecycle("lifecycle: init()");
    return self->init();
}

void Plugin::sDestroy(const clap_plugin* p) {
    auto* self = from(p);
    CVP_ASSERT_MAIN_THREAD(self);
    if (self->_contract.isActive())
        self->_contract.report(Violation::L06, "destroy()",
                               "called while the plugin is still active");
    self->_contract.logSummary();
    self->logLifecycle("lifecycle: destroy()");
    delete self;
}

bool Plugin::sActivate(const clap_plugin* p, double sampleRate, uint32_t minFrames,
                       uint32_t maxFrames) {
    auto* self = from(p);
    CVP_ASSERT_MAIN_THREAD(self);
    if (self->_contract.isActive()) {
        self->_contract.report(Violation::L03, "activate()", "called while already active");
        return false;
    }
    if (minFrames < 1 || maxFrames < minFrames) {
        char detail[96];
        std::snprintf(detail, sizeof(detail), "invalid frame bounds (min=%u, max=%u)", minFrames,
                      maxFrames);
        self->_contract.report(Violation::L13, "activate()", detail);
        minFrames = std::max<uint32_t>(minFrames, 1);
        maxFrames = std::max(maxFrames, minFrames);
    }
    if (self->_logLifecycle) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "lifecycle: activate(sr=%.1f, min=%u, max=%u)", sampleRate,
                      minFrames, maxFrames);
        self->logToHost(CLAP_LOG_INFO, buf);
    }
    self->_sampleRate = sampleRate;
    self->_contract.beginActivation();
    const bool ok = self->activate(sampleRate, minFrames, maxFrames);
    if (ok) {
        self->captureActivateSnapshot(minFrames, maxFrames);
        self->_contract.setActive(true);
    }
    self->_contract.endActivation();
    self->_firstProcessLogged = false;
    return ok;
}

void Plugin::sDeactivate(const clap_plugin* p) {
    auto* self = from(p);
    CVP_ASSERT_MAIN_THREAD(self);
    if (!self->_contract.isActive()) {
        self->_contract.report(Violation::L04, "deactivate()", "called while not active");
        return;
    }
    if (self->_contract.isProcessing()) {
        self->_contract.report(Violation::L05, "deactivate()",
                               "called while still processing (missing stop_processing())");
        self->stopProcessing();
        self->_contract.setProcessing(false);
    }
    self->logLifecycle("lifecycle: deactivate()");
    self->deactivate();
    self->_contract.setActive(false);
}

bool Plugin::sStartProcessing(const clap_plugin* p) {
    auto* self = from(p);
    CVP_ASSERT_AUDIO_THREAD(self);
    if (!self->_contract.isActive()) {
        self->_contract.report(Violation::L07, "start_processing()", "called while not active");
        return false;
    }
    if (self->_contract.isProcessing()) {
        self->_contract.report(Violation::L07, "start_processing()",
                               "called while already processing");
        return true;
    }
    self->logLifecycle("lifecycle: start_processing()");
    const bool ok = self->startProcessing();
    self->_contract.setProcessing(ok);
    self->_contract.invalidateSteadyTime();
    return ok;
}

void Plugin::sStopProcessing(const clap_plugin* p) {
    auto* self = from(p);
    CVP_ASSERT_AUDIO_THREAD(self);
    if (!self->_contract.isActive())
        self->_contract.report(Violation::L08, "stop_processing()", "called while not active");
    if (!self->_contract.isProcessing()) {
        self->_contract.report(Violation::L08, "stop_processing()", "called while not processing");
        return;
    }
    self->logLifecycle("lifecycle: stop_processing()");
    self->stopProcessing();
    self->_contract.setProcessing(false);
}

void Plugin::sReset(const clap_plugin* p) {
    auto* self = from(p);
    CVP_ASSERT_AUDIO_THREAD(self);
    if (!self->_contract.isActive())
        self->_contract.report(Violation::L10, "reset()", "called while not active");
    self->logLifecycle("lifecycle: reset()");
    // A reset may legally jump steady_time backwards (plugin.h): re-baseline.
    self->_contract.invalidateSteadyTime();
    self->reset();
}

// Data contracts on the clap_process struct (P01..P10 minus the flush check).
// All reports are throttled by the monitor, so a host that is persistently
// wrong does not flood the log from the audio thread.
bool Plugin::checkProcessContracts(const clap_process* process) noexcept {
    bool processable = true;
    char detail[160];

    if (process->frames_count < _contract.minFrames() ||
        process->frames_count > _contract.maxFrames()) {
        std::snprintf(detail, sizeof(detail), "frames_count=%u outside activate bounds [%u, %u]",
                      process->frames_count, _contract.minFrames(), _contract.maxFrames());
        _contract.report(Violation::P01, "process()", detail);
    }

    if (!_contract.checkSteadyTime(process->steady_time, process->frames_count)) {
        std::snprintf(detail, sizeof(detail),
                      "steady_time=%lld went backwards (must advance by at least frames_count)",
                      static_cast<long long>(process->steady_time));
        _contract.report(Violation::P02, "process()", detail);
    }

    const auto& inputPorts = _contract.inputPorts();
    const auto& outputPorts = _contract.outputPorts();
    if (process->audio_inputs_count != inputPorts.size() ||
        process->audio_outputs_count != outputPorts.size()) {
        std::snprintf(detail, sizeof(detail),
                      "buffer counts in=%u/out=%u do not match declared ports in=%zu/out=%zu",
                      process->audio_inputs_count, process->audio_outputs_count,
                      inputPorts.size(), outputPorts.size());
        _contract.report(Violation::P05, "process()", detail);
    } else {
        // Buffer format bookkeeping across all ports for P09/P10. A port
        // requires 32-bit unless it declared CLAP_AUDIO_PORT_SUPPORTS_64BITS;
        // if any port declared REQUIRES_COMMON_SAMPLE_SIZE, mixing 32/64
        // across the ports of this plugin is illegal.
        bool anyRequiresCommon = false;
        bool saw32 = false;
        bool saw64 = false;
        for (int direction = 0; direction < 2; ++direction) {
            const bool isInput = direction == 0;
            const auto& layout = isInput ? inputPorts : outputPorts;
            const auto* buffers = isInput ? process->audio_inputs : process->audio_outputs;
            const uint32_t count =
                isInput ? process->audio_inputs_count : process->audio_outputs_count;
            for (uint32_t i = 0; i < count; ++i) {
                const auto& buffer = buffers[i];
                const char* dirName = isInput ? "input" : "output";
                if (buffer.channel_count != layout[i].channels) {
                    std::snprintf(detail, sizeof(detail),
                                  "%s port %u has channel_count=%u, declared %u", dirName, i,
                                  buffer.channel_count, layout[i].channels);
                    _contract.report(Violation::P05, "process()", detail);
                }
                anyRequiresCommon |=
                    (layout[i].flags & CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE) != 0;
                const bool has64 = buffer.data64 != nullptr;
                const bool has32 = buffer.data32 != nullptr;
                if (!has32 && !has64) {
                    std::snprintf(detail, sizeof(detail),
                                  "%s port %u has neither data32 nor data64", dirName, i);
                    _contract.report(Violation::P09, "process()", detail);
                    processable = false;
                    continue;
                }
                if (has64 && !(layout[i].flags & CLAP_AUDIO_PORT_SUPPORTS_64BITS)) {
                    std::snprintf(detail, sizeof(detail),
                                  "%s port %u got a 64-bit buffer but did not declare "
                                  "CLAP_AUDIO_PORT_SUPPORTS_64BITS",
                                  dirName, i);
                    _contract.report(Violation::P09, "process()", detail);
                    // Without an accompanying 32-bit buffer the plugin has
                    // nothing it could legally read/write on this port.
                    if (!has32)
                        processable = false;
                }
                // Effective sample size of this buffer: 64 when the host set
                // data64 (a plugin supporting 64 bits must use it then).
                (has64 ? saw64 : saw32) = true;
            }
        }
        if (anyRequiresCommon && saw32 && saw64)
            _contract.report(Violation::P10, "process()",
                             "mixed 32/64-bit buffers although a port declared "
                             "CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE");
    }
    const clap_input_events* events = process->in_events;
    if (!events || !events->size || !events->get)
        return processable;
    const uint32_t count = events->size(events);
    uint32_t previousTime = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header* header = events->get(events, i);
        if (!header)
            continue;
        if (header->size < sizeof(clap_event_header)) {
            std::snprintf(detail, sizeof(detail), "event %u has header size %u (malformed)", i,
                          header->size);
            _contract.report(Violation::P06, "process()", detail);
            continue;
        }
        if (header->time < previousTime) {
            std::snprintf(detail, sizeof(detail),
                          "event %u at time %u arrives after an event at time %u (unsorted)", i,
                          header->time, previousTime);
            _contract.report(Violation::P03, "process()", detail);
        } else {
            previousTime = header->time;
        }
        if (header->time >= process->frames_count) {
            std::snprintf(detail, sizeof(detail),
                          "event %u at time %u is beyond the block (frames_count=%u)", i,
                          header->time, process->frames_count);
            _contract.report(Violation::P04, "process()", detail);
        }
    }
    checkParamEvents(events, process->transport);
    return processable;
}

// Param-event contracts (P08, P11..P14), shared by the process() pre-pass and
// the flush() wrapper. P14 (automation of a non-automatable param) only fires
// for events delivered while the transport is playing: the spec explicitly
// allows live user changes regardless of CLAP_PARAM_IS_AUTOMATABLE, and a
// playing transport is the one context that identifies automation playback.
void Plugin::checkParamEvents(const clap_input_events* events,
                              const clap_event_transport* transport) noexcept {
    if (!events || !events->size || !events->get || !_contract.hasParams())
        return;
    const bool playing = transport && (transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
    char detail[160];
    const uint32_t count = events->size(events);
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header* header = events->get(events, i);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
            header->size < sizeof(clap_event_header))
            continue; // malformed headers are P06, reported by the caller
        const bool isValue = header->type == CLAP_EVENT_PARAM_VALUE;
        const bool isMod = header->type == CLAP_EVENT_PARAM_MOD;
        if (!isValue && !isMod)
            continue;

        clap_id paramId;
        void* cookie;
        int32_t noteId;
        int16_t key, channel;
        if (isValue) {
            const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
            paramId = event->param_id;
            cookie = event->cookie;
            noteId = event->note_id;
            key = event->key;
            channel = event->channel;
        } else {
            const auto* event = reinterpret_cast<const clap_event_param_mod*>(header);
            paramId = event->param_id;
            cookie = event->cookie;
            noteId = event->note_id;
            key = event->key;
            channel = event->channel;
        }
        const auto* param = _contract.findParam(paramId);
        if (!param) {
            std::snprintf(detail, sizeof(detail), "event %u targets unknown param_id=%u", i,
                          paramId);
            _contract.report(Violation::P08, "process()/flush()", detail);
            continue;
        }
        // The host must pass back exactly the cookie from param_info, or null.
        if (cookie && cookie != param->cookie) {
            std::snprintf(detail, sizeof(detail),
                          "event %u for param_id=%u carries a wrong cookie", i, paramId);
            _contract.report(Violation::P11, "process()/flush()", detail);
        }
        if (isValue && (param->flags & CLAP_PARAM_IS_READONLY)) {
            std::snprintf(detail, sizeof(detail),
                          "event %u writes read-only param_id=%u", i, paramId);
            _contract.report(Violation::P12, "process()/flush()", detail);
            continue;
        }
        if (isMod) {
            if (!(param->flags & CLAP_PARAM_IS_MODULATABLE)) {
                std::snprintf(detail, sizeof(detail),
                              "event %u modulates param_id=%u, which is not modulatable", i,
                              paramId);
                _contract.report(Violation::P13, "process()/flush()", detail);
            } else if ((noteId >= 0 &&
                        !(param->flags & CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID)) ||
                       (key >= 0 && !(param->flags & CLAP_PARAM_IS_MODULATABLE_PER_KEY)) ||
                       (channel >= 0 &&
                        !(param->flags & CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL))) {
                std::snprintf(detail, sizeof(detail),
                              "event %u modulates param_id=%u per note/key/channel without the "
                              "matching per-* capability",
                              i, paramId);
                _contract.report(Violation::P13, "process()/flush()", detail);
            }
            continue;
        }
        if (playing && !(param->flags & CLAP_PARAM_IS_AUTOMATABLE)) {
            std::snprintf(detail, sizeof(detail),
                          "event %u automates non-automatable param_id=%u (delivered while the "
                          "transport is playing)",
                          i, paramId);
            _contract.report(Violation::P14, "process()", detail);
        }
    }
}

clap_process_status Plugin::sProcess(const clap_plugin* p, const clap_process* process) {
    auto* self = from(p);
    CVP_ASSERT_AUDIO_THREAD(self);
    if (!self->_contract.tryEnterProcess()) {
        self->_contract.report(Violation::L12, "process()",
                               "called concurrently with another process() call");
        return CLAP_PROCESS_ERROR;
    }
    struct ProcessGuard {
        ContractMonitor& monitor;
        ~ProcessGuard() { monitor.leaveProcess(); }
    } guard{self->_contract};

    if (!self->_contract.isActive() || !self->_contract.isProcessing()) {
        self->_contract.report(Violation::L09, "process()", "called while not active/processing");
        return CLAP_PROCESS_ERROR;
    }
    if (self->_logLifecycle && !self->_firstProcessLogged) {
        self->_firstProcessLogged = true;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "lifecycle: first process() (frames=%u)",
                      process->frames_count);
        self->logToHost(CLAP_LOG_INFO, buf);
    }
    if (!self->checkProcessContracts(process))
        return CLAP_PROCESS_ERROR; // a port had no usable buffer (P09)
    return self->process(process);
}

const void* Plugin::sGetExtension(const clap_plugin* p, const char* id) {
    // [thread-safe] per spec — no thread assertion here.
    auto* self = from(p);
    if (!self->_contract.isInitialized() && id &&
        std::strcmp(id, CVP_EXT_VIOLATIONS) != 0) {
        char detail[128];
        std::snprintf(detail, sizeof(detail), "get_extension(\"%s\") called before init()", id);
        self->_contract.report(Violation::L01, "get_extension()", detail);
    }
    if (id && std::strcmp(id, CVP_EXT_VIOLATIONS) == 0)
        return &kViolationsVtable;
    return self->findVtable(id);
}

void Plugin::sOnMainThread(const clap_plugin* p) {
    auto* self = from(p);
    CVP_ASSERT_MAIN_THREAD(self);
    if (!self->_contract.consumePendingCallbacks())
        self->_contract.report(Violation::L11, "on_main_thread()",
                               "called without a pending request_callback()");
    self->logLifecycle("lifecycle: on_main_thread()");
    self->onMainThread();
}

} // namespace cvp
