#include "esp_lifecycle/lifecycle.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
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

namespace {

bool anyInitializedNode(const std::vector<bool> &initialized) {
	for (bool value : initialized) {
		if (value) {
			return true;
		}
	}
	return false;
}

} // namespace

LifecycleResult ESPLifecycle::initialize() {
	std::unique_lock<std::mutex> lock(transitionMutex, std::try_to_lock);
	if (!lock.owns_lock()) {
		return failResult(LifecycleErrorCode::Busy, nullptr, "lifecycle is busy", false);
	}

	if (!graphBuilt) {
		LifecycleResult buildResult = validateAndBuildGraph();
		if (!buildResult.ok) {
			return buildResult;
		}
	}

	if (state() == LifecycleState::Running) {
		return okResult("already running");
	}

	setPhase("initialize");

	if (config.onInitStarted) {
		config.onInitStarted();
	}

	LifecycleResult initResult = initializeInternal(
	    allNodeIndexes(),
	    LifecycleState::Initializing,
	    config.enableParallelInit
	);
	if (!initResult.ok) {
		if (config.onInitFailed) {
			config.onInitFailed();
		}
		return initResult;
	}

	setState(LifecycleState::Running, nullptr);
	setPhase("idle");
	if (config.onReady) {
		config.onReady();
	}

	return okResult("initialized");
}

LifecycleResult ESPLifecycle::deinitialize() {
	std::unique_lock<std::mutex> lock(transitionMutex, std::try_to_lock);
	if (!lock.owns_lock()) {
		return failResult(LifecycleErrorCode::Busy, nullptr, "lifecycle is busy", false);
	}

	if (!anyInitializedNode(initialized)) {
		setState(LifecycleState::Idle, nullptr);
		setPhase("idle");
		return okResult("deinitialize no-op");
	}

	setPhase("deinitialize");
	LifecycleResult result =
	    deinitializeInternal(allNodeIndexes(), true, config.enableParallelDeinit);
	if (!result.ok) {
		setState(LifecycleState::Failed, result.nodeName);
		return result;
	}

	setState(LifecycleState::Idle, nullptr);
	setPhase("idle");
	return okResult("deinitialized");
}

LifecycleResult ESPLifecycle::deinitialize(const std::vector<const char *> &nodeNames) {
	std::unique_lock<std::mutex> lock(transitionMutex, std::try_to_lock);
	if (!lock.owns_lock()) {
		return failResult(LifecycleErrorCode::Busy, nullptr, "lifecycle is busy", false);
	}

	if (!graphBuilt) {
		LifecycleResult buildResult = validateAndBuildGraph();
		if (!buildResult.ok) {
			return buildResult;
		}
	}

	std::vector<size_t> subset;
	LifecycleResult resolveResult = resolveNodeNamesToSubset(nodeNames, subset);
	if (!resolveResult.ok) {
		return resolveResult;
	}

	LifecycleResult closureResult = expandSubsetWithDependents(subset);
	if (!closureResult.ok) {
		return closureResult;
	}

	setPhase("deinitialize");
	LifecycleResult result = deinitializeInternal(subset, true, config.enableParallelDeinit);
	if (!result.ok) {
		setState(LifecycleState::Failed, result.nodeName);
		return result;
	}

	if (anyInitializedNode(initialized)) {
		setState(LifecycleState::Running, nullptr);
	} else {
		setState(LifecycleState::Idle, nullptr);
	}
	setPhase("idle");
	return okResult("deinitialized nodes");
}

LifecycleResult ESPLifecycle::reinitializeAll() {
	std::unique_lock<std::mutex> lock(transitionMutex, std::try_to_lock);
	if (!lock.owns_lock()) {
		return failResult(LifecycleErrorCode::Busy, nullptr, "lifecycle is busy", false);
	}

	if (!graphBuilt) {
		LifecycleResult buildResult = validateAndBuildGraph();
		if (!buildResult.ok) {
			return buildResult;
		}
	}

	setPhase("reinitialize");
	setState(LifecycleState::Reinitializing, nullptr);

	std::vector<size_t> subset = allNodeIndexes();
	LifecycleResult deinitResult = deinitializeInternal(subset, false, config.enableParallelReinit);
	if (!deinitResult.ok) {
		setState(LifecycleState::Failed, deinitResult.nodeName);
		return deinitResult;
	}

	LifecycleResult initResult =
	    initializeInternal(subset, LifecycleState::Reinitializing, config.enableParallelReinit);
	if (!initResult.ok) {
		if (config.onInitFailed) {
			config.onInitFailed();
		}
		return initResult;
	}

	setState(LifecycleState::Running, nullptr);
	setPhase("idle");
	if (config.onReady) {
		config.onReady();
	}

	return okResult("reinitialized all");
}

