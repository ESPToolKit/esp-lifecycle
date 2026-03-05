#include "esp_lifecycle/lifecycle.h"

#include <algorithm>
#include <cstring>
#include <utility>

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif

#if __has_include(<freertos/task.h>)
#include <freertos/task.h>
#endif

ESPLifecycle::NodeBuilder::NodeBuilder(ESPLifecycle* lifecycleRef, size_t nodeIndexRef)
    : lifecycle(lifecycleRef), nodeIndex(nodeIndexRef) {}

ESPLifecycle::NodeBuilder& ESPLifecycle::NodeBuilder::after(const char* dependencyNodeName) {
    if( lifecycle != nullptr ){
        lifecycle->addDependencyName(nodeIndex, dependencyNodeName);
    }
    return *this;
}

ESPLifecycle::NodeBuilder& ESPLifecycle::NodeBuilder::before(const char* dependentNodeName) {
    if( lifecycle != nullptr ){
        lifecycle->addDependentName(nodeIndex, dependentNodeName);
    }
    return *this;
}

ESPLifecycle::NodeBuilder& ESPLifecycle::NodeBuilder::timeoutMs(uint32_t value) {
    if( lifecycle != nullptr ){
        lifecycle->setNodeTimeout(nodeIndex, value);
    }
    return *this;
}

ESPLifecycle::NodeBuilder& ESPLifecycle::NodeBuilder::reloadScope(uint32_t scopeBit) {
    if( lifecycle != nullptr ){
        lifecycle->setNodeReloadScope(nodeIndex, scopeBit);
    }
    return *this;
}

ESPLifecycle::NodeBuilder& ESPLifecycle::NodeBuilder::optional(bool isOptional) {
    if( lifecycle != nullptr ){
        lifecycle->setNodeOptional(nodeIndex, isOptional);
    }
    return *this;
}

ESPLifecycle::SectionBuilder::SectionBuilder(ESPLifecycle* lifecycleRef, size_t sectionIndexRef)
    : lifecycle(lifecycleRef), sectionIndex(sectionIndexRef) {}

ESPLifecycle::SectionBuilder& ESPLifecycle::SectionBuilder::mode(LifecycleSectionMode sectionMode) {
    if( lifecycle != nullptr ){
        lifecycle->setSectionMode(sectionIndex, sectionMode);
    }
    return *this;
}

ESPLifecycle::SectionBuilder& ESPLifecycle::SectionBuilder::readiness(
    std::function<bool()> isReady,
    std::function<void(TickType_t)> waitFn
) {
    if( lifecycle != nullptr ){
        lifecycle->setSectionReadiness(sectionIndex, isReady, waitFn);
    }
    return *this;
}

ESPLifecycle& ESPLifecycle::init(std::initializer_list<const char*> sectionNames) {
    if( sections.empty() ){
        for( const char* sectionName : sectionNames ){
            ensureSection(sectionName);
        }
    }

    if( sections.empty() ){
        ensureSection("default");
    }

    return *this;
}

ESPLifecycle::SectionBuilder& ESPLifecycle::section(const char* sectionName) {
    const size_t index = ensureSection(sectionName);
    sectionBuilder = SectionBuilder(this, index);
    return sectionBuilder;
}

ESPLifecycle::NodeBuilder& ESPLifecycle::addTo(
    const char* section,
    const char* nodeName,
    std::function<bool()> initFn,
    std::function<bool()> teardownFn
) {
    const size_t sectionIndex = ensureSection(section);

    LifecycleNodeDefinition node{};
    if( nodeName != nullptr ){
        node.name = nodeName;
    }
    node.sectionIndex = sectionIndex;
    node.initFn = std::move(initFn);
    node.teardownFn = std::move(teardownFn);
    node.timeoutMs = config.defaultStepTimeoutMs;
    nodes.push_back(std::move(node));

    nodeBuilder = NodeBuilder(this, nodes.size() - 1);
    graphBuilt = false;
    return nodeBuilder;
}

bool ESPLifecycle::configure(const LifecycleConfig& configValue) {
    config = configValue;
    return true;
}

void ESPLifecycle::clear() {
    stopScopeListener();

    {
        std::lock_guard<std::mutex> lock(transitionMutex);
        nodes.clear();
        sections.clear();
        topologicalOrder.clear();
        initialized.clear();
        graphBuilt = false;
    }

    setState(LifecycleState::Idle, nullptr);
    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        snapshotValue.completed = 0;
        snapshotValue.total = 0;
        snapshotValue.failed = false;
        snapshotValue.errorCode = LifecycleErrorCode::None;
        detailText.clear();
        nodeNameText.clear();
    }
    publishSnapshot();
}

LifecycleState ESPLifecycle::state() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return currentState;
}

