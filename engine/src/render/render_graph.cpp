#include "ugc_renderer/render/render_graph.h"

#include <stdexcept>
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

void RenderGraph::Reset()
{
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
    for (const Pass& pass : passes_)
    {
        pass.execute();
    }
}

const std::vector<RenderGraph::Pass>& RenderGraph::GetPasses() const noexcept
{
    return passes_;
}

bool RenderGraph::Empty() const noexcept
{
    return passes_.empty();
}
} // namespace ugc_renderer