LifecycleResult ESPLifecycle::reinitialize(const std::vector<const char *> &nodeNames) {
	std::unique_lock<std::mutex> lock(transitionMutex, std::try_to_lock);
	if (!lock.owns_lock()) {
		return failResult(LifecycleErrorCode::Busy, nullptr, "lifecycle is busy", false);
	}

	if (!graphBuilt) {
		LifecycleResult buildResult = validateAndBuildGraph();
		if (!buildResult.ok) {
			return buildResult;
		}
	}

	std::vector<size_t> subset;
	LifecycleResult resolveResult = resolveNodeNamesToSubset(nodeNames, subset);
	if (!resolveResult.ok) {
		return resolveResult;
	}

	if (config.dependencyReinitialization) {
		LifecycleResult closureResult = expandSubsetForReinitialize(subset);
		if (!closureResult.ok) {
			return closureResult;
		}
	}

	setPhase("reinitialize");
	setState(LifecycleState::Reinitializing, nullptr);

	LifecycleResult deinitResult = deinitializeInternal(subset, false, config.enableParallelReinit);
	if (!deinitResult.ok) {
		setState(LifecycleState::Failed, deinitResult.nodeName);
		return deinitResult;
	}

	LifecycleResult initResult =
	    initializeInternal(subset, LifecycleState::Reinitializing, config.enableParallelReinit);
	if (!initResult.ok) {
		if (config.onInitFailed) {
			config.onInitFailed();
		}
		return initResult;
	}

	setState(LifecycleState::Running, nullptr);
	setPhase("idle");
	return okResult("reinitialized nodes");
}

LifecycleResult ESPLifecycle::initializeInternal(
    const std::vector<size_t> &subset, LifecycleState transitionState, bool enableParallel
) {
	setState(transitionState, nullptr);

	std::vector<bool> selected(nodes.size(), false);
	for (size_t index : subset) {
		if (index < selected.size()) {
			selected[index] = true;
		}
	}

	for (size_t sectionIndex = 0; sectionIndex < sections.size(); sectionIndex++) {
		LifecycleResult sectionResult = runSectionInitialize(sectionIndex, subset, enableParallel);
		if (!sectionResult.ok) {
			if (config.rollbackOnInitFailure) {
				std::vector<size_t> rollbackSubset;
				rollbackSubset.reserve(nodes.size());
				for (size_t i = 0; i < nodes.size(); i++) {
					if (initialized[i] && selected[i]) {
						rollbackSubset.push_back(i);
					}
				}
				(void)deinitializeInternal(rollbackSubset, false, config.enableParallelDeinit);
			}

			return sectionResult;
		}
	}

	return okResult();
}

LifecycleResult ESPLifecycle::runSectionInitialize(
    size_t sectionIndex, const std::vector<size_t> &subset, bool enableParallel
) {
	if (sectionIndex >= sections.size()) {
		return failResult(
		    LifecycleErrorCode::InvalidSection,
		    nullptr,
		    "section index out of bounds",
		    true
		);
	}

	const LifecycleSectionDefinition &sectionDef = sections[sectionIndex];
	if (sectionDef.mode == LifecycleSectionMode::Deferred) {
		while (true) {
			if (sectionDef.readinessCheck && sectionDef.readinessCheck()) {
				break;
			}

			if (!sectionDef.waitFn) {
				return failResult(
				    LifecycleErrorCode::InvalidSection,
				    nullptr,
				    "deferred section wait callback missing",
				    true
				);
			}

			sectionDef.waitFn(config.waitTicks);
		}
	}

	std::vector<size_t> sectionSubset;
	sectionSubset.reserve(subset.size());
	for (size_t nodeIndex : subset) {
		if (nodeIndex < nodes.size() && nodes[nodeIndex].sectionIndex == sectionIndex) {
			sectionSubset.push_back(nodeIndex);
		}
	}

	if (sectionSubset.empty()) {
		return okResult();
	}

	std::vector<std::vector<size_t>> batches = buildWavesForSubset(sectionSubset, true);
	if (!sectionSubset.empty() && batches.empty()) {
		return failResult(
		    LifecycleErrorCode::CycleDetected,
		    nullptr,
		    "unable to build initialize waves",
		    true
		);
	}
	return runPhaseBatches(batches, true, enableParallel);
}

