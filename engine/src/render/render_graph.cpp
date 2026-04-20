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
constexpr std::uint32_t kInvalidPhysicalResourceIndex = std::numeric_limits<std::uint32_t>::max();

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

const char* ToString(const RenderGraph::ResourceDimension dimension)
{
    switch (dimension)
    {
    case RenderGraph::ResourceDimension::Unknown:
        return "Unknown";
    case RenderGraph::ResourceDimension::Texture2D:
        return "Texture2D";
    }

    return "Unknown";
}

const char* ToString(const RenderGraph::PassType type)
{
    switch (type)
    {
    case RenderGraph::PassType::Generic:
        return "Generic";
    case RenderGraph::PassType::Graphics:
        return "Graphics";
    case RenderGraph::PassType::Fullscreen:
        return "Fullscreen";
    case RenderGraph::PassType::Present:
        return "Present";
    }

    return "Unknown";
}

const char* ToString(const DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_UNKNOWN:
        return "UNKNOWN";
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return "R16G16B16A16_FLOAT";
    case DXGI_FORMAT_D32_FLOAT:
        return "D32_FLOAT";
    case DXGI_FORMAT_R32_FLOAT:
        return "R32_FLOAT";
    case DXGI_FORMAT_R32_TYPELESS:
        return "R32_TYPELESS";
    default:
        return "OTHER";
    }
}

bool HasBindFlag(
    const RenderGraph::ResourceBindFlags bindFlags,
    const RenderGraph::ResourceBindFlags flag) noexcept
{
    return (static_cast<std::uint32_t>(bindFlags) & static_cast<std::uint32_t>(flag)) != 0;
}

RenderGraph::ResourceState ResolveDesiredState(
    const RenderGraph::ResourceUsage& usage,
    const RenderGraph::ResourceState fallbackState)
{
    return usage.desiredState != RenderGraph::ResourceState::Unknown ? usage.desiredState : fallbackState;
}

std::vector<std::string> GetSortedKeys(
    const std::unordered_map<std::string, RenderGraph::ResourceDeclaration>& resources)
{
    std::vector<std::string> names;
    names.reserve(resources.size());
    for (const auto& [resourceName, declaration] : resources)
    {
        (void)declaration;
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

std::string DescribeResourceDesc(const RenderGraph::ResourceDesc& resourceDesc)
{
    std::ostringstream description;
    description << ToString(resourceDesc.dimension);

    if (resourceDesc.dimension == RenderGraph::ResourceDimension::Texture2D)
    {
        description << " " << resourceDesc.width << "x" << resourceDesc.height
                    << " fmt=" << ToString(resourceDesc.format);

        if (resourceDesc.shaderReadFormat != DXGI_FORMAT_UNKNOWN && resourceDesc.shaderReadFormat != resourceDesc.format)
        {
            description << " srv=" << ToString(resourceDesc.shaderReadFormat);
        }
        if (resourceDesc.renderTargetFormat != DXGI_FORMAT_UNKNOWN
            && resourceDesc.renderTargetFormat != resourceDesc.format)
        {
            description << " rtv=" << ToString(resourceDesc.renderTargetFormat);
        }
        if (resourceDesc.depthStencilFormat != DXGI_FORMAT_UNKNOWN
            && resourceDesc.depthStencilFormat != resourceDesc.format)
        {
            description << " dsv=" << ToString(resourceDesc.depthStencilFormat);
        }

        description
                    << " flags=";

        bool emittedFlag = false;
        if (HasBindFlag(resourceDesc.bindFlags, RenderGraph::ResourceBindFlags::ShaderRead))
        {
            description << "ShaderRead";
            emittedFlag = true;
        }
        if (HasBindFlag(resourceDesc.bindFlags, RenderGraph::ResourceBindFlags::RenderTarget))
        {
            description << (emittedFlag ? "|" : "") << "RenderTarget";
            emittedFlag = true;
        }
        if (HasBindFlag(resourceDesc.bindFlags, RenderGraph::ResourceBindFlags::DepthStencil))
        {
            description << (emittedFlag ? "|" : "") << "DepthStencil";
            emittedFlag = true;
        }
        if (!emittedFlag)
        {
            description << "None";
        }
    }

    return description.str();
}

std::string EscapeGraphvizLabel(const std::string_view text)
{
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text)
    {
        switch (character)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        default:
            escaped += character;
            break;
        }
    }

    return escaped;
}
} // namespace

