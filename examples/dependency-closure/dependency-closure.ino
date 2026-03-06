#include <Arduino.h>
#include <ESPLifecycle.h>
#include <ESPWorker.h>

ESPWorker worker;
ESPLifecycle lifecycle;

static bool loggerReady = false;
static bool wifiReady = false;
static bool apiReady = false;

bool initLogger() {
    loggerReady = true;
    Serial.println("init logger");
    return true;
}

bool initWifi() {
    if( !loggerReady ){
        return false;
    }
    wifiReady = true;
    Serial.println("init wifi");
    return true;
}

bool initApi() {
    if( !wifiReady ){
        return false;
    }
    apiReady = true;
    Serial.println("init api");
    return true;
}

bool deinitLogger() {
    loggerReady = false;
    Serial.println("deinit logger");
    return true;
}

bool deinitWifi() {
    wifiReady = false;
    Serial.println("deinit wifi");
    return true;
}

bool deinitApi() {
    apiReady = false;
    Serial.println("deinit api");
    return true;
}

void setup() {
    Serial.begin(115200);

    worker.init(ESPWorker::Config{});

    LifecycleConfig config{};
    config.worker = &worker;
    config.enableParallelInit = false;
    config.enableParallelDeinit = false;
    config.enableParallelReinit = false;
    config.dependencyReinitialization = true;

    lifecycle.configure(config);
    lifecycle.init({"core", "network", "services"});

    lifecycle.addTo("core", "logger", initLogger, deinitLogger);
    lifecycle.addTo("network", "wifi", initWifi, deinitWifi).after("logger");
    lifecycle.addTo("services", "api", initApi, deinitApi).after("wifi");

    (void)lifecycle.build();
    (void)lifecycle.initialize();

    Serial.println("Reinitialize logger -> expects wifi+api closure");
    (void)lifecycle.reinitialize({"logger"});

    Serial.println("Deinitialize logger -> expects wifi+api teardown");
    (void)lifecycle.deinitialize({"logger"});
}

void loop() {
    delay(500);
}
