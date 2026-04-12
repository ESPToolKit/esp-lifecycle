# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added
- Name-first runtime APIs:
  - `reinitialize(std::initializer_list<const char*>)`
  - `reinitialize(const std::vector<const char*>&)`
  - `deinitialize(std::initializer_list<const char*>)`
  - `deinitialize(const std::vector<const char*>&)`
- Node dependency batching API:
  - `NodeBuilder::after(std::initializer_list<const char*>)`
- Name-list based reload listener API:
  - `startReloadListener(ESPEventBus&, uint16_t, std::function<std::vector<const char*>(void*)>)`
  - `stopReloadListener()`
- Optional node teardown callback overload:
  - `addTo(const char* section, const char* nodeName, std::function<bool()> initFn)`
  - Defaults teardown to a callback that returns `true`
- Extended example suite:
  - `dependency-closure`
  - `deferred-readiness`
  - `parallel-waves`
  - `failure-policy`
  - `reload-burst`
- `LifecycleConfig::dependencyReinitialization` (default `false`) to control partial reinit closure expansion.

### Changed
- Public lifecycle targeting is now node-name based.
- Partial deinitialize expands selected nodes with transitive dependents.
- Partial reinitialize targets selected nodes by default.
- Partial reinitialize closure (dependents + required dependencies) is now opt-in via `dependencyReinitialization=true`.
- Reload listener now consumes node-name payloads and coalesces deduplicated names.

### Removed
- `start()` / `stop()` compatibility aliases.
- Scope-mask-based APIs:
  - `deinitializeByScopeMask(...)`
  - `reinitializeByScopeMask(...)`

### Fixed
- CI now pins PIOArduino Core to `v6.1.19` and installs the ESP32 platform via `pio pkg install`, restoring PlatformIO compatibility with the current `platform-espressif32` package.
  - `reinitializeByNodeNames(...)`
- Node scope bit tagging API:
  - `NodeBuilder::reloadScope(...)`

### Fixed
- Error reporting now uses node-focused resolution failures (`UnknownNode`, `NodeResolutionFailed`).
- `snapshotJson()` now exposes `phaseCompleted` as an explicit boolean so JSON consumers do not need to infer completion from the numeric `completed` counter.

## [0.1.0] - 2026-03-05
### Added
- Initial stable release of ESPLifecycle.
