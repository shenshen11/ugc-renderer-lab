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
RenderGraph::ResourceUsage RenderGraph::Read(std::string resourceName)
{
    return ResourceUsage {
        .resourceName = std::move(resourceName),
        .access = ResourceAccess::Read,
    };
}

RenderGraph::ResourceUsage RenderGraph::Write(std::string resourceName)
{
    return ResourceUsage {
        .resourceName = std::move(resourceName),
        .access = ResourceAccess::Write,
    };
}

RenderGraph::ResourceUsage RenderGraph::ReadWrite(std::string resourceName)
{
    return ResourceUsage {
        .resourceName = std::move(resourceName),
        .access = ResourceAccess::ReadWrite,
    };
}

void RenderGraph::ImportResource(std::string resourceName)
{
    if (resourceName.empty())
    {
        throw std::invalid_argument("RenderGraph imported resource requires a non-empty resource name.");
    }

    importedResources_.insert(std::move(resourceName));
}

void RenderGraph::Reset()
{
    importedResources_.clear();
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
    constexpr std::uint32_t kInvalidPassIndex = std::numeric_limits<std::uint32_t>::max();

    struct ResourceState
    {
        bool available = false;
        std::uint32_t lastWriterPassIndex = std::numeric_limits<std::uint32_t>::max();
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
            .dependencyPassIndices = {},
        });
    }

    std::unordered_set<std::string> passNames;
    std::unordered_map<std::string, ResourceState> resourceStates;

    for (const std::string& importedResource : importedResources_)
    {
        resourceStates[importedResource].available = true;
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

            ResourceState& resourceState = resourceStates[resource.resourceName];
            if ((resource.access == ResourceAccess::Read || resource.access == ResourceAccess::ReadWrite)
                && !resourceState.available)
            {
                std::ostringstream error;
                error << "RenderGraph resource '" << resource.resourceName
                      << "' is read before it is produced or imported.";
                throw std::invalid_argument(error.str());
            }

            if (resource.access == ResourceAccess::Read)
            {
                addDependency(resourceState.lastWriterPassIndex, passIndex, resource.resourceName);
                resourceState.readerPassIndices.push_back(passIndex);
                continue;
            }

            if (resource.access == ResourceAccess::Write)
            {
                addDependency(resourceState.lastWriterPassIndex, passIndex, resource.resourceName);
                for (const std::uint32_t readerPassIndex : resourceState.readerPassIndices)
                {
                    addDependency(readerPassIndex, passIndex, resource.resourceName);
                }
                resourceState.available = true;
                resourceState.lastWriterPassIndex = passIndex;
                resourceState.readerPassIndices.clear();
                continue;
            }

            addDependency(resourceState.lastWriterPassIndex, passIndex, resource.resourceName);
            for (const std::uint32_t readerPassIndex : resourceState.readerPassIndices)
            {
                addDependency(readerPassIndex, passIndex, resource.resourceName);
            }
            resourceState.available = true;
            resourceState.lastWriterPassIndex = passIndex;
            resourceState.readerPassIndices.clear();
        }
    }

    std::vector<std::uint32_t> pendingDependencyCounts;
    std::vector<std::vector<std::uint32_t>> dependentPassIndices;
    pendingDependencyCounts.resize(result.passes.size());
    dependentPassIndices.resize(result.passes.size());

    for (std::uint32_t passIndex = 0; passIndex < result.passes.size(); ++passIndex)
    {
        pendingDependencyCounts[passIndex] =
            static_cast<std::uint32_t>(result.passes[passIndex].dependencyPassIndices.size());
        for (const std::uint32_t dependencyPassIndex : result.passes[passIndex].dependencyPassIndices)
        {
            dependentPassIndices[dependencyPassIndex].push_back(passIndex);
        }
    }

    std::vector<bool> emittedPasses(result.passes.size(), false);
    result.executionPassIndices.reserve(result.passes.size());

    while (result.executionPassIndices.size() < result.passes.size())
    {
        bool emittedPass = false;
        for (std::uint32_t passIndex = 0; passIndex < result.passes.size(); ++passIndex)
        {
            if (emittedPasses[passIndex] || pendingDependencyCounts[passIndex] != 0)
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

    return result;
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
} // namespace ugc_renderer
