#include <Arduino.h>
#include <ESPLifecycle.h>
#include <ESPWorker.h>

ESPWorker worker;
ESPLifecycle lifecycle;

void setup() {
    Serial.begin(115200);

    worker.init(ESPWorker::Config{});

    LifecycleConfig config{};
    config.worker = &worker;
    config.enableParallelInit = true;
    config.enableParallelDeinit = true;
    config.enableParallelReinit = true;
    config.onInitStarted = []() { Serial.println("init started"); };
    config.onReady = []() { Serial.println("running"); };
    config.onInitFailed = []() { Serial.println("init failed"); };

    lifecycle.configure(config);
    lifecycle.init({"core", "network"});

    lifecycle.addTo("core", "logger", []() { return true; }, []() { return true; }).parallelSafe();
    lifecycle.addTo("core", "storage", []() { return true; }, []() { return true; });
    lifecycle.addTo("network", "wifi", []() { return true; }, []() { return true; }).after("storage");

    if( !lifecycle.start() ){
        Serial.println("start failed");
        return;
    }

    JsonDocument snapshot = lifecycle.snapshotJson();
    serializeJson(snapshot, Serial);
    Serial.println();

    delay(1000);
    lifecycle.stop();
}

void loop() {
    delay(250);
}
