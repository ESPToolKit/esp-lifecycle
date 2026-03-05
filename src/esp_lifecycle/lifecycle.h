#pragma once

#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

#include "internal/GraphTypes.h"

class ESPEventBus;

class ESPLifecycle {
  public:
    class NodeBuilder {
      public:
        NodeBuilder() = default;
        NodeBuilder(ESPLifecycle* lifecycleRef, size_t nodeIndexRef);

        NodeBuilder& after(const char* dependencyNodeName);
        NodeBuilder& before(const char* dependentNodeName);
        NodeBuilder& timeoutMs(uint32_t value);
        NodeBuilder& reloadScope(uint32_t scopeBit);
        NodeBuilder& optional(bool isOptional);

      private:
        ESPLifecycle* lifecycle = nullptr;
        size_t nodeIndex = 0;
    };

    class SectionBuilder {
      public:
        SectionBuilder() = default;
        SectionBuilder(ESPLifecycle* lifecycleRef, size_t sectionIndexRef);

        SectionBuilder& mode(LifecycleSectionMode sectionMode);
        SectionBuilder& readiness(std::function<bool()> isReady, std::function<void(TickType_t)> waitFn);

      private:
        ESPLifecycle* lifecycle = nullptr;
        size_t sectionIndex = 0;
    };

    ESPLifecycle() = default;

    ESPLifecycle& init(std::initializer_list<const char*> sectionNames);
    SectionBuilder& section(const char* sectionName);
    NodeBuilder& addTo(
        const char* section,
        const char* nodeName,
        std::function<bool()> initFn,
        std::function<bool()> teardownFn
    );

    bool configure(const LifecycleConfig& config);
    LifecycleResult build();
    LifecycleResult initialize();
    LifecycleResult deinitialize();
    LifecycleResult reinitializeAll();
    LifecycleResult reinitializeByScopeMask(uint32_t scopeMask);
    LifecycleResult reinitializeByNodeNames(const std::vector<const char*>& nodeNames);

    bool startScopeListener(
        ESPEventBus& eventBus,
        uint16_t eventId,
        std::function<uint32_t(void*)> payloadToScopeMask
    );
    void stopScopeListener();

    LifecycleState state() const;
    void clear();

  private:
    LifecycleConfig config = {};

    std::vector<LifecycleSectionDefinition> sections = {};
    std::vector<LifecycleNodeDefinition> nodes = {};
    std::vector<size_t> topologicalOrder = {};
    std::vector<bool> initialized = {};
    bool graphBuilt = false;

    mutable std::mutex stateMutex;
    LifecycleState currentState = LifecycleState::Idle;

    mutable std::mutex snapshotMutex;
    LifecycleSnapshot snapshotValue = {};
    std::string activeNodeText = {};
    std::string detailText = {};
    std::string nodeNameText = {};

    NodeBuilder nodeBuilder = {};
    SectionBuilder sectionBuilder = {};

    mutable std::mutex transitionMutex;

    mutable std::mutex listenerMutex;
    ESPEventBus* listenerBus = nullptr;
    uint32_t listenerSubId = 0;
    uint16_t listenerEventId = 0;
    std::function<uint32_t(void*)> listenerPayloadMaskFn;
    uint32_t pendingScopeMask = 0;
    bool listenerWorkerRunning = false;
    uint32_t listenerCoalesceMs = 25;

    size_t ensureSection(const char* sectionName);
    void addDependencyName(size_t nodeIndex, const char* dependencyNodeName);
    void addDependentName(size_t nodeIndex, const char* dependentNodeName);
    void setNodeTimeout(size_t nodeIndex, uint32_t value);
    void setNodeReloadScope(size_t nodeIndex, uint32_t scopeBit);
    void setNodeOptional(size_t nodeIndex, bool isOptional);
    void setSectionMode(size_t sectionIndex, LifecycleSectionMode mode);
    void setSectionReadiness(
        size_t sectionIndex,
        std::function<bool()> isReady,
        std::function<void(TickType_t)> waitFn
    );

    LifecycleResult failResult(
        LifecycleErrorCode code,
        const char* nodeName,
        const char* detail,
        bool updateFailedState
    );
    LifecycleResult okResult(const char* detail = nullptr);

    LifecycleResult validateAndBuildGraph();
    LifecycleResult resolveIndexes();
    LifecycleResult ensureNoCycles();

    LifecycleResult initializeInternal(const std::vector<size_t>& subset, LifecycleState transitionState);
    LifecycleResult deinitializeInternal(const std::vector<size_t>& subset, bool updateState);
    LifecycleResult runSection(size_t sectionIndex, const std::vector<size_t>& subset);
    LifecycleResult runNodeInit(size_t nodeIndex);
    LifecycleResult runNodeTeardown(size_t nodeIndex);

    std::vector<size_t> allNodeIndexes() const;
    std::vector<size_t> resolveScopeSubset(uint32_t scopeMask, bool* knownScope = nullptr) const;
    LifecycleResult expandSubsetWithClosure(std::vector<size_t>& subset);
    std::vector<size_t> reverseTopologicalSubset(const std::vector<size_t>& subset) const;

    void publishSnapshot();
    void setState(LifecycleState stateValue, const char* activeNode = nullptr);
    void markProgress(const char* activeNode, bool completedStep);
    uint32_t nowMs() const;

    void log(LifecycleLogLevel level, const char* message) const;

    void scheduleScopeReinitialize(uint32_t scopeMask);
    void listenerWorkerLoop();
};
