# ESPLifecycle

ESPLifecycle is the ESPToolKit lifecycle orchestrator for deterministic init, deinit, and scoped reinit with ESPStartup-like fluent registration.

## CI / Release / License
[![CI](https://github.com/ESPToolKit/esp-lifecycle/actions/workflows/ci.yml/badge.svg)](https://github.com/ESPToolKit/esp-lifecycle/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ESPToolKit/esp-lifecycle?sort=semver)](https://github.com/ESPToolKit/esp-lifecycle/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

## Features
- Section-based orchestration with dependency validation (`after` / `before`).
- Deterministic `initialize()`, `deinitialize()`, `reinitialize*()`.
- Scoped deinit/reinit closure with dependency correctness.
- Optional parallel waves for init, deinit, and reinit.
- Deferred sections with readiness gates.
- Snapshot callback + direct `snapshotJson()` helper (ArduinoJson V7).
- Optional scope listener with 25ms coalescing.

## Installation
- PlatformIO: add `https://github.com/ESPToolKit/esp-lifecycle.git` to `lib_deps`.
- Arduino IDE: install as ZIP from this repository.

Dependencies:
- `ESPWorker`
- `ESPEventBus`
- `ArduinoJson`

## Single Include
```cpp
#include <ESPLifecycle.h>
```

## Quick Start (ESPStartup-like)
```cpp
#include <ESPLifecycle.h>
#include <ESPWorker.h>

ESPWorker worker;
ESPLifecycle lifecycle;

void setup() {
    worker.init(ESPWorker::Config{});

    LifecycleConfig cfg{};
    cfg.worker = &worker;
    cfg.enableParallelInit = true;
    cfg.enableParallelDeinit = true;
    cfg.enableParallelReinit = true;
    cfg.onReady = []() { Serial.println("ready"); };

    lifecycle.configure(cfg);
    lifecycle.init({"core", "network"});

    lifecycle.addTo("core", "logger", []() { return true; }, []() { return true; }).parallelSafe();
    lifecycle.addTo("core", "storage", []() { return true; }, []() { return true; });
    lifecycle.addTo("network", "wifi", []() { return true; }, []() { return true; }).after("logger");

    if (!lifecycle.start()) {
        Serial.println("lifecycle start failed");
    }
}
```

## Deferred Section Example
```cpp
lifecycle.section("services")
    .mode(LifecycleSectionMode::Deferred)
    .readiness(
        []() { return networkReady(); },
        [](TickType_t waitTicks) { vTaskDelay(waitTicks); }
    );
```

## Scope Semantics
- `deinitializeByScopeMask(mask)` expands to target nodes + transitive dependents.
- `reinitializeByScopeMask(mask)` expands to target nodes + dependents + required dependencies.
- `reinitializeByNodeNames(...)` uses the same reinit closure rule.

## Snapshot API
```cpp
LifecycleSnapshot snap = lifecycle.snapshot();
JsonDocument json = lifecycle.snapshotJson();
serializeJson(json, Serial);
```

`snapshotJson()` includes:
- `state`, `activeNode`, `completed`, `total`, `failed`, `errorCode`, `updatedAtMs`
- `phase` (`initialize|deinitialize|reinitialize|idle`)
- `lastOperationOk`
- `lastError.nodeName`, `lastError.detail`
- `parallel.enabled.init|deinit|reinit`

## Runtime API
- `LifecycleResult build()`
- `bool start()` / `void stop()` compatibility aliases
- `LifecycleResult initialize()`
- `LifecycleResult deinitialize()`
- `LifecycleResult deinitializeByScopeMask(uint32_t scopeMask)`
- `LifecycleResult reinitializeAll()`
- `LifecycleResult reinitializeByScopeMask(uint32_t scopeMask)`
- `LifecycleResult reinitializeByNodeNames(const std::vector<const char*>& nodeNames)`
- `LifecycleSnapshot snapshot() const`
- `JsonDocument snapshotJson() const`

## Failure Policy
- No exceptions.
- Errors are returned through `LifecycleResult`.
- `rollbackOnInitFailure=true` rolls back initialized subset.
- `continueTeardownOnFailure=false` stops at first teardown failure.
- Busy transitions return `LifecycleErrorCode::Busy`.

## Migration from ESPStartup
- `start()` exists as compatibility alias.
- `stop()` maps to `deinitialize()`.
- Each node must provide `init` and `teardown` callbacks.
- Parallel config now exists for all phases.

## Examples
- `examples/basic-startup`
- `examples/scoped-reload`

## License
MIT - see [LICENSE.md](LICENSE.md).

## ESPToolKit
- Repositories: <https://github.com/orgs/ESPToolKit/repositories>
- Support: <https://ko-fi.com/esptoolkit>
- Website: <https://www.esptoolkit.hu/>
