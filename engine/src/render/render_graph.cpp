#include "ugc_renderer/render/render_graph.h"

#include <stdexcept>
#include <utility>

namespace ugc_renderer
{
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

    passes_.push_back(Pass {
        .name = std::move(name),
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
