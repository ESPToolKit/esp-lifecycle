#include <Arduino.h>
#include <ESPLifecycle.h>
#include <ESPWorker.h>

ESPWorker worker;
ESPLifecycle lifecycle;

bool initCore() {
    Serial.println("init core");
    return true;
}

bool initCloud() {
    Serial.println("init cloud");
    return true;
}

bool deinitCore() {
    Serial.println("deinit core");
    return true;
}

bool deinitCloud() {
    Serial.println("deinit cloud");
    return true;
}

bool cloudReady() {
    return millis() > 4000;
}

void waitForReady(TickType_t waitTicks) {
    (void)waitTicks;
    delay(250);
}

void setup() {
    Serial.begin(115200);

    worker.init(ESPWorker::Config{});

    LifecycleConfig config{};
    config.worker = &worker;
    config.onReady = []() { Serial.println("all sections ready"); };

    lifecycle.configure(config);
    lifecycle.init({"core", "cloud"});

    lifecycle.section("cloud")
        .mode(LifecycleSectionMode::Deferred)
        .readiness(cloudReady, waitForReady);

    lifecycle.addTo("core", "core-init", initCore, deinitCore);
    lifecycle.addTo("cloud", "cloud-sync", initCloud, deinitCloud).after("core-init");

    (void)lifecycle.build();
    (void)lifecycle.initialize();
}

void loop() {
    delay(500);
}
