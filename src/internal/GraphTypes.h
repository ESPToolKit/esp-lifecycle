#pragma once

#include <cstdint>
#include <functional>
#include <cstddef>
#include <string>
#include <vector>

#if __has_include(<freertos/FreeRTOS.h>)
#include <freertos/FreeRTOS.h>
#else
using TickType_t = uint32_t;
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif

#include "ESPWorker.h"

enum class LifecycleState : uint8_t {
    Idle,
    Initializing,
    Running,
    Reinitializing,
    Deinitializing,
    Failed,
};

enum class LifecycleSectionMode : uint8_t {
    Blocking,
    Deferred,
};

enum class LifecycleErrorCode : uint8_t {
    None,
    DuplicateNode,
    MissingDependency,
    CycleDetected,
    Busy,
    InitFailed,
    TeardownFailed,
    InvalidSection,
    InvalidConfig,
    UnknownNode,
    NodeResolutionFailed,
};

enum class LifecycleLogLevel : uint8_t {
    Debug,
    Info,
    Warn,
    Error,
};

struct LifecycleResult {
    bool ok = true;
    LifecycleErrorCode code = LifecycleErrorCode::None;
    const char* nodeName = nullptr;
    const char* detail = nullptr;
};

struct LifecycleSnapshot {
    LifecycleState state = LifecycleState::Idle;
    const char* activeNode = nullptr;
    uint16_t completed = 0;
    uint16_t total = 0;
    bool failed = false;
    LifecycleErrorCode errorCode = LifecycleErrorCode::None;
    uint32_t updatedAtMs = 0;
};

struct LifecycleConfig {
    ESPWorker* worker = nullptr;
    uint32_t defaultStepTimeoutMs = 3000;
    TickType_t waitTicks = pdMS_TO_TICKS(500);
    const char* workerName = "lifecycle-flow";
    size_t workerStackSizeBytes = 6 * 1024;
    bool enableParallelInit = false;
    bool enableParallelDeinit = false;
    bool enableParallelReinit = false;
    bool dependencyReinitialization = false;
    bool rollbackOnInitFailure = true;
    bool continueTeardownOnFailure = false;
    uint16_t maxNodes = 64;
    uint16_t maxDependencies = 256;
    std::function<void()> onInitStarted;
    std::function<void()> onReady;
    std::function<void()> onInitFailed;
    std::function<void(const LifecycleSnapshot&)> onSnapshot;
    std::function<void(LifecycleLogLevel, const char*)> logger;
};

struct LifecycleSectionDefinition {
    std::string name;
    LifecycleSectionMode mode = LifecycleSectionMode::Blocking;
    std::function<bool()> readinessCheck;
    std::function<void(TickType_t)> waitFn;
};

struct LifecycleNodeDefinition {
    std::string name;
    size_t sectionIndex = 0;
    std::function<bool()> initFn;
    std::function<bool()> teardownFn;
    uint32_t timeoutMs = 0;
    bool optional = false;
    bool parallelSafe = false;
    std::vector<std::string> dependenciesByName;
    std::vector<std::string> dependentsByName;
    std::vector<size_t> dependencyIndexes;
    std::vector<size_t> reverseDependencyIndexes;
};
