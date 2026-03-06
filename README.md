# ESPLifecycle

ESPLifecycle is the ESPToolKit lifecycle orchestrator for deterministic init, deinit, and node-targeted reinit.

## CI / Release / License
[![CI](https://github.com/ESPToolKit/esp-lifecycle/actions/workflows/ci.yml/badge.svg)](https://github.com/ESPToolKit/esp-lifecycle/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ESPToolKit/esp-lifecycle?sort=semver)](https://github.com/ESPToolKit/esp-lifecycle/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

## Features
- Section-based orchestration with dependency validation (`after` / `before`).
- Batch dependency declaration: `.after({"dep-a", "dep-b"})`.
- Deterministic `initialize()`, `deinitialize()`, and partial `reinitialize(...)`.
- Node-name targeted reinit with optional dependency closure.
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
    cfg.dependencyReinitialization = true; // opt-in: include dependents/dependencies on partial reinit

    lifecycle.configure(cfg);
    lifecycle.init({"core", "network"});

    lifecycle.addTo("core", "logger", []() { return true; }).parallelSafe();
    lifecycle.addTo("core", "storage", []() { return true; }, []() { return true; });
    lifecycle.addTo("network", "wifi", []() { return true; }, []() { return true; }).after("logger");
    lifecycle.addTo("network", "api", []() { return true; }, []() { return true; }).after({"logger", "storage"});

    (void)lifecycle.build();
    (void)lifecycle.initialize();

    // Reinitialize logger and everything that depends on it (because dependencyReinitialization=true).
    (void)lifecycle.reinitialize({"logger"});
}
```

If teardown is omitted in `addTo(...)`, ESPLifecycle uses a default teardown callback that returns `true`.

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
- `reinitialize({"logger"})` targets selected nodes only (default behavior).
- Set `cfg.dependencyReinitialization = true` to expand `reinitialize({"logger"})` to selected nodes + dependents + required dependencies.
- `reinitializeAll()` keeps full-graph behavior.

## Migration Note
- If you relied on legacy partial-reinit closure, set `cfg.dependencyReinitialization = true` to retain the previous behavior.

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
- `NodeBuilder& addTo(const char* section, const char* nodeName, std::function<bool()> initFn)`
- `NodeBuilder& addTo(const char* section, const char* nodeName, std::function<bool()> initFn, std::function<bool()> teardownFn)`
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
