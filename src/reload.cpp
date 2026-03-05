#include "esp_lifecycle/lifecycle.h"

#include "ESPEventBus.h"

#include <chrono>
#include <thread>
#include <utility>

#if __has_include(<freertos/FreeRTOS.h>)
#include <freertos/FreeRTOS.h>
#endif

#if __has_include(<freertos/task.h>)
#include <freertos/task.h>
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif

bool ESPLifecycle::startScopeListener(
    ESPEventBus& eventBus,
    uint16_t eventId,
    std::function<uint32_t(void*)> payloadToScopeMask
) {
    if( payloadToScopeMask == nullptr ){
        return false;
    }

    if( config.worker == nullptr ){
        log(LifecycleLogLevel::Error, "scope listener requires config.worker");
        return false;
    }

    stopScopeListener();

    EventBusSub subId = eventBus.subscribe(
        eventId,
        [this, payloadToScopeMask](void* payload, void* /*userArg*/) {
            const uint32_t mask = payloadToScopeMask(payload);
            if( mask == 0 ){
                return;
            }
            scheduleScopeReinitialize(mask);
        }
    );

    if( subId == 0 ){
        return false;
    }

    std::lock_guard<std::mutex> lock(listenerMutex);
    listenerBus = &eventBus;
    listenerEventId = eventId;
    listenerPayloadMaskFn = std::move(payloadToScopeMask);
    listenerSubId = subId;
    pendingScopeMask = 0;
    listenerWorkerRunning = false;
    return true;
}

void ESPLifecycle::stopScopeListener() {
    std::lock_guard<std::mutex> lock(listenerMutex);
    if( listenerBus != nullptr && listenerSubId != 0 ){
        listenerBus->unsubscribe(listenerSubId);
    }

    listenerBus = nullptr;
    listenerSubId = 0;
    listenerEventId = 0;
    listenerPayloadMaskFn = {};
    pendingScopeMask = 0;
    listenerWorkerRunning = false;
}

void ESPLifecycle::scheduleScopeReinitialize(uint32_t scopeMask) {
    if( scopeMask == 0 ){
        return;
    }

    bool shouldSpawnWorker = false;

    {
        std::lock_guard<std::mutex> lock(listenerMutex);
        pendingScopeMask |= scopeMask;

        if( !listenerWorkerRunning ){
            listenerWorkerRunning = true;
            shouldSpawnWorker = true;
        }
    }

    if( !shouldSpawnWorker ){
        return;
    }

    if( config.worker == nullptr ){
        std::lock_guard<std::mutex> lock(listenerMutex);
        listenerWorkerRunning = false;
        return;
    }

    WorkerConfig workerConfig{};
    workerConfig.name = "lifecycle-reload";

    WorkerResult spawnResult = config.worker->spawnExt(
        [this]() {
            listenerWorkerLoop();
        },
        workerConfig
    );

    if( spawnResult.error != WorkerError::None ){
        std::lock_guard<std::mutex> lock(listenerMutex);
        listenerWorkerRunning = false;
        log(LifecycleLogLevel::Error, "failed to spawn scope listener worker");
    }
}

void ESPLifecycle::listenerWorkerLoop() {
    while( true ){
#if __has_include(<freertos/task.h>)
        vTaskDelay(pdMS_TO_TICKS(listenerCoalesceMs));
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(listenerCoalesceMs));
#endif

        uint32_t mask = 0;
        {
            std::lock_guard<std::mutex> lock(listenerMutex);
            mask = pendingScopeMask;
            pendingScopeMask = 0;
        }

        if( mask == 0 ){
            break;
        }

        LifecycleResult result = reinitializeByScopeMask(mask);
        if( !result.ok ){
            if( result.code == LifecycleErrorCode::Busy ){
                log(LifecycleLogLevel::Warn, "scope-triggered reinitialize rejected because lifecycle is busy");
            } else {
                log(LifecycleLogLevel::Error, "scope-triggered reinitialize failed");
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(listenerMutex);
        listenerWorkerRunning = false;

        if( pendingScopeMask != 0 ){
            listenerWorkerRunning = true;
            if( config.worker != nullptr ){
                WorkerConfig workerConfig{};
                workerConfig.name = "lifecycle-reload";
                (void)config.worker->spawnExt([this]() { listenerWorkerLoop(); }, workerConfig);
            } else {
                listenerWorkerRunning = false;
            }
        }
    }
}
