#include "esp_lifecycle/lifecycle.h"

#include "ESPEventBus.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if __has_include(<freertos/FreeRTOS.h>)
#include <freertos/FreeRTOS.h>
#endif

#if __has_include(<freertos/task.h>)
#include <freertos/task.h>
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif

bool ESPLifecycle::startReloadListener(
    ESPEventBus& eventBus,
    uint16_t eventId,
    std::function<std::vector<const char*>(void*)> payloadToNodeNames
) {
    if( payloadToNodeNames == nullptr ){
        return false;
    }

    if( config.worker == nullptr ){
        log(LifecycleLogLevel::Error, "reload listener requires config.worker");
        return false;
    }

    stopReloadListener();

    EventBusSub subId = eventBus.subscribe(
        eventId,
        [this, payloadToNodeNames](void* payload, void* /*userArg*/) {
            const std::vector<const char*> names = payloadToNodeNames(payload);
            if( names.empty() ){
                return;
            }
            scheduleNodeReinitialize(names);
        }
    );

    if( subId == 0 ){
        return false;
    }

    std::lock_guard<std::mutex> lock(listenerMutex);
    listenerBus = &eventBus;
    listenerEventId = eventId;
    listenerPayloadNamesFn = std::move(payloadToNodeNames);
    listenerSubId = subId;
    pendingNodeNames.clear();
    listenerWorkerRunning = false;
    return true;
}

void ESPLifecycle::stopReloadListener() {
    std::lock_guard<std::mutex> lock(listenerMutex);
    if( listenerBus != nullptr && listenerSubId != 0 ){
        listenerBus->unsubscribe(listenerSubId);
    }

    listenerBus = nullptr;
    listenerSubId = 0;
    listenerEventId = 0;
    listenerPayloadNamesFn = {};
    pendingNodeNames.clear();
    listenerWorkerRunning = false;
}

void ESPLifecycle::scheduleNodeReinitialize(const std::vector<const char*>& nodeNames) {
    if( nodeNames.empty() ){
        return;
    }

    bool shouldSpawnWorker = false;

    {
        std::lock_guard<std::mutex> lock(listenerMutex);
        for( const char* name : nodeNames ){
            if( name == nullptr || name[0] == '\0' ){
                continue;
            }

            if( std::find(pendingNodeNames.begin(), pendingNodeNames.end(), name) == pendingNodeNames.end() ){
                pendingNodeNames.emplace_back(name);
            }
        }

        if( pendingNodeNames.empty() ){
            return;
        }

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
    workerConfig.stackSizeBytes = config.workerStackSizeBytes;
    if( config.workerName != nullptr ){
        workerConfig.name = std::string(config.workerName) + "-reload";
    } else {
        workerConfig.name = "lifecycle-reload";
    }

    WorkerResult spawnResult = config.worker->spawn(
        [this]() {
            listenerWorkerLoop();
        },
        workerConfig
    );

    if( spawnResult.error != WorkerError::None ){
        std::lock_guard<std::mutex> lock(listenerMutex);
        listenerWorkerRunning = false;
        log(LifecycleLogLevel::Error, "failed to spawn reload listener worker");
    }
}

void ESPLifecycle::listenerWorkerLoop() {
    while( true ){
#if __has_include(<freertos/task.h>)
        vTaskDelay(pdMS_TO_TICKS(listenerCoalesceMs));
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(listenerCoalesceMs));
#endif

        std::vector<std::string> pending;
        {
            std::lock_guard<std::mutex> lock(listenerMutex);
            pending.swap(pendingNodeNames);
        }

        if( pending.empty() ){
            break;
        }

        std::vector<const char*> names;
        names.reserve(pending.size());
        for( const std::string& name : pending ){
            names.push_back(name.c_str());
        }

        LifecycleResult result = reinitialize(names);
        if( !result.ok ){
            if( result.code == LifecycleErrorCode::Busy ){
                log(LifecycleLogLevel::Warn, "reload reinitialize rejected because lifecycle is busy");
            } else {
                log(LifecycleLogLevel::Error, "reload reinitialize failed");
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(listenerMutex);
        listenerWorkerRunning = false;

        if( !pendingNodeNames.empty() ){
            listenerWorkerRunning = true;
            if( config.worker != nullptr ){
                WorkerConfig workerConfig{};
                workerConfig.stackSizeBytes = config.workerStackSizeBytes;
                if( config.workerName != nullptr ){
                    workerConfig.name = std::string(config.workerName) + "-reload";
                } else {
                    workerConfig.name = "lifecycle-reload";
                }
                (void)config.worker->spawn([this]() { listenerWorkerLoop(); }, workerConfig);
            } else {
                listenerWorkerRunning = false;
            }
        }
    }
}