LifecycleResult ESPLifecycle::deinitializeInternal(
    const std::vector<size_t> &subset, bool updateState, bool enableParallel
) {
	if (updateState) {
		setState(LifecycleState::Deinitializing, nullptr);
	}

	std::vector<size_t> teardownSubset;
	teardownSubset.reserve(subset.size());
	for (size_t nodeIndex : subset) {
		if (nodeIndex < initialized.size() && initialized[nodeIndex]) {
			teardownSubset.push_back(nodeIndex);
		}
	}

	if (teardownSubset.empty()) {
		return okResult();
	}

	std::vector<std::vector<size_t>> batches = buildWavesForSubset(teardownSubset, false);
	if (!teardownSubset.empty() && batches.empty()) {
		return failResult(
		    LifecycleErrorCode::CycleDetected,
		    nullptr,
		    "unable to build deinitialize waves",
		    true
		);
	}
	return runPhaseBatches(batches, false, enableParallel);
}

LifecycleResult ESPLifecycle::runPhaseBatches(
    const std::vector<std::vector<size_t>> &batches, bool initializePhase, bool enableParallel
) {
	for (const std::vector<size_t> &batch : batches) {
		if (batch.empty()) {
			continue;
		}

		if (!enableParallel) {
			for (size_t nodeIndex : batch) {
				LifecycleResult result = initializePhase ? runNodeInit(nodeIndex, true)
				                                         : runNodeTeardown(nodeIndex, true);

				if (result.ok) {
					continue;
				}

				if (initializePhase && nodes[nodeIndex].optional) {
					log(LifecycleLogLevel::Warn, "optional node init failed, continuing");
					continue;
				}

				if (!initializePhase && config.continueTeardownOnFailure) {
					log(LifecycleLogLevel::Warn, "teardown failed, continuing due to policy");
					continue;
				}

				return result;
			}
			continue;
		}

		std::vector<size_t> parallelEligible;
		std::vector<size_t> sequentialOnly;
		parallelEligible.reserve(batch.size());
		sequentialOnly.reserve(batch.size());
		for (size_t nodeIndex : batch) {
			if (isParallelEligible(nodeIndex)) {
				parallelEligible.push_back(nodeIndex);
			} else {
				sequentialOnly.push_back(nodeIndex);
			}
		}

		if (parallelEligible.size() < 2) {
			for (size_t nodeIndex : batch) {
				LifecycleResult result = initializePhase ? runNodeInit(nodeIndex, true)
				                                         : runNodeTeardown(nodeIndex, true);

				if (result.ok) {
					continue;
				}

				if (initializePhase && nodes[nodeIndex].optional) {
					log(LifecycleLogLevel::Warn, "optional node init failed, continuing");
					continue;
				}

				if (!initializePhase && config.continueTeardownOnFailure) {
					log(LifecycleLogLevel::Warn, "teardown failed, continuing due to policy");
					continue;
				}

				return result;
			}
			continue;
		}

		LifecycleResult parallelResult = runParallelBatch(parallelEligible, initializePhase);
		if (!parallelResult.ok) {
			if (initializePhase && parallelResult.nodeName != nullptr) {
				auto it = std::find_if(
				    nodes.begin(),
				    nodes.end(),
				    [&](const LifecycleNodeDefinition &node) {
					    return node.name == parallelResult.nodeName;
				    }
				);
				if (it != nodes.end() && it->optional) {
					log(LifecycleLogLevel::Warn,
					    "optional node init failed in parallel batch, continuing");
					continue;
				}
			}

			if (!initializePhase && config.continueTeardownOnFailure) {
				log(LifecycleLogLevel::Warn, "parallel teardown failed, continuing due to policy");
				continue;
			}

			return parallelResult;
		}

		for (size_t nodeIndex : sequentialOnly) {
			LifecycleResult result =
			    initializePhase ? runNodeInit(nodeIndex, true) : runNodeTeardown(nodeIndex, true);

			if (result.ok) {
				continue;
			}

			if (initializePhase && nodes[nodeIndex].optional) {
				log(LifecycleLogLevel::Warn, "optional node init failed, continuing");
				continue;
			}

			if (!initializePhase && config.continueTeardownOnFailure) {
				log(LifecycleLogLevel::Warn, "teardown failed, continuing due to policy");
				continue;
			}

			return result;
		}
	}

	return okResult();
}

