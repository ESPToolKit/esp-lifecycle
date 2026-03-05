#include <Arduino.h>
#include <ESPEventBus.h>
#include <ESPLifecycle.h>
#include <ESPWorker.h>

enum : uint16_t {
    EVENT_RELOAD_SCOPE = 100,
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
    config.onSnapshot = [](const LifecycleSnapshot& snapshot) {
        Serial.printf("state=%d active=%s completed=%u/%u\n",
                      static_cast<int>(snapshot.state),
                      snapshot.activeNode == nullptr ? "-" : snapshot.activeNode,
                      snapshot.completed,
                      snapshot.total);
    };

    lifecycle.configure(config);
    lifecycle.init({"core", "network"});

    lifecycle.addTo("core", "logger", []() { return true; }, []() { return true; }).reloadScope(0x01);
    lifecycle.addTo("network", "wifi", []() { return true; }, []() { return true; }).after("logger").reloadScope(0x02);

    if( !lifecycle.startScopeListener(
            eventBus,
            EVENT_RELOAD_SCOPE,
            [](void* payload) -> uint32_t {
                if( payload == nullptr ){
                    return 0;
                }
                return *static_cast<uint32_t*>(payload);
            }
        ) ){
        Serial.println("scope listener failed");
    }

    (void)lifecycle.build();
    (void)lifecycle.initialize();

    uint32_t mask = 0x03;
    (void)eventBus.post(EVENT_RELOAD_SCOPE, &mask);
}

void loop() {
    delay(500);
}
