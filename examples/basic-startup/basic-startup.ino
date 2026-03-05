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
    config.onInitStarted = []() { Serial.println("init started"); };
    config.onReady = []() { Serial.println("running"); };
    config.onInitFailed = []() { Serial.println("init failed"); };

    lifecycle.configure(config);
    lifecycle.init({"core", "network"});

    lifecycle.addTo("core", "logger", []() { return true; }, []() { return true; });
    lifecycle.addTo("core", "storage", []() { return true; }, []() { return true; }).after("logger");
    lifecycle.addTo("network", "wifi", []() { return true; }, []() { return true; }).after("storage");

    LifecycleResult buildResult = lifecycle.build();
    if( !buildResult.ok ){
        Serial.println("build failed");
        return;
    }

    LifecycleResult initResult = lifecycle.initialize();
    if( !initResult.ok ){
        Serial.println("initialize failed");
        return;
    }

    delay(1000);
    (void)lifecycle.deinitialize();
}

void loop() {
    delay(250);
}