LifecycleResult
ESPLifecycle::runParallelBatch(const std::vector<size_t> &batch, bool initializePhase) {
	if (config.worker == nullptr) {
		return failResult(
		    LifecycleErrorCode::InvalidConfig,
		    nullptr,
		    "parallel execution requires config.worker",
		    true
		);
	}

	struct BatchResult {
		size_t nodeIndex = 0;
		bool ok = false;
	};

	std::vector<std::shared_ptr<WorkerHandler>> handlers;
	handlers.reserve(batch.size());

	std::mutex resultMutex;
	std::vector<BatchResult> results;
	results.reserve(batch.size());

	for (size_t nodeIndex : batch) {
		if (nodeIndex >= nodes.size()) {
			return failResult(
			    LifecycleErrorCode::InvalidConfig,
			    nullptr,
			    "node index out of bounds",
			    true
			);
		}

		WorkerConfig workerConfig{};
		workerConfig.stackSizeBytes = config.workerStackSizeBytes;
		if (config.workerName != nullptr) {
			workerConfig.name = std::string(config.workerName) + "-parallel";
		} else {
			workerConfig.name = "lifecycle-parallel";
		}

		setState(state(), nodes[nodeIndex].name.c_str());

		WorkerResult workerResult = config.worker->spawn(
		    [this, nodeIndex, initializePhase, &resultMutex, &results]() {
			    bool ok = false;
			    if (initializePhase) {
				    if (nodes[nodeIndex].initFn) {
					    ok = nodes[nodeIndex].initFn();
				    }
			    } else {
				    if (nodes[nodeIndex].teardownFn) {
					    ok = nodes[nodeIndex].teardownFn();
				    }
			    }

			    std::lock_guard<std::mutex> lock(resultMutex);
			    results.push_back(BatchResult{nodeIndex, ok});
		    },
		    workerConfig
		);

		if (workerResult.error != WorkerError::None || workerResult.handler == nullptr) {
			return failResult(
			    LifecycleErrorCode::InvalidConfig,
			    nodes[nodeIndex].name.c_str(),
			    "failed to start parallel worker",
			    true
			);
		}

		handlers.push_back(workerResult.handler);
	}

	for (const auto &handler : handlers) {
		if (handler == nullptr) {
			continue;
		}

		const bool workerStopped = handler->wait(pdMS_TO_TICKS(1000));
		if (!workerStopped) {
			handler->destroy();
			handler->wait(pdMS_TO_TICKS(250));
		}
	}

	std::vector<BatchResult> collected;
	{
		std::lock_guard<std::mutex> lock(resultMutex);
		collected = results;
	}

	for (size_t nodeIndex : batch) {
		auto it = std::find_if(collected.begin(), collected.end(), [&](const BatchResult &result) {
			return result.nodeIndex == nodeIndex;
		});

		if (it == collected.end()) {
			return failResult(
			    LifecycleErrorCode::InvalidConfig,
			    nodes[nodeIndex].name.c_str(),
			    "parallel batch missing node result",
			    true
			);
		}

		if (it->ok) {
			initialized[nodeIndex] = initializePhase;
			markProgress(nodes[nodeIndex].name.c_str(), true);
			continue;
		}

		if (initializePhase && nodes[nodeIndex].optional) {
			log(LifecycleLogLevel::Warn, "optional node init failed in parallel batch");
			continue;
		}

		if (!initializePhase && config.continueTeardownOnFailure) {
			log(LifecycleLogLevel::Warn,
			    "teardown failed in parallel batch, continuing due to policy");
			continue;
		}

		return failResult(
		    initializePhase ? LifecycleErrorCode::InitFailed : LifecycleErrorCode::TeardownFailed,
		    nodes[nodeIndex].name.c_str(),
		    initializePhase ? "node init failed" : "node teardown failed",
		    true
		);
	}

	return okResult();
}

