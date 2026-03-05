#include "esp_lifecycle/lifecycle.h"

#include <algorithm>
#include <queue>
#include <unordered_map>

#if __has_include(<freertos/FreeRTOS.h>)
#include <freertos/FreeRTOS.h>
#endif

#if __has_include(<freertos/task.h>)
#include <freertos/task.h>
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif

LifecycleResult ESPLifecycle::initialize() {
    std::unique_lock<std::mutex> lock(transitionMutex, std::try_to_lock);
    if( !lock.owns_lock() ){
        return failResult(LifecycleErrorCode::Busy, nullptr, "lifecycle is busy", false);
    }

    if( !graphBuilt ){
        LifecycleResult buildResult = validateAndBuildGraph();
        if( !buildResult.ok ){
            return buildResult;
        }
    }

    if( state() == LifecycleState::Running ){
        return okResult("already running");
    }

    if( config.onInitStarted ){
        config.onInitStarted();
    }

    setState(LifecycleState::Initializing, nullptr);
    LifecycleResult initResult = initializeInternal(allNodeIndexes(), LifecycleState::Initializing);
    if( !initResult.ok ){
        if( config.onInitFailed ){
            config.onInitFailed();
        }
        return initResult;
    }

    setState(LifecycleState::Running, nullptr);
    if( config.onReady ){
        config.onReady();
    }

    return okResult("initialized");
}

LifecycleResult ESPLifecycle::deinitialize() {
    std::unique_lock<std::mutex> lock(transitionMutex, std::try_to_lock);
    if( !lock.owns_lock() ){
        return failResult(LifecycleErrorCode::Busy, nullptr, "lifecycle is busy", false);
    }

    bool anyInitialized = false;
    for( bool value : initialized ){
        if( value ){
            anyInitialized = true;
            break;
        }
    }

    if( !anyInitialized ){
        setState(LifecycleState::Idle, nullptr);
        return okResult("deinitialize no-op");
    }

    setState(LifecycleState::Deinitializing, nullptr);
    LifecycleResult result = deinitializeInternal(allNodeIndexes(), false);
    if( !result.ok ){
        setState(LifecycleState::Failed, result.nodeName);
        return result;
    }

    setState(LifecycleState::Idle, nullptr);
    return okResult("deinitialized");
}

LifecycleResult ESPLifecycle::reinitializeAll() {
    std::unique_lock<std::mutex> lock(transitionMutex, std::try_to_lock);
    if( !lock.owns_lock() ){
        return failResult(LifecycleErrorCode::Busy, nullptr, "lifecycle is busy", false);
    }

    if( !graphBuilt ){
        LifecycleResult buildResult = validateAndBuildGraph();
        if( !buildResult.ok ){
            return buildResult;
        }
    }

    setState(LifecycleState::Reinitializing, nullptr);

    std::vector<size_t> subset = allNodeIndexes();
    LifecycleResult deinitResult = deinitializeInternal(subset, false);
    if( !deinitResult.ok ){
        setState(LifecycleState::Failed, deinitResult.nodeName);
        return deinitResult;
    }

    LifecycleResult initResult = initializeInternal(subset, LifecycleState::Reinitializing);
    if( !initResult.ok ){
        if( config.onInitFailed ){
            config.onInitFailed();
        }
        return initResult;
    }

    setState(LifecycleState::Running, nullptr);
    if( config.onReady ){
        config.onReady();
    }

    return okResult("reinitialized all");
}

LifecycleResult ESPLifecycle::reinitializeByScopeMask(uint32_t scopeMask) {
    std::unique_lock<std::mutex> lock(transitionMutex, std::try_to_lock);
    if( !lock.owns_lock() ){
        return failResult(LifecycleErrorCode::Busy, nullptr, "lifecycle is busy", false);
    }

    if( !graphBuilt ){
        LifecycleResult buildResult = validateAndBuildGraph();
        if( !buildResult.ok ){
            return buildResult;
        }
    }

    bool knownScope = false;
    std::vector<size_t> subset = resolveScopeSubset(scopeMask, &knownScope);
    if( subset.empty() ){
        return failResult(
            knownScope ? LifecycleErrorCode::ScopeResolutionFailed : LifecycleErrorCode::UnknownScope,
            nullptr,
            "scope mask resolved no nodes",
            false
        );
    }

    LifecycleResult closureResult = expandSubsetWithClosure(subset);
    if( !closureResult.ok ){
        return closureResult;
    }

    setState(LifecycleState::Reinitializing, nullptr);

    LifecycleResult deinitResult = deinitializeInternal(subset, false);
    if( !deinitResult.ok ){
        setState(LifecycleState::Failed, deinitResult.nodeName);
        return deinitResult;
    }

    LifecycleResult initResult = initializeInternal(subset, LifecycleState::Reinitializing);
    if( !initResult.ok ){
        if( config.onInitFailed ){
            config.onInitFailed();
        }
        return initResult;
    }

    setState(LifecycleState::Running, nullptr);
    return okResult("reinitialized scope");
}

