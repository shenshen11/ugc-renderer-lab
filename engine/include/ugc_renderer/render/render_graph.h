#pragma once

#include <initializer_list>
#include <cstdint>
#include <functional>
#include <string_view>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ugc_renderer
{
class RenderGraph
{
public:
    using ExecuteCallback = std::function<void()>;

    enum class ResourceState
    {
        Unknown,
        ShaderRead,
        RenderTarget,
        DepthWrite,
        Present,
    };

    enum class ResourceKind
    {
        Internal,
        Imported,
        Transient,
    };

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
        ResourceState desiredState = ResourceState::Unknown;
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
        struct ResourceTransition
        {
            std::string resourceName;
            ResourceState beforeState = ResourceState::Unknown;
            ResourceState afterState = ResourceState::Unknown;
        };

        std::uint32_t sourcePassIndex = 0;
        bool culled = false;
        std::string name;
        std::vector<ResourceUsage> resources;
        std::vector<ResourceTransition> transitions;
        std::vector<std::uint32_t> dependencyPassIndices;
    };

    struct CompileResult
    {
        struct CompiledResource
        {
            std::string name;
            ResourceKind kind = ResourceKind::Internal;
            bool imported = false;
            bool exported = false;
            ResourceState initialState = ResourceState::Unknown;
            ResourceState firstUsageState = ResourceState::Unknown;
            ResourceState lastUsageState = ResourceState::Unknown;
            std::uint32_t firstPassIndex = 0;
            std::uint32_t lastPassIndex = 0;
            std::uint32_t firstWriterPassIndex = 0;
            std::uint32_t lastWriterPassIndex = 0;
        };

        std::vector<CompiledPass> passes;
        std::vector<CompiledResource> resources;
        std::vector<DependencyEdge> edges;
        std::vector<std::uint32_t> executionPassIndices;
        std::vector<std::uint32_t> culledPassIndices;
    };

    [[nodiscard]] static ResourceUsage Read(
        std::string resourceName,
        ResourceState desiredState = ResourceState::Unknown);
    [[nodiscard]] static ResourceUsage Write(
        std::string resourceName,
        ResourceState desiredState = ResourceState::Unknown);
    [[nodiscard]] static ResourceUsage ReadWrite(
        std::string resourceName,
        ResourceState desiredState = ResourceState::Unknown);

    void ImportResource(std::string resourceName, ResourceState initialState = ResourceState::Unknown);
    void DeclareTransientResource(std::string resourceName, ResourceState initialState = ResourceState::Unknown);
    void ExportResource(std::string resourceName);
    void Reset();
    void AddPass(std::string name, ExecuteCallback execute);
    void AddPass(std::string name, std::initializer_list<ResourceUsage> resources, ExecuteCallback execute);
    void Execute() const;
    [[nodiscard]] CompileResult Compile() const;
    [[nodiscard]] std::string Describe() const;
    [[nodiscard]] std::string Describe(const CompileResult& compileResult) const;
    void Validate() const;

    [[nodiscard]] const std::vector<Pass>& GetPasses() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] bool IsImportedResource(std::string_view resourceName) const;
    [[nodiscard]] bool IsTransientResource(std::string_view resourceName) const;
    [[nodiscard]] bool IsExportedResource(std::string_view resourceName) const;

private:
    std::unordered_map<std::string, ResourceState> importedResources_;
    std::unordered_map<std::string, ResourceState> transientResources_;
    std::unordered_set<std::string> exportedResources_;
    std::vector<Pass> passes_;
};
} // namespace ugc_renderer
