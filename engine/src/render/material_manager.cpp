#include "ugc_renderer/render/material_manager.h"

#include "ugc_renderer/core/throw_if_failed.h"

#include <span>
#include <stdexcept>
#include <string>

namespace ugc_renderer
{
void MaterialManager::Initialize(ID3D12Device& device, DescriptorAllocator& cbvAllocator)
{
    device_ = &device;
    cbvAllocator_ = &cbvAllocator;
}

std::uint32_t MaterialManager::CreateMaterial(const MaterialDesc& materialDesc, const std::string_view name)
{
    if (device_ == nullptr || cbvAllocator_ == nullptr)
    {
        throw std::logic_error("MaterialManager must be initialized before creating materials.");
    }

    MaterialAsset materialAsset = {};
    materialAsset.name = name.empty() ? "Material " + std::to_string(materials_.size()) : std::string(name);
    materialAsset.desc = materialDesc;
    materialAsset.constantBuffer = std::make_unique<ConstantBuffer>();
    materialAsset.constantBuffer->Initialize(*device_, sizeof(MaterialConstants));
    materialAsset.constantBuffer->Update(std::as_bytes(std::span {&materialAsset.desc.constants, 1}));
    materialAsset.cbvAllocation = cbvAllocator_->Allocate(1);

    D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc = {};
    constantBufferViewDesc.BufferLocation = materialAsset.constantBuffer->GetGpuVirtualAddress();
    constantBufferViewDesc.SizeInBytes = materialAsset.constantBuffer->GetAlignedSizeInBytes();
    device_->CreateConstantBufferView(&constantBufferViewDesc, materialAsset.cbvAllocation.GetCpuHandle());

    const std::uint32_t index = static_cast<std::uint32_t>(materials_.size());
    materials_.push_back(std::move(materialAsset));
    return index;
}

void MaterialManager::UpdateMaterial(const std::uint32_t index, const MaterialDesc& materialDesc)
{
    MaterialAsset& materialAsset = materials_.at(index);
    materialAsset.desc = materialDesc;
    materialAsset.constantBuffer->Update(std::as_bytes(std::span {&materialAsset.desc.constants, 1}));
}

MaterialAsset& MaterialManager::GetMaterial(const std::uint32_t index)
{
    return materials_.at(index);
}

const MaterialAsset& MaterialManager::GetMaterial(const std::uint32_t index) const
{
    return materials_.at(index);
}
} // namespace ugc_renderer
