#include "ugc_renderer/render/render_graph.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ugc_renderer
{
namespace
{
constexpr std::uint32_t kInvalidPassIndex = std::numeric_limits<std::uint32_t>::max();

const char* ToString(const RenderGraph::ResourceAccess access)
{
    switch (access)
    {
    case RenderGraph::ResourceAccess::Read:
        return "Read";
    case RenderGraph::ResourceAccess::Write:
        return "Write";
    case RenderGraph::ResourceAccess::ReadWrite:
        return "ReadWrite";
    }

    return "Unknown";
}

const char* ToString(const RenderGraph::ResourceState state)
{
    switch (state)
    {
    case RenderGraph::ResourceState::Unknown:
        return "Unknown";
    case RenderGraph::ResourceState::ShaderRead:
        return "ShaderRead";
    case RenderGraph::ResourceState::RenderTarget:
        return "RenderTarget";
    case RenderGraph::ResourceState::DepthWrite:
        return "DepthWrite";
    case RenderGraph::ResourceState::Present:
        return "Present";
    }

    return "Unknown";
}

const char* ToString(const RenderGraph::ResourceKind kind)
{
    switch (kind)
    {
    case RenderGraph::ResourceKind::Internal:
        return "Internal";
    case RenderGraph::ResourceKind::Imported:
        return "Imported";
    case RenderGraph::ResourceKind::Transient:
        return "Transient";
    }

    return "Unknown";
}

RenderGraph::ResourceState ResolveDesiredState(
    const RenderGraph::ResourceUsage& usage,
    const RenderGraph::ResourceState fallbackState)
{
    return usage.desiredState != RenderGraph::ResourceState::Unknown ? usage.desiredState : fallbackState;
}

std::vector<std::string> GetSortedKeys(const std::unordered_map<std::string, RenderGraph::ResourceState>& resources)
{
    std::vector<std::string> names;
    names.reserve(resources.size());
    for (const auto& [resourceName, initialState] : resources)
    {
        (void)initialState;
        names.push_back(resourceName);
    }

    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> GetSortedKeys(const std::unordered_set<std::string>& resources)
{
    std::vector<std::string> names(resources.begin(), resources.end());
    std::sort(names.begin(), names.end());
    return names;
}
} // namespace

RenderGraph::ResourceUsage RenderGraph::Read(std::string resourceName, const ResourceState desiredState)
{
    return ResourceUsage {
        .resourceName = std::move(resourceName),
        .access = ResourceAccess::Read,
        .desiredState = desiredState,
    };
}

RenderGraph::ResourceUsage RenderGraph::Write(std::string resourceName, const ResourceState desiredState)
{
    return ResourceUsage {
        .resourceName = std::move(resourceName),
        .access = ResourceAccess::Write,
        .desiredState = desiredState,
    };
}

RenderGraph::ResourceUsage RenderGraph::ReadWrite(std::string resourceName, const ResourceState desiredState)
{
    return ResourceUsage {
        .resourceName = std::move(resourceName),
        .access = ResourceAccess::ReadWrite,
        .desiredState = desiredState,
    };
}

void RenderGraph::ImportResource(std::string resourceName, const ResourceState initialState)
{
    if (resourceName.empty())
    {
        throw std::invalid_argument("RenderGraph imported resource requires a non-empty resource name.");
    }

    if (transientResources_.contains(resourceName))
    {
        throw std::invalid_argument("RenderGraph resource cannot be both imported and transient.");
    }

    const auto [iterator, inserted] = importedResources_.emplace(std::move(resourceName), initialState);
    if (!inserted)
    {
        if (iterator->second != initialState)
        {
            throw std::invalid_argument("RenderGraph imported resource is declared multiple times with different states.");
        }
    }
}

void RenderGraph::DeclareTransientResource(std::string resourceName, const ResourceState initialState)
{
    if (resourceName.empty())
    {
        throw std::invalid_argument("RenderGraph transient resource requires a non-empty resource name.");
    }

    if (importedResources_.contains(resourceName))
    {
        throw std::invalid_argument("RenderGraph resource cannot be both transient and imported.");
    }

    const auto [iterator, inserted] = transientResources_.emplace(std::move(resourceName), initialState);
    if (!inserted)
    {
        if (iterator->second != initialState)
        {
            throw std::invalid_argument("RenderGraph transient resource is declared multiple times with different states.");
        }
    }
}

void RenderGraph::ExportResource(std::string resourceName)
{
    if (resourceName.empty())
    {
        throw std::invalid_argument("RenderGraph exported resource requires a non-empty resource name.");
    }

    exportedResources_.insert(std::move(resourceName));
}

void RenderGraph::Reset()
{
    importedResources_.clear();
    transientResources_.clear();
    exportedResources_.clear();
    passes_.clear();
}

void RenderGraph::AddPass(std::string name, ExecuteCallback execute)
{
    if (!execute)
    {
        throw std::invalid_argument("RenderGraph pass requires a valid execute callback.");
    }

    AddPass(std::move(name), {}, std::move(execute));
}

void RenderGraph::AddPass(std::string name, std::initializer_list<ResourceUsage> resources, ExecuteCallback execute)
{
    if (!execute)
    {
        throw std::invalid_argument("RenderGraph pass requires a valid execute callback.");
    }

    for (const ResourceUsage& resource : resources)
    {
        if (resource.resourceName.empty())
        {
            throw std::invalid_argument("RenderGraph resource usage requires a non-empty resource name.");
        }
    }

    passes_.push_back(Pass {
        .name = std::move(name),
        .resources = resources,
        .execute = std::move(execute),
    });
}

void RenderGraph::Execute() const
{
    const CompileResult compileResult = Compile();

    for (const std::uint32_t passIndex : compileResult.executionPassIndices)
    {
        passes_[passIndex].execute();
    }
}

RenderGraph::CompileResult RenderGraph::Compile() const
{
    struct DependencyResourceState
    {
        bool available = false;
        std::uint32_t lastWriterPassIndex = kInvalidPassIndex;
        std::vector<std::uint32_t> readerPassIndices;
    };

    CompileResult result;
    result.passes.reserve(passes_.size());
    for (const Pass& pass : passes_)
    {
        result.passes.push_back(CompiledPass {
            .sourcePassIndex = static_cast<std::uint32_t>(result.passes.size()),
            .name = pass.name,
            .resources = pass.resources,
            .transitions = {},
            .dependencyPassIndices = {},
        });
    }

    std::unordered_set<std::string> passNames;
    std::unordered_map<std::string, DependencyResourceState> dependencyResources;
    std::unordered_map<std::string, std::uint32_t> lastAccessPassByResource;
    std::unordered_map<std::string, std::uint32_t> lastWriterPassByResource;
    std::unordered_map<std::string, std::size_t> resourceSummaryIndices;

    auto ensureCompiledResource = [&](const std::string& resourceName) -> CompileResult::CompiledResource&
    {
        const auto existingIndex = resourceSummaryIndices.find(resourceName);
        if (existingIndex != resourceSummaryIndices.end())
        {
            return result.resources[existingIndex->second];
        }

        CompileResult::CompiledResource compiledResource = {};
        compiledResource.name = resourceName;
        compiledResource.firstPassIndex = kInvalidPassIndex;
        compiledResource.lastPassIndex = kInvalidPassIndex;
        compiledResource.firstWriterPassIndex = kInvalidPassIndex;
        compiledResource.lastWriterPassIndex = kInvalidPassIndex;

        if (const auto imported = importedResources_.find(resourceName); imported != importedResources_.end())
        {
            compiledResource.kind = ResourceKind::Imported;
            compiledResource.imported = true;
            compiledResource.initialState = imported->second;
        }
        else if (const auto transient = transientResources_.find(resourceName); transient != transientResources_.end())
        {
            compiledResource.kind = ResourceKind::Transient;
            compiledResource.initialState = transient->second;
        }

        compiledResource.exported = exportedResources_.contains(resourceName);

        const std::size_t resourceIndex = result.resources.size();
        resourceSummaryIndices.emplace(resourceName, resourceIndex);
        result.resources.push_back(std::move(compiledResource));
        return result.resources.back();
    };

    for (const auto& [resourceName, initialState] : importedResources_)
    {
        (void)initialState;
        dependencyResources[resourceName].available = true;
        ensureCompiledResource(resourceName);
    }

    for (const auto& [resourceName, initialState] : transientResources_)
    {
        dependencyResources[resourceName].available = initialState != ResourceState::Unknown;
        ensureCompiledResource(resourceName);
    }

    for (const std::string& exportedResource : exportedResources_)
    {
        ensureCompiledResource(exportedResource).exported = true;
    }

    auto addDependency = [&](const std::uint32_t fromPassIndex,
                             const std::uint32_t toPassIndex,
                             const std::string& resourceName)
    {
        if (fromPassIndex == kInvalidPassIndex || fromPassIndex == toPassIndex)
        {
            return;
        }

        auto& dependencyPassIndices = result.passes[toPassIndex].dependencyPassIndices;
        if (std::find(dependencyPassIndices.begin(), dependencyPassIndices.end(), fromPassIndex)
            == dependencyPassIndices.end())
        {
            dependencyPassIndices.push_back(fromPassIndex);
        }

        const auto duplicateEdge = std::find_if(
            result.edges.begin(),
            result.edges.end(),
            [&](const DependencyEdge& edge)
            {
                return edge.fromPassIndex == fromPassIndex && edge.toPassIndex == toPassIndex
                    && edge.resourceName == resourceName;
            });
        if (duplicateEdge == result.edges.end())
        {
            result.edges.push_back(DependencyEdge {
                .fromPassIndex = fromPassIndex,
                .toPassIndex = toPassIndex,
                .resourceName = resourceName,
            });
        }
    };

    for (std::uint32_t passIndex = 0; passIndex < passes_.size(); ++passIndex)
    {
        const Pass& pass = passes_[passIndex];
        if (pass.name.empty())
        {
            throw std::invalid_argument("RenderGraph pass requires a non-empty name.");
        }

        if (!passNames.insert(pass.name).second)
        {
            throw std::invalid_argument("RenderGraph pass names must be unique.");
        }

        std::unordered_set<std::string> passResourceNames;
        for (const ResourceUsage& resource : pass.resources)
        {
            if (!passResourceNames.insert(resource.resourceName).second)
            {
                throw std::invalid_argument("RenderGraph pass cannot declare the same resource more than once.");
            }

            ensureCompiledResource(resource.resourceName);
            DependencyResourceState& resourceState = dependencyResources[resource.resourceName];
            if ((resource.access == ResourceAccess::Read || resource.access == ResourceAccess::ReadWrite)
                && !resourceState.available)
            {
                std::ostringstream error;
                error << "RenderGraph resource '" << resource.resourceName
                      << "' is read before it is produced or imported.";
                throw std::invalid_argument(error.str());
            }

            lastAccessPassByResource[resource.resourceName] = passIndex;

            if (resource.access == ResourceAccess::Read)
            {
                addDependency(resourceState.lastWriterPassIndex, passIndex, resource.resourceName);
                resourceState.readerPassIndices.push_back(passIndex);
                continue;
            }

            addDependency(resourceState.lastWriterPassIndex, passIndex, resource.resourceName);
            for (const std::uint32_t readerPassIndex : resourceState.readerPassIndices)
            {
                addDependency(readerPassIndex, passIndex, resource.resourceName);
            }

            resourceState.available = true;
            resourceState.lastWriterPassIndex = passIndex;
            lastWriterPassByResource[resource.resourceName] = passIndex;
            resourceState.readerPassIndices.clear();
        }
    }

    std::vector<bool> requiredPasses(result.passes.size(), exportedResources_.empty());
    if (!exportedResources_.empty())
    {
        std::vector<std::uint32_t> passStack;
        for (const std::string& exportedResource : exportedResources_)
        {
            const auto lastAccess = lastAccessPassByResource.find(exportedResource);
            if (lastAccess == lastAccessPassByResource.end())
            {
                std::ostringstream error;
                error << "RenderGraph exported resource '" << exportedResource << "' is never used.";
                throw std::invalid_argument(error.str());
            }

            passStack.push_back(lastAccess->second);
        }

        while (!passStack.empty())
        {
            const std::uint32_t passIndex = passStack.back();
            passStack.pop_back();

            if (requiredPasses[passIndex])
            {
                continue;
            }

            requiredPasses[passIndex] = true;
            for (const std::uint32_t dependencyPassIndex : result.passes[passIndex].dependencyPassIndices)
            {
                passStack.push_back(dependencyPassIndex);
            }
        }
    }

    for (std::uint32_t passIndex = 0; passIndex < result.passes.size(); ++passIndex)
    {
        if (!requiredPasses[passIndex])
        {
            result.passes[passIndex].culled = true;
            result.culledPassIndices.push_back(passIndex);
        }
    }

    std::vector<std::uint32_t> pendingDependencyCounts(result.passes.size(), 0);
    std::vector<std::vector<std::uint32_t>> dependentPassIndices(result.passes.size());
    for (std::uint32_t passIndex = 0; passIndex < result.passes.size(); ++passIndex)
    {
        if (!requiredPasses[passIndex])
        {
            continue;
        }

        pendingDependencyCounts[passIndex] =
            static_cast<std::uint32_t>(result.passes[passIndex].dependencyPassIndices.size());
        for (const std::uint32_t dependencyPassIndex : result.passes[passIndex].dependencyPassIndices)
        {
            if (!requiredPasses[dependencyPassIndex])
            {
                continue;
            }

            dependentPassIndices[dependencyPassIndex].push_back(passIndex);
        }
    }

    std::vector<bool> emittedPasses(result.passes.size(), false);
    result.executionPassIndices.reserve(result.passes.size());
    const std::size_t requiredPassCount = static_cast<std::size_t>(
        std::count(requiredPasses.begin(), requiredPasses.end(), true));

    while (result.executionPassIndices.size() < requiredPassCount)
    {
        bool emittedPass = false;
        for (std::uint32_t passIndex = 0; passIndex < result.passes.size(); ++passIndex)
        {
            if (!requiredPasses[passIndex] || emittedPasses[passIndex] || pendingDependencyCounts[passIndex] != 0)
            {
                continue;
            }

            emittedPasses[passIndex] = true;
            result.executionPassIndices.push_back(passIndex);
            emittedPass = true;

            for (const std::uint32_t dependentPassIndex : dependentPassIndices[passIndex])
            {
                --pendingDependencyCounts[dependentPassIndex];
            }
            break;
        }

        if (!emittedPass)
        {
            throw std::invalid_argument("RenderGraph dependencies contain a cycle.");
        }
    }

    std::unordered_map<std::string, ResourceState> currentStates;
    for (const CompileResult::CompiledResource& resource : result.resources)
    {
        currentStates.emplace(resource.name, resource.initialState);
    }

    for (const std::uint32_t passIndex : result.executionPassIndices)
    {
        CompiledPass& compiledPass = result.passes[passIndex];
        for (const ResourceUsage& resourceUsage : compiledPass.resources)
        {
            CompileResult::CompiledResource& compiledResource =
                result.resources[resourceSummaryIndices.at(resourceUsage.resourceName)];
            const ResourceState beforeState = currentStates[resourceUsage.resourceName];
            const ResourceState desiredState = ResolveDesiredState(resourceUsage, beforeState);

            if (compiledResource.firstPassIndex == kInvalidPassIndex)
            {
                compiledResource.firstPassIndex = passIndex;
                compiledResource.firstUsageState = desiredState;
            }

            compiledResource.lastPassIndex = passIndex;
            compiledResource.lastUsageState = desiredState;

            if (resourceUsage.access != ResourceAccess::Read)
            {
                if (compiledResource.firstWriterPassIndex == kInvalidPassIndex)
                {
                    compiledResource.firstWriterPassIndex = passIndex;
                }

                compiledResource.lastWriterPassIndex = passIndex;
            }

            if (beforeState != ResourceState::Unknown
                && desiredState != ResourceState::Unknown
                && beforeState != desiredState)
            {
                compiledPass.transitions.push_back(CompiledPass::ResourceTransition {
                    .resourceName = resourceUsage.resourceName,
                    .beforeState = beforeState,
                    .afterState = desiredState,
                });
            }

            if (desiredState != ResourceState::Unknown)
            {
                currentStates[resourceUsage.resourceName] = desiredState;
            }
        }
    }

    std::sort(
        result.resources.begin(),
        result.resources.end(),
        [](const CompileResult::CompiledResource& left, const CompileResult::CompiledResource& right)
        {
            return left.name < right.name;
        });

    return result;
}

std::string RenderGraph::Describe() const
{
    return Describe(Compile());
}

std::string RenderGraph::Describe(const CompileResult& compileResult) const
{
    std::ostringstream description;
    description << "RenderGraph:\n";

    const auto importedNames = GetSortedKeys(importedResources_);
    if (!importedNames.empty())
    {
        description << "  Imported:\n";
        for (const std::string& resourceName : importedNames)
        {
            description << "    " << resourceName << " (" << ToString(importedResources_.at(resourceName)) << ")\n";
        }
    }

    const auto transientNames = GetSortedKeys(transientResources_);
    if (!transientNames.empty())
    {
        description << "  Transient:\n";
        for (const std::string& resourceName : transientNames)
        {
            description << "    " << resourceName << " (" << ToString(transientResources_.at(resourceName)) << ")\n";
        }
    }

    const auto exportedNames = GetSortedKeys(exportedResources_);
    if (!exportedNames.empty())
    {
        description << "  Exported:";
        for (const std::string& resourceName : exportedNames)
        {
            description << ' ' << resourceName;
        }
        description << '\n';
    }

    description << "  Passes:\n";
    for (std::uint32_t passIndex = 0; passIndex < compileResult.passes.size(); ++passIndex)
    {
        const CompiledPass& pass = compileResult.passes[passIndex];
        description << "    [" << passIndex << "] " << pass.name;
        if (pass.culled)
        {
            description << " (culled)";
        }
        description << '\n';

        if (!pass.resources.empty())
        {
            description << "      resources:";
            for (const ResourceUsage& resource : pass.resources)
            {
                description << ' ' << resource.resourceName
                            << '(' << ToString(resource.access)
                            << ", " << ToString(resource.desiredState) << ')';
            }
            description << '\n';
        }

        if (!pass.transitions.empty())
        {
            description << "      transitions:";
            for (const CompiledPass::ResourceTransition& transition : pass.transitions)
            {
                description << ' ' << transition.resourceName
                            << '(' << ToString(transition.beforeState)
                            << "->" << ToString(transition.afterState) << ')';
            }
            description << '\n';
        }

        description << "      deps:";
        if (pass.dependencyPassIndices.empty())
        {
            description << " none";
        }
        else
        {
            for (const std::uint32_t dependencyPassIndex : pass.dependencyPassIndices)
            {
                description << ' ' << compileResult.passes[dependencyPassIndex].name;
            }
        }
        description << '\n';
    }

    if (!compileResult.resources.empty())
    {
        description << "  Resources:\n";
        for (const CompileResult::CompiledResource& resource : compileResult.resources)
        {
            description << "    " << resource.name
                        << " [kind=" << ToString(resource.kind);
            if (resource.exported)
            {
                description << ", exported";
            }
            description << "]\n";

            description << "      states: initial=" << ToString(resource.initialState)
                        << " first=" << ToString(resource.firstUsageState)
                        << " last=" << ToString(resource.lastUsageState) << '\n';

            description << "      lifetime:";
            if (resource.firstPassIndex == kInvalidPassIndex)
            {
                description << " external-only";
            }
            else
            {
                description << " first=" << compileResult.passes[resource.firstPassIndex].name
                            << " last=" << compileResult.passes[resource.lastPassIndex].name;
            }
            description << '\n';

            description << "      writers:";
            if (resource.firstWriterPassIndex == kInvalidPassIndex)
            {
                description << " none";
            }
            else
            {
                description << " first=" << compileResult.passes[resource.firstWriterPassIndex].name
                            << " last=" << compileResult.passes[resource.lastWriterPassIndex].name;
            }
            description << '\n';
        }
    }

    if (!compileResult.edges.empty())
    {
        description << "  Edges:\n";
        for (const DependencyEdge& edge : compileResult.edges)
        {
            description << "    " << compileResult.passes[edge.fromPassIndex].name
                        << " -> " << compileResult.passes[edge.toPassIndex].name
                        << " via " << edge.resourceName << '\n';
        }
    }

    if (!compileResult.executionPassIndices.empty())
    {
        description << "  ExecutionOrder:";
        for (const std::uint32_t passIndex : compileResult.executionPassIndices)
        {
            description << ' ' << compileResult.passes[passIndex].name;
        }
        description << '\n';
    }

    if (!compileResult.culledPassIndices.empty())
    {
        description << "  Culled:";
        for (const std::uint32_t passIndex : compileResult.culledPassIndices)
        {
            description << ' ' << compileResult.passes[passIndex].name;
        }
        description << '\n';
    }

    return description.str();
}

void RenderGraph::Validate() const
{
    (void)Compile();
}

const std::vector<RenderGraph::Pass>& RenderGraph::GetPasses() const noexcept
{
    return passes_;
}

bool RenderGraph::Empty() const noexcept
{
    return passes_.empty();
}

bool RenderGraph::IsImportedResource(const std::string_view resourceName) const
{
    return importedResources_.contains(std::string(resourceName));
}

bool RenderGraph::IsTransientResource(const std::string_view resourceName) const
{
    return transientResources_.contains(std::string(resourceName));
}

bool RenderGraph::IsExportedResource(const std::string_view resourceName) const
{
    return exportedResources_.contains(std::string(resourceName));
}
} // namespace ugc_renderer
