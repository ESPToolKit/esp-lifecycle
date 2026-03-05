#pragma once

#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

#include <ArduinoJson.h>

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
        NodeBuilder& optional(bool isOptional);
        NodeBuilder& parallelSafe(bool enabled = true);

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
    LifecycleResult deinitialize(std::initializer_list<const char*> nodeNames);
    LifecycleResult deinitialize(const std::vector<const char*>& nodeNames);

    LifecycleResult reinitializeAll();
    LifecycleResult reinitialize(std::initializer_list<const char*> nodeNames);
    LifecycleResult reinitialize(const std::vector<const char*>& nodeNames);

    bool startReloadListener(
        ESPEventBus& eventBus,
        uint16_t eventId,
        std::function<std::vector<const char*>(void*)> payloadToNodeNames
    );
    void stopReloadListener();

    LifecycleState state() const;
    LifecycleSnapshot snapshot() const;
    JsonDocument snapshotJson() const;
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
    bool lastOperationOk = true;
    std::string phaseText = "idle";

    NodeBuilder nodeBuilder = {};
    SectionBuilder sectionBuilder = {};

    mutable std::mutex transitionMutex;

    mutable std::mutex listenerMutex;
    ESPEventBus* listenerBus = nullptr;
    uint32_t listenerSubId = 0;
    uint16_t listenerEventId = 0;
    std::function<std::vector<const char*>(void*)> listenerPayloadNamesFn;
    std::vector<std::string> pendingNodeNames = {};
    bool listenerWorkerRunning = false;
    uint32_t listenerCoalesceMs = 25;

    size_t ensureSection(const char* sectionName);
    void addDependencyName(size_t nodeIndex, const char* dependencyNodeName);
    void addDependentName(size_t nodeIndex, const char* dependentNodeName);
    void setNodeTimeout(size_t nodeIndex, uint32_t value);
    void setNodeOptional(size_t nodeIndex, bool isOptional);
    void setNodeParallelSafe(size_t nodeIndex, bool enabled);
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

    LifecycleResult initializeInternal(
        const std::vector<size_t>& subset,
        LifecycleState transitionState,
        bool enableParallel
    );
    LifecycleResult deinitializeInternal(
        const std::vector<size_t>& subset,
        bool updateState,
        bool enableParallel
    );
    LifecycleResult runSectionInitialize(
        size_t sectionIndex,
        const std::vector<size_t>& subset,
        bool enableParallel
    );
    LifecycleResult runPhaseBatches(
        const std::vector<std::vector<size_t>>& batches,
        bool initializePhase,
        bool enableParallel
    );
    LifecycleResult runParallelBatch(const std::vector<size_t>& batch, bool initializePhase);
    LifecycleResult runNodeInit(size_t nodeIndex, bool countProgress = true);
    LifecycleResult runNodeTeardown(size_t nodeIndex, bool countProgress = true);
    std::vector<std::vector<size_t>> buildWavesForSubset(
        const std::vector<size_t>& subset,
        bool initializePhase
    ) const;
    bool requiresParallelWorkerForBatch(const std::vector<size_t>& batch) const;
    bool isParallelEligible(size_t nodeIndex) const;

    std::vector<size_t> allNodeIndexes() const;
    LifecycleResult resolveNodeNamesToSubset(
        const std::vector<const char*>& nodeNames,
        std::vector<size_t>& outSubset
    );
    LifecycleResult expandSubsetWithDependents(std::vector<size_t>& subset);
    LifecycleResult expandSubsetWithDependencies(std::vector<size_t>& subset);
    LifecycleResult expandSubsetForReinitialize(std::vector<size_t>& subset);

    void publishSnapshot();
    void setState(LifecycleState stateValue, const char* activeNode = nullptr);
    void setPhase(const char* phaseName);
    void markProgress(const char* activeNode, bool completedStep);
    uint32_t nowMs() const;

    void log(LifecycleLogLevel level, const char* message) const;

    void scheduleNodeReinitialize(const std::vector<const char*>& nodeNames);
    void listenerWorkerLoop();
};
