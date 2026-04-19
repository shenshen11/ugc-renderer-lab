#pragma once

#include <initializer_list>
#include <functional>
#include <string>
#include <vector>

namespace ugc_renderer
{
class RenderGraph
{
public:
    using ExecuteCallback = std::function<void()>;

    enum class ResourceAccess
    {
        Read,
        Write,
        ReadWrite,
    };

    struct ResourceUsage
    {
        std::string resourceName;
        ResourceAccess access = ResourceAccess::Read;
    };

    struct Pass
    {
        std::string name;
        std::vector<ResourceUsage> resources;
        ExecuteCallback execute;
    };

    [[nodiscard]] static ResourceUsage Read(std::string resourceName);
    [[nodiscard]] static ResourceUsage Write(std::string resourceName);
    [[nodiscard]] static ResourceUsage ReadWrite(std::string resourceName);

    void Reset();
    void AddPass(std::string name, ExecuteCallback execute);
    void AddPass(std::string name, std::initializer_list<ResourceUsage> resources, ExecuteCallback execute);
    void Execute() const;

    [[nodiscard]] const std::vector<Pass>& GetPasses() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;

private:
    std::vector<Pass> passes_;
};
} // namespace ugc_renderer
