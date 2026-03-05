# ESPLifecycle

ESPLifecycle is a standalone ESPToolKit library for deterministic startup, teardown, and scoped reinitialization of ESP32 app subsystems.

## CI / Release / License
[![CI](https://github.com/ESPToolKit/esp-lifecycle/actions/workflows/ci.yml/badge.svg)](https://github.com/ESPToolKit/esp-lifecycle/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ESPToolKit/esp-lifecycle?sort=semver)](https://github.com/ESPToolKit/esp-lifecycle/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

## Purpose
Use ESPLifecycle when you need:
- deterministic boot order with dependency validation,
- explicit teardown (`deinitialize()`),
- scoped reloads (`reinitializeByScopeMask(...)`, `reinitializeByNodeNames(...)`),
- state snapshots for websocket/UI status updates.

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

## Quick Start (Fluent API)
```cpp
#include <ESPLifecycle.h>
#include <ESPWorker.h>

ESPWorker worker;
ESPLifecycle lifecycle;

void setup() {
    worker.init(ESPWorker::Config{});

    LifecycleConfig cfg{};
    cfg.worker = &worker;
    cfg.onReady = []() { Serial.println("Lifecycle ready"); };
    cfg.onInitFailed = []() { Serial.println("Lifecycle failed"); };
    cfg.onSnapshot = [](const LifecycleSnapshot& s) {
        Serial.printf("state=%d completed=%u/%u\n", static_cast<int>(s.state), s.completed, s.total);
    };

    lifecycle.configure(cfg);
    lifecycle.init({"core", "network", "services"});

    lifecycle.addTo("core", "logger", []() { return true; }, []() { return true; });
    lifecycle.addTo("core", "storage", []() { return true; }, []() { return true; }).after("logger");
    lifecycle.addTo("network", "wifi", []() { return true; }, []() { return true; }).after("storage");
    lifecycle.addTo("services", "api", []() { return true; }, []() { return true; }).after("wifi");

    LifecycleResult buildResult = lifecycle.build();
    if (!buildResult.ok) {
        return;
    }

    (void)lifecycle.initialize();
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

## Teardown and Reinit
```cpp
(void)lifecycle.deinitialize();
(void)lifecycle.reinitializeAll();
(void)lifecycle.reinitializeByScopeMask(0x01);
```

## Optional Reload Listener
```cpp
lifecycle.startScopeListener(
    eventBus,
    100,
    [](void* payload) -> uint32_t {
        if (payload == nullptr) {
            return 0;
        }
        return *static_cast<uint32_t*>(payload);
    }
);
```

Behavior:
- Coalesces rapid events in a 25ms window.
- Merges masks and performs a single reinit batch.
- If lifecycle is busy, the batch is rejected (`LifecycleErrorCode::Busy`).

## Failure Policy
- No exceptions are used.
- Every runtime method returns `LifecycleResult`.
- `rollbackOnInitFailure=true` rolls back already initialized nodes in reverse topological order.
- `continueTeardownOnFailure=false` stops at first teardown error.

## Thread Safety and Reentrancy
- Transitions are guarded by a non-blocking lock.
- Concurrent transition attempts return `Busy`.
- `deinitialize()` is idempotent when already idle.

## Validation Rules (`build()`)
`build()` fails on:
- duplicate section names,
- duplicate node names,
- missing dependencies,
- dependency cycles,
- deferred sections without readiness callbacks,
- invalid node/section references,
- optional reload-scope collisions when `disallowSharedReloadScopeBits=true`.

## Migration from ESPStartup
- `start()` becomes `build()` + `initialize()`.
- Each node must provide both `init` and `teardown` callbacks.
- Scoped reloads replace app-specific reloader wiring.
- Snapshot callback schema changes to lifecycle-focused fields.

## API Summary
- `bool configure(const LifecycleConfig& config)`
- `ESPLifecycle& init(std::initializer_list<const char*> sectionNames)`
- `SectionBuilder& section(const char* sectionName)`
- `NodeBuilder& addTo(const char* section, const char* nodeName, std::function<bool()> initFn, std::function<bool()> teardownFn)`
- `LifecycleResult build()`
- `LifecycleResult initialize()`
- `LifecycleResult deinitialize()`
- `LifecycleResult reinitializeAll()`
- `LifecycleResult reinitializeByScopeMask(uint32_t scopeMask)`
- `LifecycleResult reinitializeByNodeNames(const std::vector<const char*>& nodeNames)`
- `bool startScopeListener(ESPEventBus& eventBus, uint16_t eventId, std::function<uint32_t(void*)> payloadToScopeMask)`
- `void stopScopeListener()`
- `LifecycleState state() const`
- `void clear()`

## Examples
- `examples/basic-startup` - deterministic init/deinit flow.
- `examples/scoped-reload` - scope-mask-triggered reinitialization via event bus.

## License
MIT - see [LICENSE.md](LICENSE.md).

## ESPToolKit
- Repositories: <https://github.com/orgs/ESPToolKit/repositories>
- Support: <https://ko-fi.com/esptoolkit>
- Website: <https://www.esptoolkit.hu/>
