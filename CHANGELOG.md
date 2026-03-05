# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added
- ESPStartup-like compatibility APIs: `start()` and `stop()` aliases.
- `NodeBuilder::parallelSafe(bool)` for fluent startup-style tuning.
- `LifecycleConfig` parallel controls for all phases:
  - `enableParallelInit`
  - `enableParallelDeinit`
  - `enableParallelReinit`
- Additional runtime config fields:
  - `waitTicks`
  - `workerName`
  - `workerStackSizeBytes`
- Direct snapshot APIs:
  - `LifecycleSnapshot snapshot() const`
  - `JsonDocument snapshotJson() const`
- Scope teardown entrypoint:
  - `LifecycleResult deinitializeByScopeMask(uint32_t scopeMask)`

### Changed
- Scoped teardown now includes transitive dependents of selected scope nodes.
- Scoped reinit now includes selected nodes, their transitive dependents, and required transitive dependencies.
- Runtime scheduling now uses deterministic dependency waves for init/deinit/reinit.
- Parallel execution now supported in init/deinit/reinit waves when enabled.

### Fixed
- Dependency closure behavior now matches lifecycle correctness requirements for partial reinit.
- Snapshot metadata now includes phase and last operation/error context for websocket/UI consumers.

## [0.1.0] - 2026-03-05
### Added
- Initial stable release of ESPLifecycle.