RenderGraph::ResourceDesc RenderGraph::ResourceDesc::Texture2D(
    const std::uint32_t width,
    const std::uint32_t height,
    const DXGI_FORMAT format)
{
    ResourceDesc desc = {};
    desc.dimension = ResourceDimension::Texture2D;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    return desc;
}

RenderGraph::ResourceDesc& RenderGraph::ResourceDesc::AllowShaderRead()
{
    bindFlags |= ResourceBindFlags::ShaderRead;
    if (shaderReadFormat == DXGI_FORMAT_UNKNOWN)
    {
        shaderReadFormat = format;
    }
    return *this;
}

RenderGraph::ResourceDesc& RenderGraph::ResourceDesc::AllowRenderTarget(const std::array<float, 4> clearColorValue)
{
    bindFlags |= ResourceBindFlags::RenderTarget;
    if (renderTargetFormat == DXGI_FORMAT_UNKNOWN)
    {
        renderTargetFormat = format;
    }
    hasClearValue = true;
    clearColor = clearColorValue;
    return *this;
}

RenderGraph::ResourceDesc& RenderGraph::ResourceDesc::AllowDepthStencil(
    const float clearDepthValue,
    const std::uint8_t clearStencilValue)
{
    bindFlags |= ResourceBindFlags::DepthStencil;
    if (depthStencilFormat == DXGI_FORMAT_UNKNOWN)
    {
        depthStencilFormat = format;
    }
    hasClearValue = true;
    clearDepth = clearDepthValue;
    clearStencil = clearStencilValue;
    return *this;
}

RenderGraph::ResourceDesc& RenderGraph::ResourceDesc::SetShaderReadFormat(const DXGI_FORMAT shaderReadFormatValue)
{
    shaderReadFormat = shaderReadFormatValue;
    return *this;
}

RenderGraph::ResourceDesc& RenderGraph::ResourceDesc::SetRenderTargetFormat(const DXGI_FORMAT renderTargetFormatValue)
{
    renderTargetFormat = renderTargetFormatValue;
    return *this;
}

RenderGraph::ResourceDesc& RenderGraph::ResourceDesc::SetDepthStencilFormat(const DXGI_FORMAT depthStencilFormatValue)
{
    depthStencilFormat = depthStencilFormatValue;
    return *this;
}

bool RenderGraph::ResourceDesc::operator==(const ResourceDesc& other) const noexcept
{
    return dimension == other.dimension
        && width == other.width
        && height == other.height
        && format == other.format
        && shaderReadFormat == other.shaderReadFormat
        && renderTargetFormat == other.renderTargetFormat
        && depthStencilFormat == other.depthStencilFormat
        && bindFlags == other.bindFlags
        && hasClearValue == other.hasClearValue
        && clearColor == other.clearColor
        && clearDepth == other.clearDepth
        && clearStencil == other.clearStencil;
}

RenderGraph::PassMetadata RenderGraph::PassMetadata::Graphics(std::string debugLabelValue)
{
    return PassMetadata {
        .type = PassType::Graphics,
        .allowCulling = true,
        .enableCpuTiming = true,
        .debugLabel = std::move(debugLabelValue),
    };
}

RenderGraph::PassMetadata RenderGraph::PassMetadata::Fullscreen(std::string debugLabelValue)
{
    return PassMetadata {
        .type = PassType::Fullscreen,
        .allowCulling = true,
        .enableCpuTiming = true,
        .debugLabel = std::move(debugLabelValue),
    };
}

