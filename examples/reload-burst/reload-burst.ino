#include <Arduino.h>
#include <ESPEventBus.h>
#include <ESPLifecycle.h>
#include <ESPWorker.h>

struct ReloadPayload {
    const char* names[4];
    uint8_t count;
};

enum : uint16_t {
    EVENT_RELOAD = 200,
};

ESPWorker worker;
ESPEventBus eventBus;
ESPLifecycle lifecycle;

bool initStep(const char* name) {
    Serial.printf("init %s\n", name);
    return true;
}

bool deinitStep(const char* name) {
    Serial.printf("deinit %s\n", name);
    return true;
}

void setup() {
    Serial.begin(115200);

    worker.init(ESPWorker::Config{});
    eventBus.init(EventBusConfig{});

    LifecycleConfig cfg{};
    cfg.worker = &worker;
    cfg.enableParallelReinit = true;

    lifecycle.configure(cfg);
    lifecycle.init({"core", "network", "services"});

    lifecycle.addTo("core", "logger", []() { return initStep("logger"); }, []() { return deinitStep("logger"); });
    lifecycle.addTo("network", "wifi", []() { return initStep("wifi"); }, []() { return deinitStep("wifi"); }).after("logger");
    lifecycle.addTo("services", "api", []() { return initStep("api"); }, []() { return deinitStep("api"); }).after("wifi");

    (void)lifecycle.build();
    (void)lifecycle.initialize();

    (void)lifecycle.startReloadListener(
        eventBus,
        EVENT_RELOAD,
        [](void* payload) -> std::vector<const char*> {
            std::vector<const char*> names;
            if( payload == nullptr ){
                return names;
            }

            ReloadPayload* p = static_cast<ReloadPayload*>(payload);
            for( uint8_t i = 0; i < p->count; i++ ){
                if( p->names[i] != nullptr ){
                    names.push_back(p->names[i]);
                }
            }
            return names;
        }
    );

    // Burst: should coalesce and deduplicate names.
    ReloadPayload p1{{"logger", "wifi", nullptr, nullptr}, 2};
    ReloadPayload p2{{"wifi", "api", nullptr, nullptr}, 2};
    ReloadPayload p3{{"logger", nullptr, nullptr, nullptr}, 1};

    (void)eventBus.post(EVENT_RELOAD, &p1);
    (void)eventBus.post(EVENT_RELOAD, &p2);
    (void)eventBus.post(EVENT_RELOAD, &p3);
}

void loop() {
    delay(500);
}
