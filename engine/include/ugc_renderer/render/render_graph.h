#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ugc_renderer
{
class RenderGraph
{
public:
    using ExecuteCallback = std::function<void()>;

    struct Pass
    {
        std::string name;
        ExecuteCallback execute;
    };

    void Reset();
    void AddPass(std::string name, ExecuteCallback execute);
    void Execute() const;

    [[nodiscard]] const std::vector<Pass>& GetPasses() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;

private:
    std::vector<Pass> passes_;
};
} // namespace ugc_renderer