RenderGraph::PassMetadata RenderGraph::PassMetadata::Present(std::string debugLabelValue)
{
    return PassMetadata {
        .type = PassType::Present,
        .allowCulling = false,
        .enableCpuTiming = false,
        .debugLabel = std::move(debugLabelValue),
    };
}

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
    ImportResource(std::move(resourceName), {}, initialState);
}

void RenderGraph::ImportResource(
    std::string resourceName,
    const ResourceDesc& resourceDesc,
    const ResourceState initialState)
{
    if (resourceName.empty())
    {
        throw std::invalid_argument("RenderGraph imported resource requires a non-empty resource name.");
    }

    if (transientResources_.contains(resourceName))
    {
        throw std::invalid_argument("RenderGraph resource cannot be both imported and transient.");
    }

    const auto [iterator, inserted] = importedResources_.emplace(
        std::move(resourceName),
        ResourceDeclaration {
            .desc = resourceDesc,
            .initialState = initialState,
        });
    if (!inserted)
    {
        if (iterator->second.initialState != initialState || !(iterator->second.desc == resourceDesc))
        {
            throw std::invalid_argument(
                "RenderGraph imported resource is declared multiple times with different descriptors or states.");
        }
    }
}

void RenderGraph::DeclareTransientResource(std::string resourceName, const ResourceState initialState)
{
    DeclareTransientResource(std::move(resourceName), {}, initialState);
}

