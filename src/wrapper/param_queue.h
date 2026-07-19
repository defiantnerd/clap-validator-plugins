#pragma once

#include <clap/clap.h>

#include <mutex>
#include <vector>

namespace cvp {

// Thread-safe queue for parameter changes originating in a GUI. The GUI
// thread pushes; the plugin drains in process()/paramsFlush(), forwarding
// gesture and value events to the host's out_events.
class ParamEventQueue {
public:
    enum class Kind { GestureBegin, Value, GestureEnd };

    struct Item {
        Kind kind;
        clap_id paramId;
        double value;
    };

    void push(const Item& item) {
        std::lock_guard<std::mutex> lock(_mutex);
        _items.push_back(item);
    }

    // Drains all pending items: forwards them as CLAP events to `out` (if
    // non-null) and calls apply(paramId, value) for Value items.
    template <typename Apply>
    void drain(const clap_output_events* out, Apply apply) {
        std::vector<Item> items;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            items.swap(_items);
        }
        for (const auto& item : items) {
            if (item.kind == Kind::Value) {
                apply(item.paramId, item.value);
                if (out) {
                    clap_event_param_value event{};
                    event.header.size = sizeof(event);
                    event.header.time = 0;
                    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    event.header.type = CLAP_EVENT_PARAM_VALUE;
                    event.header.flags = 0;
                    event.param_id = item.paramId;
                    event.cookie = nullptr;
                    event.note_id = -1;
                    event.port_index = -1;
                    event.channel = -1;
                    event.key = -1;
                    event.value = item.value;
                    out->try_push(out, &event.header);
                }
            } else if (out) {
                clap_event_param_gesture event{};
                event.header.size = sizeof(event);
                event.header.time = 0;
                event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                event.header.type = item.kind == Kind::GestureBegin
                                        ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                        : CLAP_EVENT_PARAM_GESTURE_END;
                event.header.flags = 0;
                event.param_id = item.paramId;
                out->try_push(out, &event.header);
            }
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(_mutex);
        _items.clear();
    }

private:
    std::mutex _mutex;
    std::vector<Item> _items;
};

} // namespace cvp
