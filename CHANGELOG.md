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

### Changed
- Public lifecycle targeting is now node-name based.
- Partial deinitialize expands selected nodes with transitive dependents.
- Partial reinitialize expands selected nodes with dependents and required dependencies.
- Reload listener now consumes node-name payloads and coalesces deduplicated names.

### Removed
- `start()` / `stop()` compatibility aliases.
- Scope-mask-based APIs:
  - `deinitializeByScopeMask(...)`
  - `reinitializeByScopeMask(...)`
  - `reinitializeByNodeNames(...)`
- Node scope bit tagging API:
  - `NodeBuilder::reloadScope(...)`

### Fixed
- Error reporting now uses node-focused resolution failures (`UnknownNode`, `NodeResolutionFailed`).

## [0.1.0] - 2026-03-05
### Added
- Initial stable release of ESPLifecycle.