LifecycleResult ESPLifecycle::reinitializeByNodeNames(const std::vector<const char*>& nodeNames) {
    std::unique_lock<std::mutex> lock(transitionMutex, std::try_to_lock);
    if( !lock.owns_lock() ){
        return failResult(LifecycleErrorCode::Busy, nullptr, "lifecycle is busy", false);
    }

    if( !graphBuilt ){
        LifecycleResult buildResult = validateAndBuildGraph();
        if( !buildResult.ok ){
            return buildResult;
        }
    }

    std::unordered_map<std::string, size_t> nodeByName;
    nodeByName.reserve(nodes.size());
    for( size_t i = 0; i < nodes.size(); i++ ){
        nodeByName[nodes[i].name] = i;
    }

    std::vector<size_t> subset;
    subset.reserve(nodeNames.size());

    for( const char* nodeName : nodeNames ){
        if( nodeName == nullptr ){
            continue;
        }

        auto it = nodeByName.find(nodeName);
        if( it == nodeByName.end() ){
            return failResult(LifecycleErrorCode::ScopeResolutionFailed, nodeName, "unknown node name", false);
        }

        subset.push_back(it->second);
    }

    std::sort(subset.begin(), subset.end());
    subset.erase(std::unique(subset.begin(), subset.end()), subset.end());
    if( subset.empty() ){
        return failResult(LifecycleErrorCode::ScopeResolutionFailed, nullptr, "empty node subset", false);
    }

    LifecycleResult closureResult = expandSubsetWithClosure(subset);
    if( !closureResult.ok ){
        return closureResult;
    }

    setState(LifecycleState::Reinitializing, nullptr);

    LifecycleResult deinitResult = deinitializeInternal(subset, false);
    if( !deinitResult.ok ){
        setState(LifecycleState::Failed, deinitResult.nodeName);
        return deinitResult;
    }

    LifecycleResult initResult = initializeInternal(subset, LifecycleState::Reinitializing);
    if( !initResult.ok ){
        if( config.onInitFailed ){
            config.onInitFailed();
        }
        return initResult;
    }

    setState(LifecycleState::Running, nullptr);
    return okResult("reinitialized nodes");
}

LifecycleResult ESPLifecycle::initializeInternal(const std::vector<size_t>& subset, LifecycleState transitionState) {
    setState(transitionState, nullptr);

    std::vector<bool> selected(nodes.size(), false);
    for( size_t index : subset ){
        if( index < selected.size() ){
            selected[index] = true;
        }
    }

    for( size_t sectionIndex = 0; sectionIndex < sections.size(); sectionIndex++ ){
        LifecycleResult sectionResult = runSection(sectionIndex, subset);
        if( !sectionResult.ok ){
            if( config.rollbackOnInitFailure ){
                std::vector<size_t> rollbackSubset;
                rollbackSubset.reserve(nodes.size());
                for( size_t i = 0; i < nodes.size(); i++ ){
                    if( initialized[i] && selected[i] ){
                        rollbackSubset.push_back(i);
                    }
                }
                (void)deinitializeInternal(rollbackSubset, false);
            }

            return sectionResult;
        }
    }

    return okResult();
}

LifecycleResult ESPLifecycle::runSection(size_t sectionIndex, const std::vector<size_t>& subset) {
    if( sectionIndex >= sections.size() ){
        return failResult(LifecycleErrorCode::InvalidSection, nullptr, "section index out of bounds", true);
    }

    std::vector<bool> selected(nodes.size(), false);
    for( size_t index : subset ){
        if( index < selected.size() ){
            selected[index] = true;
        }
    }

    const LifecycleSectionDefinition& sectionDef = sections[sectionIndex];
    if( sectionDef.mode == LifecycleSectionMode::Deferred ){
        while( true ){
            if( sectionDef.readinessCheck && sectionDef.readinessCheck() ){
                break;
            }

            if( !sectionDef.waitFn ){
                return failResult(
                    LifecycleErrorCode::InvalidSection,
                    nullptr,
                    "deferred section wait callback missing",
                    true
                );
            }

            sectionDef.waitFn(pdMS_TO_TICKS(listenerCoalesceMs));
        }
    }

    for( size_t nodeIndex : topologicalOrder ){
        if( nodeIndex >= nodes.size() ){
            return failResult(LifecycleErrorCode::InvalidConfig, nullptr, "node index out of bounds", true);
        }

        if( !selected[nodeIndex] ){
            continue;
        }

        if( nodes[nodeIndex].sectionIndex != sectionIndex ){
            continue;
        }

        LifecycleResult initResult = runNodeInit(nodeIndex);
        if( !initResult.ok ){
            if( nodes[nodeIndex].optional ){
                log(LifecycleLogLevel::Warn, "optional node init failed, continuing");
                continue;
            }
            return initResult;
        }
    }

    return okResult();
}

