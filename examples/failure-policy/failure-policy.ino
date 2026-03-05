#include <Arduino.h>
#include <ESPLifecycle.h>
#include <ESPWorker.h>

ESPWorker worker;
ESPLifecycle lifecycle;

static uint8_t flakyAttempts = 0;

bool initStable() {
    Serial.println("init stable");
    return true;
}

bool initFlaky() {
    flakyAttempts++;
    Serial.printf("init flaky attempt %u\n", flakyAttempts);
    return flakyAttempts > 1;
}

bool deinitStable() {
    Serial.println("deinit stable");
    return true;
}

bool deinitFlaky() {
    Serial.println("deinit flaky (forced fail)");
    return false;
}

void setup() {
    Serial.begin(115200);

    worker.init(ESPWorker::Config{});

    LifecycleConfig cfg{};
    cfg.worker = &worker;
    cfg.rollbackOnInitFailure = true;
    cfg.continueTeardownOnFailure = true;
    cfg.onInitFailed = []() { Serial.println("init failed callback"); };

    lifecycle.configure(cfg);
    lifecycle.init({"core"});

    lifecycle.addTo("core", "stable", initStable, deinitStable);
    lifecycle.addTo("core", "flaky", initFlaky, deinitFlaky).after("stable");

    (void)lifecycle.build();

    LifecycleResult first = lifecycle.initialize();
    Serial.printf("first initialize ok=%d\n", first.ok ? 1 : 0);

    LifecycleResult second = lifecycle.initialize();
    Serial.printf("second initialize ok=%d\n", second.ok ? 1 : 0);

    // Continue teardown despite flaky deinit failure.
    (void)lifecycle.deinitialize();
}

void loop() {
    delay(500);
}