LifecycleResult ESPLifecycle::runNodeInit(size_t nodeIndex, bool countProgress) {
	if (nodeIndex >= nodes.size()) {
		return failResult(
		    LifecycleErrorCode::InvalidConfig,
		    nullptr,
		    "node index out of bounds",
		    true
		);
	}

	const LifecycleNodeDefinition &node = nodes[nodeIndex];
	setState(state(), node.name.c_str());

	if (!node.initFn) {
		return failResult(
		    LifecycleErrorCode::InvalidConfig,
		    node.name.c_str(),
		    "init callback missing",
		    true
		);
	}

	if (!node.initFn()) {
		return failResult(
		    LifecycleErrorCode::InitFailed,
		    node.name.c_str(),
		    "node init failed",
		    true
		);
	}

	initialized[nodeIndex] = true;
	if (countProgress) {
		markProgress(node.name.c_str(), true);
	}
	return okResult();
}

LifecycleResult ESPLifecycle::runNodeTeardown(size_t nodeIndex, bool countProgress) {
	if (nodeIndex >= nodes.size()) {
		return failResult(
		    LifecycleErrorCode::InvalidConfig,
		    nullptr,
		    "node index out of bounds",
		    true
		);
	}

	const LifecycleNodeDefinition &node = nodes[nodeIndex];
	setState(state(), node.name.c_str());

	if (!node.teardownFn) {
		return failResult(
		    LifecycleErrorCode::InvalidConfig,
		    node.name.c_str(),
		    "teardown callback missing",
		    true
		);
	}

	if (!node.teardownFn()) {
		return failResult(
		    LifecycleErrorCode::TeardownFailed,
		    node.name.c_str(),
		    "node teardown failed",
		    true
		);
	}

	initialized[nodeIndex] = false;
	if (countProgress) {
		markProgress(node.name.c_str(), true);
	}
	return okResult();
}

std::vector<std::vector<size_t>>
ESPLifecycle::buildWavesForSubset(const std::vector<size_t> &subset, bool initializePhase) const {
	std::vector<std::vector<size_t>> waves;
	if (subset.empty()) {
		return waves;
	}

	std::vector<bool> selected(nodes.size(), false);
	for (size_t index : subset) {
		if (index < selected.size()) {
			selected[index] = true;
		}
	}

	std::vector<size_t> indegree(nodes.size(), 0);
	std::vector<std::vector<size_t>> outgoing(nodes.size());

	for (size_t nodeIndex : subset) {
		if (nodeIndex >= nodes.size()) {
			continue;
		}

		if (initializePhase) {
			for (size_t dep : nodes[nodeIndex].dependencyIndexes) {
				if (dep < selected.size() && selected[dep]) {
					indegree[nodeIndex]++;
					outgoing[dep].push_back(nodeIndex);
				}
			}
		} else {
			for (size_t dependent : nodes[nodeIndex].reverseDependencyIndexes) {
				if (dependent < selected.size() && selected[dependent]) {
					indegree[nodeIndex]++;
					outgoing[dependent].push_back(nodeIndex);
				}
			}
		}
	}

	std::unordered_map<size_t, size_t> topoPos;
	topoPos.reserve(topologicalOrder.size());
	for (size_t i = 0; i < topologicalOrder.size(); i++) {
		topoPos[topologicalOrder[i]] = i;
	}

	std::vector<size_t> ready;
	ready.reserve(subset.size());
	for (size_t nodeIndex : subset) {
		if (nodeIndex < indegree.size() && indegree[nodeIndex] == 0) {
			ready.push_back(nodeIndex);
		}
	}

	auto waveOrder = [&](const size_t left, const size_t right) {
		const size_t leftPos = topoPos.count(left) != 0 ? topoPos.at(left) : left;
		const size_t rightPos = topoPos.count(right) != 0 ? topoPos.at(right) : right;
		if (initializePhase) {
			return leftPos < rightPos;
		}
		return leftPos > rightPos;
	};

	size_t visitedCount = 0;
	while (!ready.empty()) {
		std::sort(ready.begin(), ready.end(), waveOrder);

		std::vector<size_t> wave = ready;
		waves.push_back(wave);
		ready.clear();

		visitedCount += wave.size();

		for (size_t nodeIndex : wave) {
			for (size_t target : outgoing[nodeIndex]) {
				if (target >= indegree.size() || indegree[target] == 0) {
					continue;
				}

				indegree[target]--;
				if (indegree[target] == 0) {
					ready.push_back(target);
				}
			}
		}
	}

	if (visitedCount != subset.size()) {
		return {};
	}

	return waves;
}

bool ESPLifecycle::isParallelEligible(size_t nodeIndex) const {
	if (nodeIndex >= nodes.size()) {
		return false;
	}

	const LifecycleNodeDefinition &node = nodes[nodeIndex];
	return node.parallelSafe || node.dependencyIndexes.empty();
}

