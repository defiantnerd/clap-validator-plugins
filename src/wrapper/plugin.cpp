#include "wrapper/plugin.h"

#include <cstdio>
#include <cstring>

namespace cvp {

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
        std::fprintf(stderr, "[clap-validator-plugin] %s\n", message);
}

void Plugin::logLifecycle(const char* message) noexcept {
    if (_logLifecycle)
        logToHost(CLAP_LOG_INFO, message);
}

bool Plugin::sInit(const clap_plugin* p) {
    auto* self = from(p);
    self->_threads.onInit(self->_host, &self->_logBuffer);
    if (self->_host)
        self->_hostLog =
            static_cast<const clap_host_log*>(self->_host->get_extension(self->_host, CLAP_EXT_LOG));
    self->logLifecycle("lifecycle: init()");
    return self->init();
}

void Plugin::sDestroy(const clap_plugin* p) {
    auto* self = from(p);
    CVP_ASSERT_MAIN_THREAD(self);
    if (self->_active)
        self->logToHost(CLAP_LOG_HOST_MISBEHAVING,
                        "destroy() called while the plugin is still active");
    self->logLifecycle("lifecycle: destroy()");
    delete self;
}

bool Plugin::sActivate(const clap_plugin* p, double sampleRate, uint32_t minFrames,
                       uint32_t maxFrames) {
    auto* self = from(p);
    CVP_ASSERT_MAIN_THREAD(self);
    if (self->_active) {
        self->logToHost(CLAP_LOG_HOST_MISBEHAVING, "activate() called while already active");
        return false;
    }
    if (self->_logLifecycle) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "lifecycle: activate(sr=%.1f, min=%u, max=%u)", sampleRate,
                      minFrames, maxFrames);
        self->logToHost(CLAP_LOG_INFO, buf);
    }
    self->_sampleRate = sampleRate;
    self->_active = self->activate(sampleRate, minFrames, maxFrames);
    self->_firstProcessLogged = false;
    return self->_active;
}

void Plugin::sDeactivate(const clap_plugin* p) {
    auto* self = from(p);
    CVP_ASSERT_MAIN_THREAD(self);
    if (!self->_active) {
        self->logToHost(CLAP_LOG_HOST_MISBEHAVING, "deactivate() called while not active");
        return;
    }
    self->logLifecycle("lifecycle: deactivate()");
    self->deactivate();
    self->_active = false;
}

bool Plugin::sStartProcessing(const clap_plugin* p) {
    auto* self = from(p);
    CVP_ASSERT_AUDIO_THREAD(self);
    if (!self->_active) {
        self->logToHost(CLAP_LOG_HOST_MISBEHAVING, "start_processing() called while not active");
        return false;
    }
    if (self->_processing) {
        self->logToHost(CLAP_LOG_HOST_MISBEHAVING, "start_processing() called while already processing");
        return true;
    }
    self->logLifecycle("lifecycle: start_processing()");
    self->_processing = self->startProcessing();
    return self->_processing;
}

void Plugin::sStopProcessing(const clap_plugin* p) {
    auto* self = from(p);
    CVP_ASSERT_AUDIO_THREAD(self);
    if (!self->_processing) {
        self->logToHost(CLAP_LOG_HOST_MISBEHAVING, "stop_processing() called while not processing");
        return;
    }
    self->logLifecycle("lifecycle: stop_processing()");
    self->stopProcessing();
    self->_processing = false;
}

void Plugin::sReset(const clap_plugin* p) {
    auto* self = from(p);
    CVP_ASSERT_AUDIO_THREAD(self);
    self->logLifecycle("lifecycle: reset()");
    self->reset();
}

clap_process_status Plugin::sProcess(const clap_plugin* p, const clap_process* process) {
    auto* self = from(p);
    CVP_ASSERT_AUDIO_THREAD(self);
    if (!self->_active || !self->_processing) {
        self->logToHost(CLAP_LOG_HOST_MISBEHAVING,
                        "process() called while not active/processing");
        return CLAP_PROCESS_ERROR;
    }
    if (self->_logLifecycle && !self->_firstProcessLogged) {
        self->_firstProcessLogged = true;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "lifecycle: first process() (frames=%u)",
                      process->frames_count);
        self->logToHost(CLAP_LOG_INFO, buf);
    }
    return self->process(process);
}

const void* Plugin::sGetExtension(const clap_plugin* p, const char* id) {
    // [thread-safe] per spec — no thread assertion here.
    return from(p)->findVtable(id);
}

void Plugin::sOnMainThread(const clap_plugin* p) {
    auto* self = from(p);
    CVP_ASSERT_MAIN_THREAD(self);
    self->logLifecycle("lifecycle: on_main_thread()");
    self->onMainThread();
}

} // namespace cvp
