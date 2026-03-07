#include "esp_lifecycle/lifecycle.h"

namespace {

const char* stateToText(LifecycleState state) {
    switch( state ){
        case LifecycleState::Idle:
            return "idle";
        case LifecycleState::Initializing:
            return "initializing";
        case LifecycleState::Running:
            return "running";
        case LifecycleState::Reinitializing:
            return "reinitializing";
        case LifecycleState::Deinitializing:
            return "deinitializing";
        case LifecycleState::Failed:
            return "failed";
    }

    return "unknown";
}

const char* errorCodeToText(LifecycleErrorCode code) {
    switch( code ){
        case LifecycleErrorCode::None:
            return "none";
        case LifecycleErrorCode::DuplicateNode:
            return "duplicate_node";
        case LifecycleErrorCode::MissingDependency:
            return "missing_dependency";
        case LifecycleErrorCode::CycleDetected:
            return "cycle_detected";
        case LifecycleErrorCode::Busy:
            return "busy";
        case LifecycleErrorCode::InitFailed:
            return "init_failed";
        case LifecycleErrorCode::TeardownFailed:
            return "teardown_failed";
        case LifecycleErrorCode::InvalidSection:
            return "invalid_section";
        case LifecycleErrorCode::InvalidConfig:
            return "invalid_config";
        case LifecycleErrorCode::UnknownNode:
            return "unknown_node";
        case LifecycleErrorCode::NodeResolutionFailed:
            return "node_resolution_failed";
    }

    return "unknown";
}

bool isTransitionState(LifecycleState state) {
    return state == LifecycleState::Initializing ||
           state == LifecycleState::Reinitializing ||
           state == LifecycleState::Deinitializing;
}

}  // namespace

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

void ESPLifecycle::setPhase(const char* phaseName) {
    std::lock_guard<std::mutex> lock(snapshotMutex);
    phaseText = phaseName == nullptr ? "idle" : phaseName;
}

LifecycleSnapshot ESPLifecycle::snapshot() const {
    std::lock_guard<std::mutex> lock(snapshotMutex);
    return snapshotValue;
}

JsonDocument ESPLifecycle::snapshotJson() const {
    JsonDocument document;

    LifecycleSnapshot copy;
    std::string activeNode;
    std::string phase;
    std::string errorNode;
    std::string errorDetail;
    bool operationOk = true;

    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        copy = snapshotValue;
        activeNode = activeNodeText;
        phase = phaseText;
        errorNode = nodeNameText;
        errorDetail = detailText;
        operationOk = lastOperationOk;
    }

    document["state"] = stateToText(copy.state);
    document["activeNode"] = activeNode.empty() ? nullptr : activeNode.c_str();
    document["completed"] = copy.completed;
    document["phaseCompleted"] = !isTransitionState(copy.state);
    document["total"] = copy.total;
    document["failed"] = copy.failed;
    document["errorCode"] = errorCodeToText(copy.errorCode);
    document["updatedAtMs"] = copy.updatedAtMs;

    document["phase"] = phase.c_str();
    document["lastOperationOk"] = operationOk;

    JsonObject error = document["lastError"].to<JsonObject>();
    error["nodeName"] = errorNode.empty() ? nullptr : errorNode.c_str();
    error["detail"] = errorDetail.empty() ? nullptr : errorDetail.c_str();

    JsonObject parallel = document["parallel"]["enabled"].to<JsonObject>();
    parallel["init"] = config.enableParallelInit;
    parallel["deinit"] = config.enableParallelDeinit;
    parallel["reinit"] = config.enableParallelReinit;

    return document;
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
        config.onSnapshot(snapshot());
    }
}