size_t ESPLifecycle::ensureSection(const char* sectionName) {
    const std::string name = sectionName == nullptr ? "" : sectionName;
    for( size_t index = 0; index < sections.size(); index++ ){
        if( sections[index].name == name ){
            return index;
        }
    }

    LifecycleSectionDefinition sectionDef{};
    sectionDef.name = name;
    sections.push_back(std::move(sectionDef));
    graphBuilt = false;
    return sections.size() - 1;
}

void ESPLifecycle::addDependencyName(size_t nodeIndex, const char* dependencyNodeName) {
    if( dependencyNodeName == nullptr || nodeIndex >= nodes.size() ){
        return;
    }
    nodes[nodeIndex].dependenciesByName.emplace_back(dependencyNodeName);
    graphBuilt = false;
}

void ESPLifecycle::addDependentName(size_t nodeIndex, const char* dependentNodeName) {
    if( dependentNodeName == nullptr || nodeIndex >= nodes.size() ){
        return;
    }
    nodes[nodeIndex].dependentsByName.emplace_back(dependentNodeName);
    graphBuilt = false;
}

void ESPLifecycle::setNodeTimeout(size_t nodeIndex, uint32_t value) {
    if( nodeIndex >= nodes.size() ){
        return;
    }
    nodes[nodeIndex].timeoutMs = value;
}

void ESPLifecycle::setNodeReloadScope(size_t nodeIndex, uint32_t scopeBit) {
    if( nodeIndex >= nodes.size() ){
        return;
    }
    nodes[nodeIndex].reloadScopeMask = scopeBit;
}

void ESPLifecycle::setNodeOptional(size_t nodeIndex, bool isOptional) {
    if( nodeIndex >= nodes.size() ){
        return;
    }
    nodes[nodeIndex].optional = isOptional;
}

void ESPLifecycle::setSectionMode(size_t sectionIndex, LifecycleSectionMode mode) {
    if( sectionIndex >= sections.size() ){
        return;
    }
    sections[sectionIndex].mode = mode;
    graphBuilt = false;
}

void ESPLifecycle::setSectionReadiness(
    size_t sectionIndex,
    std::function<bool()> isReady,
    std::function<void(TickType_t)> waitFn
) {
    if( sectionIndex >= sections.size() ){
        return;
    }
    sections[sectionIndex].readinessCheck = std::move(isReady);
    sections[sectionIndex].waitFn = std::move(waitFn);
    graphBuilt = false;
}

LifecycleResult ESPLifecycle::okResult(const char* detail) {
    detailText = detail == nullptr ? "" : detail;
    LifecycleResult result{};
    result.ok = true;
    result.code = LifecycleErrorCode::None;
    result.nodeName = nullptr;
    result.detail = detailText.empty() ? nullptr : detailText.c_str();
    return result;
}

LifecycleResult ESPLifecycle::failResult(
    LifecycleErrorCode code,
    const char* nodeName,
    const char* detail,
    bool updateFailedState
) {
    nodeNameText = nodeName == nullptr ? "" : nodeName;
    detailText = detail == nullptr ? "" : detail;

    if( updateFailedState ){
        setState(LifecycleState::Failed, nodeNameText.empty() ? nullptr : nodeNameText.c_str());
    }

    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        snapshotValue.failed = true;
        snapshotValue.errorCode = code;
        snapshotValue.updatedAtMs = nowMs();
    }
    publishSnapshot();

    LifecycleResult result{};
    result.ok = false;
    result.code = code;
    result.nodeName = nodeNameText.empty() ? nullptr : nodeNameText.c_str();
    result.detail = detailText.empty() ? nullptr : detailText.c_str();
    return result;
}

std::vector<size_t> ESPLifecycle::allNodeIndexes() const {
    std::vector<size_t> indexes;
    indexes.reserve(nodes.size());
    for( size_t i = 0; i < nodes.size(); i++ ){
        indexes.push_back(i);
    }
    return indexes;
}

std::vector<size_t> ESPLifecycle::reverseTopologicalSubset(const std::vector<size_t>& subset) const {
    std::vector<size_t> reversed;
    reversed.reserve(subset.size());

    std::vector<bool> selected(nodes.size(), false);
    for( size_t index : subset ){
        if( index < selected.size() ){
            selected[index] = true;
        }
    }

    for( auto it = topologicalOrder.rbegin(); it != topologicalOrder.rend(); ++it ){
        if( *it < selected.size() && selected[*it] ){
            reversed.push_back(*it);
        }
    }

    return reversed;
}

void ESPLifecycle::log(LifecycleLogLevel level, const char* message) const {
    if( config.logger ){
        config.logger(level, message == nullptr ? "" : message);
    }
}

uint32_t ESPLifecycle::nowMs() const {
#if __has_include(<Arduino.h>)
    return static_cast<uint32_t>(millis());
#else
    return 0;
#endif
}
