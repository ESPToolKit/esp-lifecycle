#include <Arduino.h>
#include <ESPLifecycle.h>
#include <ESPWorker.h>

ESPWorker worker;
ESPLifecycle lifecycle;

bool initStorage() {
    delay(50);
    Serial.println("init storage");
    return true;
}

bool initLogger() {
    delay(50);
    Serial.println("init logger");
    return true;
}

bool initCache() {
    delay(25);
    Serial.println("init cache");
    return true;
}

bool initTelemetry() {
    delay(25);
    Serial.println("init telemetry");
    return true;
}

bool deinitSimple() {
    Serial.println("deinit step");
    return true;
}

void setup() {
    Serial.begin(115200);

    worker.init(ESPWorker::Config{});

    LifecycleConfig cfg{};
    cfg.worker = &worker;
    cfg.enableParallelInit = true;
    cfg.enableParallelDeinit = true;
    cfg.enableParallelReinit = true;
    cfg.dependencyReinitialization = true;

    lifecycle.configure(cfg);
    lifecycle.init({"core"});

    lifecycle.addTo("core", "storage", initStorage, deinitSimple);
    lifecycle.addTo("core", "logger", initLogger, deinitSimple);
    lifecycle.addTo("core", "cache", initCache, deinitSimple).after("storage").parallelSafe();
    lifecycle.addTo("core", "telemetry", initTelemetry, deinitSimple).after("storage");

    (void)lifecycle.build();
    (void)lifecycle.initialize();

    // Reinitialize storage should include dependent closure.
    (void)lifecycle.reinitialize({"storage"});
}

void loop() {
    delay(500);
}