void RenderGraph::DeclareTransientResource(
    std::string resourceName,
    const ResourceDesc& resourceDesc,
    const ResourceState initialState)
{
    if (resourceName.empty())
    {
        throw std::invalid_argument("RenderGraph transient resource requires a non-empty resource name.");
    }

    if (importedResources_.contains(resourceName))
    {
        throw std::invalid_argument("RenderGraph resource cannot be both transient and imported.");
    }

    const auto [iterator, inserted] = transientResources_.emplace(
        std::move(resourceName),
        ResourceDeclaration {
            .desc = resourceDesc,
            .initialState = initialState,
        });
    if (!inserted)
    {
        if (iterator->second.initialState != initialState || !(iterator->second.desc == resourceDesc))
        {
            throw std::invalid_argument(
                "RenderGraph transient resource is declared multiple times with different descriptors or states.");
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
    AddPass(std::move(name), {}, PassMetadata {}, std::move(execute));
}

void RenderGraph::AddPass(std::string name, PassMetadata metadata, ExecuteCallback execute)
{
    AddPass(std::move(name), {}, std::move(metadata), std::move(execute));
}

void RenderGraph::AddPass(std::string name, std::initializer_list<ResourceUsage> resources, ExecuteCallback execute)
{
    AddPass(std::move(name), resources, PassMetadata {}, std::move(execute));
}

void RenderGraph::AddPass(
    std::string name,
    std::initializer_list<ResourceUsage> resources,
    PassMetadata metadata,
    ExecuteCallback execute)
{
    if (!execute)
    {
        throw std::invalid_argument("RenderGraph pass requires a valid execute callback.");
    }

    if (name.empty())
    {
        throw std::invalid_argument("RenderGraph pass requires a non-empty name.");
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
        .metadata = std::move(metadata),
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
            .culled = false,
            .name = pass.name,
            .metadata = pass.metadata,
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
        compiledResource.physicalResourceIndex = kInvalidPhysicalResourceIndex;

        if (const auto imported = importedResources_.find(resourceName); imported != importedResources_.end())
        {
            compiledResource.kind = ResourceKind::Imported;
            compiledResource.desc = imported->second.desc;
            compiledResource.imported = true;
            compiledResource.initialState = imported->second.initialState;
        }
        else if (const auto transient = transientResources_.find(resourceName); transient != transientResources_.end())
        {
            compiledResource.kind = ResourceKind::Transient;
            compiledResource.desc = transient->second.desc;
            compiledResource.initialState = transient->second.initialState;
        }

        compiledResource.exported = exportedResources_.contains(resourceName);

        const std::size_t resourceIndex = result.resources.size();
        resourceSummaryIndices.emplace(resourceName, resourceIndex);
        result.resources.push_back(std::move(compiledResource));
        return result.resources.back();
    };

    for (const auto& [resourceName, declaration] : importedResources_)
    {
        dependencyResources[resourceName].available = true;
        ensureCompiledResource(resourceName);
        (void)declaration;
    }

    for (const auto& [resourceName, declaration] : transientResources_)
    {
        dependencyResources[resourceName].available = declaration.initialState != ResourceState::Unknown;
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
        if (!requiredPasses[passIndex] && result.passes[passIndex].metadata.allowCulling)
        {
            result.passes[passIndex].culled = true;
            result.culledPassIndices.push_back(passIndex);
        }
        else if (!result.passes[passIndex].metadata.allowCulling)
        {
            requiredPasses[passIndex] = true;
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

        for (const std::uint32_t dependencyPassIndex : result.passes[passIndex].dependencyPassIndices)
        {
            if (!requiredPasses[dependencyPassIndex])
            {
                continue;
            }

            ++pendingDependencyCounts[passIndex];
            dependentPassIndices[dependencyPassIndex].push_back(passIndex);
        }
    }

    std::vector<bool> emittedPasses(result.passes.size(), false);
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

    struct TransientLifetime
    {
        std::size_t resourceIndex = 0;
        std::uint32_t firstPassIndex = kInvalidPassIndex;
    };

    std::vector<TransientLifetime> transientLifetimes;
    transientLifetimes.reserve(result.resources.size());
    for (std::size_t resourceIndex = 0; resourceIndex < result.resources.size(); ++resourceIndex)
    {
        const CompileResult::CompiledResource& compiledResource = result.resources[resourceIndex];
        if (compiledResource.kind != ResourceKind::Transient || compiledResource.firstPassIndex == kInvalidPassIndex)
        {
            continue;
        }

        transientLifetimes.push_back(TransientLifetime {
            .resourceIndex = resourceIndex,
            .firstPassIndex = compiledResource.firstPassIndex,
        });
    }

    std::sort(
        transientLifetimes.begin(),
        transientLifetimes.end(),
        [&](const TransientLifetime& left, const TransientLifetime& right)
        {
            const auto& leftResource = result.resources[left.resourceIndex];
            const auto& rightResource = result.resources[right.resourceIndex];
            if (left.firstPassIndex != right.firstPassIndex)
            {
                return left.firstPassIndex < right.firstPassIndex;
            }

            return leftResource.name < rightResource.name;
        });

    struct PhysicalAllocationCandidate
    {
        std::uint32_t physicalResourceIndex = 0;
        ResourceDesc desc = {};
        std::uint32_t lastPassIndex = kInvalidPassIndex;
    };

    std::vector<PhysicalAllocationCandidate> physicalAllocationCandidates;
    for (const TransientLifetime& transientLifetime : transientLifetimes)
    {
        CompileResult::CompiledResource& compiledResource = result.resources[transientLifetime.resourceIndex];
        PhysicalAllocationCandidate* selectedCandidate = nullptr;
        for (PhysicalAllocationCandidate& candidate : physicalAllocationCandidates)
        {
            if (candidate.lastPassIndex < compiledResource.firstPassIndex && candidate.desc == compiledResource.desc)
            {
                selectedCandidate = &candidate;
                break;
            }
        }

        if (selectedCandidate == nullptr)
        {
            const std::uint32_t physicalResourceIndex = static_cast<std::uint32_t>(result.physicalResources.size());
            result.physicalResources.push_back(CompileResult::PhysicalResource {
                .physicalResourceIndex = physicalResourceIndex,
                .desc = compiledResource.desc,
                .initialState = compiledResource.initialState,
                .firstPassIndex = compiledResource.firstPassIndex,
                .lastPassIndex = compiledResource.lastPassIndex,
                .aliasedLogicalResources = {compiledResource.name},
            });
            physicalAllocationCandidates.push_back(PhysicalAllocationCandidate {
                .physicalResourceIndex = physicalResourceIndex,
                .desc = compiledResource.desc,
                .lastPassIndex = compiledResource.lastPassIndex,
            });
            compiledResource.physicalResourceIndex = physicalResourceIndex;
            continue;
        }

        CompileResult::PhysicalResource& physicalResource =
            result.physicalResources[selectedCandidate->physicalResourceIndex];
        physicalResource.firstPassIndex = std::min(physicalResource.firstPassIndex, compiledResource.firstPassIndex);
        physicalResource.lastPassIndex = std::max(physicalResource.lastPassIndex, compiledResource.lastPassIndex);
        physicalResource.aliasedLogicalResources.push_back(compiledResource.name);
        selectedCandidate->lastPassIndex = compiledResource.lastPassIndex;
        compiledResource.physicalResourceIndex = selectedCandidate->physicalResourceIndex;
    }

    std::sort(
        result.resources.begin(),
        result.resources.end(),
        [](const CompileResult::CompiledResource& left, const CompileResult::CompiledResource& right)
        {
            return left.name < right.name;
        });

    std::sort(
        result.physicalResources.begin(),
        result.physicalResources.end(),
        [](const CompileResult::PhysicalResource& left, const CompileResult::PhysicalResource& right)
        {
            return left.physicalResourceIndex < right.physicalResourceIndex;
        });

    for (CompileResult::PhysicalResource& physicalResource : result.physicalResources)
    {
        std::sort(physicalResource.aliasedLogicalResources.begin(), physicalResource.aliasedLogicalResources.end());
    }

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
            const ResourceDeclaration& declaration = importedResources_.at(resourceName);
            description << "    " << resourceName << " (" << ToString(declaration.initialState) << ")";
            if (declaration.desc.dimension != ResourceDimension::Unknown)
            {
                description << " [" << DescribeResourceDesc(declaration.desc) << "]";
            }
            description << '\n';
        }
    }

    const auto transientNames = GetSortedKeys(transientResources_);
    if (!transientNames.empty())
    {
        description << "  Transient:\n";
        for (const std::string& resourceName : transientNames)
        {
            const ResourceDeclaration& declaration = transientResources_.at(resourceName);
            description << "    " << resourceName << " (" << ToString(declaration.initialState) << ")";
            if (declaration.desc.dimension != ResourceDimension::Unknown)
            {
                description << " [" << DescribeResourceDesc(declaration.desc) << "]";
            }
            description << '\n';
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

        description << "      meta: type=" << ToString(pass.metadata.type)
                    << " cull=" << (pass.metadata.allowCulling ? "on" : "off")
                    << " cpuTiming=" << (pass.metadata.enableCpuTiming ? "on" : "off");
        if (!pass.metadata.debugLabel.empty())
        {
            description << " label=" << pass.metadata.debugLabel;
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

            description << "      desc: " << DescribeResourceDesc(resource.desc) << '\n';
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

            description << "      physical:";
            if (resource.physicalResourceIndex == kInvalidPhysicalResourceIndex)
            {
                description << " n/a";
            }
            else
            {
                description << ' ' << resource.physicalResourceIndex;
            }
            description << '\n';
        }
    }

    if (!compileResult.physicalResources.empty())
    {
        description << "  PhysicalResources:\n";
        for (const CompileResult::PhysicalResource& physicalResource : compileResult.physicalResources)
        {
            description << "    [" << physicalResource.physicalResourceIndex << "] "
                        << DescribeResourceDesc(physicalResource.desc) << '\n';

            description << "      aliases:";
            for (const std::string& logicalResourceName : physicalResource.aliasedLogicalResources)
            {
                description << ' ' << logicalResourceName;
            }
            description << '\n';

            description << "      lifetime: first=" << compileResult.passes[physicalResource.firstPassIndex].name
                        << " last=" << compileResult.passes[physicalResource.lastPassIndex].name << '\n';
            description << "      initial: " << ToString(physicalResource.initialState) << '\n';
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

std::string RenderGraph::DescribeGraphviz() const
{
    return DescribeGraphviz(Compile());
}

std::string RenderGraph::DescribeGraphviz(const CompileResult& compileResult) const
{
    std::ostringstream graphviz;
    graphviz << "digraph RenderGraph {\n";
    graphviz << "  rankdir=LR;\n";
    graphviz << "  graph [fontname=\"Consolas\", labelloc=t, label=\"UGC Renderer Render Graph\"];\n";
    graphviz << "  node [fontname=\"Consolas\"];\n";
    graphviz << "  edge [fontname=\"Consolas\"];\n\n";

    graphviz << "  subgraph cluster_passes {\n";
    graphviz << "    label=\"Passes\";\n";
    graphviz << "    color=\"#56697A\";\n";
    graphviz << "    style=\"rounded\";\n";
    for (std::size_t passIndex = 0; passIndex < compileResult.passes.size(); ++passIndex)
    {
        const CompiledPass& pass = compileResult.passes[passIndex];
        std::string label = "[" + std::to_string(passIndex) + "] " + pass.name;
        label += "\n";
        label += ToString(pass.metadata.type);
        if (!pass.metadata.debugLabel.empty() && pass.metadata.debugLabel != pass.name)
        {
            label += "\n";
            label += pass.metadata.debugLabel;
        }
        if (pass.culled)
        {
            label += "\nculled";
        }

        const char* fillColor = "#D9DEE4";
        switch (pass.metadata.type)
        {
        case PassType::Graphics:
            fillColor = pass.culled ? "#D7DBE0" : "#BFD9FF";
            break;
        case PassType::Fullscreen:
            fillColor = pass.culled ? "#DCD7E2" : "#D8C9FF";
            break;
        case PassType::Present:
            fillColor = pass.culled ? "#D7E2D7" : "#BDE6C1";
            break;
        case PassType::Generic:
            fillColor = pass.culled ? "#E0E0E0" : "#D9DEE4";
            break;
        }

        graphviz << "    pass_" << passIndex
                 << " [shape=box, style=\"rounded,filled\", fillcolor=\"" << fillColor
                 << "\", label=\"" << EscapeGraphvizLabel(label) << "\"];\n";
    }
    graphviz << "  }\n\n";

    graphviz << "  subgraph cluster_resources {\n";
    graphviz << "    label=\"Logical Resources\";\n";
    graphviz << "    color=\"#7A5B56\";\n";
    graphviz << "    style=\"rounded\";\n";
    for (std::size_t resourceIndex = 0; resourceIndex < compileResult.resources.size(); ++resourceIndex)
    {
        const CompileResult::CompiledResource& resource = compileResult.resources[resourceIndex];
        std::string label = resource.name;
        label += "\n";
        label += ToString(resource.kind);
        if (resource.exported)
        {
            label += "\nexported";
        }
        label += "\n";
        label += DescribeResourceDesc(resource.desc);
        label += "\n";
        label += std::string("initial=") + ToString(resource.initialState);
        label += "\n";
        label += std::string("lifetime=") + std::to_string(resource.firstPassIndex) + "->"
            + std::to_string(resource.lastPassIndex);

        const char* fillColor = "#EFE7D7";
        switch (resource.kind)
        {
        case ResourceKind::Imported:
            fillColor = "#FFE8B8";
            break;
        case ResourceKind::Transient:
            fillColor = "#FFD4C7";
            break;
        case ResourceKind::Internal:
            fillColor = "#EFE7D7";
            break;
        }

        graphviz << "    resource_" << resourceIndex
                 << " [shape=ellipse, style=\"filled\", fillcolor=\"" << fillColor
                 << "\", label=\"" << EscapeGraphvizLabel(label) << "\"];\n";
    }
    graphviz << "  }\n\n";

    if (!compileResult.physicalResources.empty())
    {
        graphviz << "  subgraph cluster_physical {\n";
        graphviz << "    label=\"Physical Resources\";\n";
        graphviz << "    color=\"#5E5A84\";\n";
        graphviz << "    style=\"rounded\";\n";
        for (const CompileResult::PhysicalResource& physicalResource : compileResult.physicalResources)
        {
            std::string label = "[" + std::to_string(physicalResource.physicalResourceIndex) + "]";
            label += "\n";
            label += DescribeResourceDesc(physicalResource.desc);
            label += "\n";
            label += std::string("lifetime=") + std::to_string(physicalResource.firstPassIndex) + "->"
                + std::to_string(physicalResource.lastPassIndex);

            graphviz << "    physical_" << physicalResource.physicalResourceIndex
                     << " [shape=cylinder, style=\"filled\", fillcolor=\"#DAD4FF\", label=\""
                     << EscapeGraphvizLabel(label) << "\"];\n";
        }
        graphviz << "  }\n\n";
    }

    std::unordered_map<std::string, std::size_t> resourceIndices;
    resourceIndices.reserve(compileResult.resources.size());
    for (std::size_t resourceIndex = 0; resourceIndex < compileResult.resources.size(); ++resourceIndex)
    {
        resourceIndices.emplace(compileResult.resources[resourceIndex].name, resourceIndex);
    }

    for (std::size_t passIndex = 0; passIndex < compileResult.passes.size(); ++passIndex)
    {
        const CompiledPass& pass = compileResult.passes[passIndex];
        for (const ResourceUsage& usage : pass.resources)
        {
            const auto resourceIndexIterator = resourceIndices.find(usage.resourceName);
            if (resourceIndexIterator == resourceIndices.end())
            {
                continue;
            }

            const std::size_t resourceIndex = resourceIndexIterator->second;
            std::string label = ToString(usage.access);
            if (usage.desiredState != ResourceState::Unknown)
            {
                label += "\n";
                label += ToString(usage.desiredState);
            }

            if (usage.access == ResourceAccess::Read)
            {
                graphviz << "  resource_" << resourceIndex << " -> pass_" << passIndex
                         << " [color=\"#4F6D8A\", label=\"" << EscapeGraphvizLabel(label) << "\"];\n";
            }
            else if (usage.access == ResourceAccess::Write)
            {
                graphviz << "  pass_" << passIndex << " -> resource_" << resourceIndex
                         << " [color=\"#9B4D3A\", label=\"" << EscapeGraphvizLabel(label) << "\"];\n";
            }
            else
            {
                graphviz << "  pass_" << passIndex << " -> resource_" << resourceIndex
                         << " [color=\"#7E4D8A\", dir=both, label=\"" << EscapeGraphvizLabel(label) << "\"];\n";
            }
        }
    }

    for (std::size_t resourceIndex = 0; resourceIndex < compileResult.resources.size(); ++resourceIndex)
    {
        const CompileResult::CompiledResource& resource = compileResult.resources[resourceIndex];
        if (resource.physicalResourceIndex == kInvalidPhysicalResourceIndex)
        {
            continue;
        }

        graphviz << "  resource_" << resourceIndex << " -> physical_" << resource.physicalResourceIndex
                 << " [style=dashed, arrowhead=none, color=\"#8A62CC\", label=\"alias\"];\n";
    }

    graphviz << "}\n";
    return graphviz.str();
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
