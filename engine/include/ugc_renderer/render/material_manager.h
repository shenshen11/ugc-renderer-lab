#pragma once

#include "ugc_renderer/render/constant_buffer.h"
#include "ugc_renderer/render/descriptor_allocator.h"
#include "ugc_renderer/render/material.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace ugc_renderer
{
struct MaterialAsset
{
    MaterialDesc desc = {};
    std::unique_ptr<ConstantBuffer> constantBuffer;
    DescriptorAllocation cbvAllocation = {};
};

class MaterialManager
{
public:
    void Initialize(ID3D12Device& device, DescriptorAllocator& cbvAllocator);

    std::uint32_t CreateMaterial(const MaterialDesc& materialDesc);
    MaterialAsset& GetMaterial(std::uint32_t index);
    const MaterialAsset& GetMaterial(std::uint32_t index) const;

private:
    ID3D12Device* device_ = nullptr;
    DescriptorAllocator* cbvAllocator_ = nullptr;
    std::vector<MaterialAsset> materials_;
};
} // namespace ugc_renderer