bool ESPLifecycle::requiresParallelWorkerForBatch(const std::vector<size_t> &batch) const {
	size_t eligible = 0;
	for (size_t nodeIndex : batch) {
		if (isParallelEligible(nodeIndex)) {
			eligible++;
		}
	}
	return eligible >= 2;
}

LifecycleResult ESPLifecycle::resolveNodeNamesToSubset(
    const std::vector<const char *> &nodeNames, std::vector<size_t> &outSubset
) {
	std::unordered_map<std::string, size_t> nodeByName;
	nodeByName.reserve(nodes.size());
	for (size_t i = 0; i < nodes.size(); i++) {
		nodeByName[nodes[i].name] = i;
	}

	outSubset.clear();
	outSubset.reserve(nodeNames.size());

	for (const char *nodeName : nodeNames) {
		if (nodeName == nullptr) {
			continue;
		}

		auto it = nodeByName.find(nodeName);
		if (it == nodeByName.end()) {
			return failResult(
			    LifecycleErrorCode::UnknownNode,
			    nodeName,
			    "unknown node name",
			    false
			);
		}

		outSubset.push_back(it->second);
	}

	std::sort(outSubset.begin(), outSubset.end());
	outSubset.erase(std::unique(outSubset.begin(), outSubset.end()), outSubset.end());

	if (outSubset.empty()) {
		return failResult(
		    LifecycleErrorCode::NodeResolutionFailed,
		    nullptr,
		    "empty node selection",
		    false
		);
	}

	return okResult();
}

LifecycleResult ESPLifecycle::expandSubsetWithDependents(std::vector<size_t> &subset) {
	if (subset.empty()) {
		return okResult();
	}

	std::vector<bool> included(nodes.size(), false);
	std::queue<size_t> pending;

	for (size_t index : subset) {
		if (index >= nodes.size()) {
			return failResult(
			    LifecycleErrorCode::NodeResolutionFailed,
			    nullptr,
			    "subset index out of bounds",
			    false
			);
		}

		if (!included[index]) {
			included[index] = true;
			pending.push(index);
		}
	}

	while (!pending.empty()) {
		const size_t current = pending.front();
		pending.pop();

		for (size_t dependent : nodes[current].reverseDependencyIndexes) {
			if (dependent < included.size() && !included[dependent]) {
				included[dependent] = true;
				pending.push(dependent);
			}
		}
	}

	subset.clear();
	for (size_t index : topologicalOrder) {
		if (index < included.size() && included[index]) {
			subset.push_back(index);
		}
	}

	if (subset.empty()) {
		return failResult(
		    LifecycleErrorCode::NodeResolutionFailed,
		    nullptr,
		    "dependent closure is empty",
		    false
		);
	}

	return okResult();
}

LifecycleResult ESPLifecycle::expandSubsetWithDependencies(std::vector<size_t> &subset) {
	if (subset.empty()) {
		return okResult();
	}

	std::vector<bool> included(nodes.size(), false);
	std::queue<size_t> pending;

	for (size_t index : subset) {
		if (index >= nodes.size()) {
			return failResult(
			    LifecycleErrorCode::NodeResolutionFailed,
			    nullptr,
			    "subset index out of bounds",
			    false
			);
		}

		if (!included[index]) {
			included[index] = true;
			pending.push(index);
		}
	}

	while (!pending.empty()) {
		const size_t current = pending.front();
		pending.pop();

		for (size_t dep : nodes[current].dependencyIndexes) {
			if (dep < included.size() && !included[dep]) {
				included[dep] = true;
				pending.push(dep);
			}
		}
	}

	subset.clear();
	for (size_t index : topologicalOrder) {
		if (index < included.size() && included[index]) {
			subset.push_back(index);
		}
	}

	if (subset.empty()) {
		return failResult(
		    LifecycleErrorCode::NodeResolutionFailed,
		    nullptr,
		    "dependency closure is empty",
		    false
		);
	}

	return okResult();
}

LifecycleResult ESPLifecycle::expandSubsetForReinitialize(std::vector<size_t> &subset) {
	LifecycleResult dependentsResult = expandSubsetWithDependents(subset);
	if (!dependentsResult.ok) {
		return dependentsResult;
	}

	LifecycleResult dependenciesResult = expandSubsetWithDependencies(subset);
	if (!dependenciesResult.ok) {
		return dependenciesResult;
	}

	return okResult();
}
