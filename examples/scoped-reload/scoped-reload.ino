#include <Arduino.h>
#include <ESPEventBus.h>
#include <ESPLifecycle.h>
#include <ESPWorker.h>

struct ReloadPayload {
    const char* names[3];
    uint8_t count;
};

enum : uint16_t {
    EVENT_RELOAD_NODES = 100,
};

ESPWorker worker;
ESPEventBus eventBus;
ESPLifecycle lifecycle;

void setup() {
    Serial.begin(115200);

    worker.init(ESPWorker::Config{});
    eventBus.init(EventBusConfig{});

    LifecycleConfig config{};
    config.worker = &worker;
    config.enableParallelInit = true;
    config.enableParallelDeinit = true;
    config.enableParallelReinit = true;
    config.onSnapshot = [](const LifecycleSnapshot& snapshot) {
        Serial.printf("state=%d active=%s completed=%u/%u\n",
                      static_cast<int>(snapshot.state),
                      snapshot.activeNode == nullptr ? "-" : snapshot.activeNode,
                      snapshot.completed,
                      snapshot.total);
    };

    lifecycle.configure(config);
    lifecycle.init({"core", "network", "services"});

    lifecycle.addTo("core", "logger", []() { return true; }, []() { return true; });
    lifecycle.addTo("network", "wifi", []() { return true; }, []() { return true; }).after("logger");
    lifecycle.addTo("services", "api", []() { return true; }, []() { return true; }).after("wifi");

    if( !lifecycle.startReloadListener(
            eventBus,
            EVENT_RELOAD_NODES,
            [](void* payload) -> std::vector<const char*> {
                std::vector<const char*> names;
                if( payload == nullptr ){
                    return names;
                }

                ReloadPayload* data = static_cast<ReloadPayload*>(payload);
                for( uint8_t i = 0; i < data->count; i++ ){
                    if( data->names[i] != nullptr ){
                        names.push_back(data->names[i]);
                    }
                }
                return names;
            }
        ) ){
        Serial.println("reload listener failed");
    }

    (void)lifecycle.build();
    (void)lifecycle.initialize();

    ReloadPayload payload{{"logger", nullptr, nullptr}, 1};
    (void)eventBus.post(EVENT_RELOAD_NODES, &payload);

    delay(1000);
    (void)lifecycle.deinitialize({"logger"});
}

void loop() {
    delay(500);
}