LifecycleResult ESPLifecycle::runNodeInit(size_t nodeIndex) {
    if( nodeIndex >= nodes.size() ){
        return failResult(LifecycleErrorCode::InvalidConfig, nullptr, "node index out of bounds", true);
    }

    const LifecycleNodeDefinition& node = nodes[nodeIndex];
    setState(state(), node.name.c_str());

    if( !node.initFn ){
        return failResult(LifecycleErrorCode::InvalidConfig, node.name.c_str(), "init callback missing", true);
    }

    if( !node.initFn() ){
        return failResult(LifecycleErrorCode::InitFailed, node.name.c_str(), "node init failed", true);
    }

    initialized[nodeIndex] = true;
    markProgress(node.name.c_str(), true);
    return okResult();
}

LifecycleResult ESPLifecycle::runNodeTeardown(size_t nodeIndex) {
    if( nodeIndex >= nodes.size() ){
        return failResult(LifecycleErrorCode::InvalidConfig, nullptr, "node index out of bounds", true);
    }

    const LifecycleNodeDefinition& node = nodes[nodeIndex];
    setState(state(), node.name.c_str());

    if( !node.teardownFn ){
        return failResult(LifecycleErrorCode::InvalidConfig, node.name.c_str(), "teardown callback missing", true);
    }

    if( !node.teardownFn() ){
        return failResult(LifecycleErrorCode::TeardownFailed, node.name.c_str(), "node teardown failed", true);
    }

    initialized[nodeIndex] = false;
    markProgress(node.name.c_str(), false);
    return okResult();
}

LifecycleResult ESPLifecycle::deinitializeInternal(const std::vector<size_t>& subset, bool updateState) {
    if( updateState ){
        setState(LifecycleState::Deinitializing, nullptr);
    }

    std::vector<size_t> reversed = reverseTopologicalSubset(subset);
    for( size_t nodeIndex : reversed ){
        if( nodeIndex >= initialized.size() || !initialized[nodeIndex] ){
            continue;
        }

        LifecycleResult stepResult = runNodeTeardown(nodeIndex);
        if( !stepResult.ok ){
            if( config.continueTeardownOnFailure ){
                continue;
            }
            return stepResult;
        }
    }

    return okResult();
}

std::vector<size_t> ESPLifecycle::resolveScopeSubset(uint32_t scopeMask, bool* knownScope) const {
    std::vector<size_t> subset;
    if( scopeMask == 0 ){
        if( knownScope != nullptr ){
            *knownScope = false;
        }
        return subset;
    }

    bool found = false;

    for( size_t i = 0; i < nodes.size(); i++ ){
        if( nodes[i].reloadScopeMask == 0 ){
            continue;
        }

        if( (nodes[i].reloadScopeMask & scopeMask) != 0 ){
            found = true;
            subset.push_back(i);
        }
    }

    if( knownScope != nullptr ){
        *knownScope = found;
    }
    return subset;
}

LifecycleResult ESPLifecycle::expandSubsetWithClosure(std::vector<size_t>& subset) {
    if( subset.empty() ){
        return okResult();
    }

    std::vector<bool> included(nodes.size(), false);
    std::queue<size_t> pending;

    for( size_t index : subset ){
        if( index >= nodes.size() ){
            return failResult(LifecycleErrorCode::ScopeResolutionFailed, nullptr, "subset index out of bounds", false);
        }

        if( !included[index] ){
            included[index] = true;
            pending.push(index);
        }
    }

    while( !pending.empty() ){
        const size_t current = pending.front();
        pending.pop();

        for( size_t dep : nodes[current].dependencyIndexes ){
            if( dep < included.size() && !included[dep] ){
                included[dep] = true;
                pending.push(dep);
            }
        }

        for( size_t dependent : nodes[current].reverseDependencyIndexes ){
            if( dependent < included.size() && !included[dependent] ){
                included[dependent] = true;
                pending.push(dependent);
            }
        }
    }

    subset.clear();
    for( size_t index : topologicalOrder ){
        if( index < included.size() && included[index] ){
            subset.push_back(index);
        }
    }

    if( subset.empty() ){
        return failResult(LifecycleErrorCode::ScopeResolutionFailed, nullptr, "dependency closure is empty", false);
    }

    return okResult();
}
