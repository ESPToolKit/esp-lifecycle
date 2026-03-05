# ESPLifecycle

ESPLifecycle is the ESPToolKit lifecycle orchestrator for deterministic init, deinit, and node-targeted reinit.

## CI / Release / License
[![CI](https://github.com/ESPToolKit/esp-lifecycle/actions/workflows/ci.yml/badge.svg)](https://github.com/ESPToolKit/esp-lifecycle/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ESPToolKit/esp-lifecycle?sort=semver)](https://github.com/ESPToolKit/esp-lifecycle/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

## Features
- Section-based orchestration with dependency validation (`after` / `before`).
- Deterministic `initialize()`, `deinitialize()`, and partial `reinitialize(...)`.
- Node-name targeted closure with dependency correctness.
- Optional parallel waves for init, deinit, and reinit.
- Deferred sections with readiness gates.
- Snapshot callback + direct `snapshotJson()` helper (ArduinoJson V7).
- Optional reload listener with 25ms coalescing and node-name payload mapping.

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

## Quick Start
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

    lifecycle.configure(cfg);
    lifecycle.init({"core", "network"});

    lifecycle.addTo("core", "logger", []() { return true; }, []() { return true; }).parallelSafe();
    lifecycle.addTo("core", "storage", []() { return true; }, []() { return true; });
    lifecycle.addTo("network", "wifi", []() { return true; }, []() { return true; }).after("logger");

    (void)lifecycle.build();
    (void)lifecycle.initialize();

    // Reinitialize logger and everything that depends on it.
    (void)lifecycle.reinitialize({"logger"});
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

## Node-Name Targeting Semantics
- `deinitialize({"logger"})` expands to selected nodes + transitive dependents.
- `reinitialize({"logger"})` expands to selected nodes + dependents + required dependencies.
- `reinitializeAll()` keeps full-graph behavior.

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
- `LifecycleResult initialize()`
- `LifecycleResult deinitialize()`
- `LifecycleResult deinitialize(std::initializer_list<const char*> nodeNames)`
- `LifecycleResult deinitialize(const std::vector<const char*>& nodeNames)`
- `LifecycleResult reinitializeAll()`
- `LifecycleResult reinitialize(std::initializer_list<const char*> nodeNames)`
- `LifecycleResult reinitialize(const std::vector<const char*>& nodeNames)`
- `bool startReloadListener(ESPEventBus&, uint16_t, std::function<std::vector<const char*>(void*)>)`
- `void stopReloadListener()`
- `LifecycleSnapshot snapshot() const`
- `JsonDocument snapshotJson() const`

## Failure Policy
- No exceptions.
- Errors are returned through `LifecycleResult`.
- `rollbackOnInitFailure=true` rolls back initialized subset.
- `continueTeardownOnFailure=false` stops at first teardown failure.
- Busy transitions return `LifecycleErrorCode::Busy`.

## Examples
- `examples/basic-startup`
- `examples/scoped-reload`
- `examples/dependency-closure` - demonstrates dependent closure for partial deinit/reinit.
- `examples/deferred-readiness` - deferred section gate using readiness callbacks.
- `examples/parallel-waves` - parallel init/deinit/reinit wave behavior.
- `examples/failure-policy` - rollback and teardown failure policy behavior.
- `examples/reload-burst` - listener burst coalescing and deduplicated node-name reloads.

## License
MIT - see [LICENSE.md](LICENSE.md).

## ESPToolKit
- Repositories: <https://github.com/orgs/ESPToolKit/repositories>
- Support: <https://ko-fi.com/esptoolkit>
- Website: <https://www.esptoolkit.hu/>
