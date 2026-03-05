#include "esp_lifecycle/lifecycle.h"

void ESPLifecycle::setState(LifecycleState stateValue, const char* activeNode) {
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        currentState = stateValue;
    }

    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        snapshotValue.state = stateValue;
        activeNodeText = activeNode == nullptr ? "" : activeNode;
        snapshotValue.activeNode = activeNodeText.empty() ? nullptr : activeNodeText.c_str();
        snapshotValue.updatedAtMs = nowMs();
    }

    publishSnapshot();
}

void ESPLifecycle::markProgress(const char* activeNode, bool /*completedStep*/) {
    {
        std::lock_guard<std::mutex> lock(snapshotMutex);

        activeNodeText = activeNode == nullptr ? "" : activeNode;
        snapshotValue.activeNode = activeNodeText.empty() ? nullptr : activeNodeText.c_str();

        uint16_t count = 0;
        for( bool isInit : initialized ){
            if( isInit ){
                count++;
            }
        }

        snapshotValue.completed = count;
        snapshotValue.failed = false;
        snapshotValue.errorCode = LifecycleErrorCode::None;
        snapshotValue.updatedAtMs = nowMs();
    }

    publishSnapshot();
}

void ESPLifecycle::publishSnapshot() {
    if( config.onSnapshot ){
        LifecycleSnapshot copy;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex);
            copy = snapshotValue;
        }
        config.onSnapshot(copy);
    }
}
