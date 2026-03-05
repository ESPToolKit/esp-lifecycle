# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added
- Standalone `ESPLifecycle` library scaffold with ESPToolKit metadata and workflows.
- Fluent lifecycle registration API with section and node builders.
- Deterministic `build()`, `initialize()`, `deinitialize()`, and scoped `reinitialize()` runtime paths.
- Optional deferred sections with readiness and wait callbacks.
- Snapshot/status callback plumbing for host observability.
- Optional reload listener integration via `ESPEventBus` + `ESPWorker` with 25ms coalescing.
- Basic and scoped-reload Arduino examples.

## [0.1.0] - 2026-03-05
### Added
- Initial stable release of ESPLifecycle.
