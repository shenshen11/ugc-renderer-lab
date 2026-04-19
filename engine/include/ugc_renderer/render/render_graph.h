#pragma once

#include <initializer_list>
#include <cstdint>
#include <functional>
#include <string_view>
#include <string>
#include <unordered_set>
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

    struct DependencyEdge
    {
        std::uint32_t fromPassIndex = 0;
        std::uint32_t toPassIndex = 0;
        std::string resourceName;
    };

    struct CompiledPass
    {
        std::uint32_t sourcePassIndex = 0;
        std::string name;
        std::vector<ResourceUsage> resources;
        std::vector<std::uint32_t> dependencyPassIndices;
    };

    struct CompileResult
    {
        std::vector<CompiledPass> passes;
        std::vector<DependencyEdge> edges;
        std::vector<std::uint32_t> executionPassIndices;
    };

    [[nodiscard]] static ResourceUsage Read(std::string resourceName);
    [[nodiscard]] static ResourceUsage Write(std::string resourceName);
    [[nodiscard]] static ResourceUsage ReadWrite(std::string resourceName);

    void ImportResource(std::string resourceName);
    void Reset();
    void AddPass(std::string name, ExecuteCallback execute);
    void AddPass(std::string name, std::initializer_list<ResourceUsage> resources, ExecuteCallback execute);
    void Execute() const;
    [[nodiscard]] CompileResult Compile() const;
    void Validate() const;

    [[nodiscard]] const std::vector<Pass>& GetPasses() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] bool IsImportedResource(std::string_view resourceName) const;

private:
    std::unordered_set<std::string> importedResources_;
    std::vector<Pass> passes_;
};
} // namespace ugc_renderer
