#include "esp_lifecycle/lifecycle.h"

#include <algorithm>
#include <queue>
#include <string>
#include <unordered_map>

LifecycleResult ESPLifecycle::build() {
    std::lock_guard<std::mutex> lock(transitionMutex);
    return validateAndBuildGraph();
}

LifecycleResult ESPLifecycle::validateAndBuildGraph() {
    if( sections.empty() ){
        ensureSection("default");
    }

    if( nodes.size() > config.maxNodes ){
        return failResult(LifecycleErrorCode::InvalidConfig, nullptr, "maxNodes exceeded", false);
    }

    std::unordered_map<std::string, size_t> sectionByName;
    sectionByName.reserve(sections.size());
    bool hasDeferredSection = false;
    for( size_t i = 0; i < sections.size(); i++ ){
        const std::string& name = sections[i].name;
        if( name.empty() ){
            return failResult(LifecycleErrorCode::InvalidSection, nullptr, "empty section name", false);
        }

        if( sectionByName.find(name) != sectionByName.end() ){
            return failResult(LifecycleErrorCode::InvalidSection, nullptr, "duplicate section name", false);
        }

        sectionByName[name] = i;

        if( sections[i].mode == LifecycleSectionMode::Deferred ){
            hasDeferredSection = true;
            if( !sections[i].readinessCheck || !sections[i].waitFn ){
                return failResult(
                    LifecycleErrorCode::InvalidSection,
                    nullptr,
                    "deferred section requires readiness and wait callback",
                    false
                );
            }
        }
    }

    if( hasDeferredSection && config.worker == nullptr ){
        return failResult(
            LifecycleErrorCode::InvalidConfig,
            nullptr,
            "deferred sections require config.worker",
            false
        );
    }

    std::unordered_map<std::string, size_t> nodeByName;
    nodeByName.reserve(nodes.size());

    size_t dependencyNameCount = 0;

    for( size_t i = 0; i < nodes.size(); i++ ){
        if( nodes[i].name.empty() ){
            return failResult(LifecycleErrorCode::InvalidConfig, nullptr, "empty node name", false);
        }

        if( nodes[i].sectionIndex >= sections.size() ){
            return failResult(LifecycleErrorCode::InvalidSection, nodes[i].name.c_str(), "node references undefined section", false);
        }

        if( !nodes[i].initFn || !nodes[i].teardownFn ){
            return failResult(LifecycleErrorCode::InvalidConfig, nodes[i].name.c_str(), "node requires init and teardown callbacks", false);
        }

        if( nodeByName.find(nodes[i].name) != nodeByName.end() ){
            return failResult(LifecycleErrorCode::DuplicateNode, nodes[i].name.c_str(), "duplicate node name", false);
        }

        nodeByName[nodes[i].name] = i;

        dependencyNameCount += nodes[i].dependenciesByName.size();
        dependencyNameCount += nodes[i].dependentsByName.size();
    }

    if( dependencyNameCount > config.maxDependencies ){
        return failResult(LifecycleErrorCode::InvalidConfig, nullptr, "maxDependencies exceeded", false);
    }

    for( auto& node : nodes ){
        node.dependencyIndexes.clear();
        node.reverseDependencyIndexes.clear();
    }

    for( size_t nodeIndex = 0; nodeIndex < nodes.size(); nodeIndex++ ){
        auto& node = nodes[nodeIndex];

        for( const std::string& depName : node.dependenciesByName ){
            auto it = nodeByName.find(depName);
            if( it == nodeByName.end() ){
                return failResult(LifecycleErrorCode::MissingDependency, node.name.c_str(), depName.c_str(), false);
            }
            node.dependencyIndexes.push_back(it->second);
        }

        for( const std::string& dependentName : node.dependentsByName ){
            auto it = nodeByName.find(dependentName);
            if( it == nodeByName.end() ){
                return failResult(LifecycleErrorCode::MissingDependency, node.name.c_str(), dependentName.c_str(), false);
            }
            nodes[it->second].dependencyIndexes.push_back(nodeIndex);
        }
    }

    for( size_t nodeIndex = 0; nodeIndex < nodes.size(); nodeIndex++ ){
        auto& deps = nodes[nodeIndex].dependencyIndexes;
        std::sort(deps.begin(), deps.end());
        deps.erase(std::unique(deps.begin(), deps.end()), deps.end());

        for( size_t depIndex : deps ){
            if( depIndex >= nodes.size() ){
                return failResult(LifecycleErrorCode::InvalidConfig, nodes[nodeIndex].name.c_str(), "dependency index invalid", false);
            }

            if( nodes[depIndex].sectionIndex > nodes[nodeIndex].sectionIndex ){
                return failResult(
                    LifecycleErrorCode::InvalidConfig,
                    nodes[nodeIndex].name.c_str(),
                    "dependency points to future section",
                    false
                );
            }

            nodes[depIndex].reverseDependencyIndexes.push_back(nodeIndex);
        }
    }

    for( auto& node : nodes ){
        auto& rev = node.reverseDependencyIndexes;
        std::sort(rev.begin(), rev.end());
        rev.erase(std::unique(rev.begin(), rev.end()), rev.end());
    }

    LifecycleResult cycleResult = ensureNoCycles();
    if( !cycleResult.ok ){
        return cycleResult;
    }

    initialized.assign(nodes.size(), false);
    graphBuilt = true;

    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        snapshotValue.total = static_cast<uint16_t>(nodes.size());
        snapshotValue.completed = 0;
        snapshotValue.failed = false;
        snapshotValue.errorCode = LifecycleErrorCode::None;
        snapshotValue.updatedAtMs = nowMs();
    }
    publishSnapshot();

    return okResult("graph built");
}

LifecycleResult ESPLifecycle::ensureNoCycles() {
    std::vector<size_t> indegree(nodes.size(), 0);
    std::queue<size_t> ready;

    for( size_t i = 0; i < nodes.size(); i++ ){
        indegree[i] = nodes[i].dependencyIndexes.size();
        if( indegree[i] == 0 ){
            ready.push(i);
        }
    }

    topologicalOrder.clear();
    topologicalOrder.reserve(nodes.size());

    while( !ready.empty() ){
        const size_t current = ready.front();
        ready.pop();
        topologicalOrder.push_back(current);

        for( size_t dependent : nodes[current].reverseDependencyIndexes ){
            if( dependent >= indegree.size() ){
                continue;
            }

            if( indegree[dependent] == 0 ){
                continue;
            }

            indegree[dependent]--;
            if( indegree[dependent] == 0 ){
                ready.push(dependent);
            }
        }
    }

    if( topologicalOrder.size() != nodes.size() ){
        return failResult(LifecycleErrorCode::CycleDetected, nullptr, "dependency cycle detected", false);
    }

    return okResult();
}

LifecycleResult ESPLifecycle::resolveIndexes() {
    return validateAndBuildGraph();
}
