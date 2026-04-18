#include "ugc_renderer/asset/gltf_scene_builder.h"

#include <DirectXMath.h>

#include <stdexcept>
#include <vector>

namespace ugc_renderer
{
namespace
{
DirectX::XMMATRIX BuildNodeLocalTransform(const GltfNode& node)
{
    if (node.hasMatrix)
    {
        const DirectX::XMFLOAT4X4 matrix = {
            node.matrix[0], node.matrix[1], node.matrix[2], node.matrix[3],
            node.matrix[4], node.matrix[5], node.matrix[6], node.matrix[7],
            node.matrix[8], node.matrix[9], node.matrix[10], node.matrix[11],
            node.matrix[12], node.matrix[13], node.matrix[14], node.matrix[15]};
        return DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&matrix));
    }

    const DirectX::XMMATRIX scale =
        DirectX::XMMatrixScaling(node.scale[0], node.scale[1], node.scale[2]);
    const DirectX::XMVECTOR rotationQuaternion =
        DirectX::XMVectorSet(node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]);
    const DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationQuaternion(rotationQuaternion);
    const DirectX::XMMATRIX translation =
        DirectX::XMMatrixTranslation(node.translation[0], node.translation[1], node.translation[2]);
    return scale * rotation * translation;
}

std::vector<std::uint32_t> ResolveRootNodes(const GltfDocument& document)
{
    if (document.defaultScene != kInvalidGltfIndex)
    {
        if (document.defaultScene >= document.scenes.size())
        {
            throw std::runtime_error("glTF default scene index is invalid.");
        }

        return document.scenes[document.defaultScene].nodes;
    }

    if (!document.scenes.empty())
    {
        return document.scenes.front().nodes;
    }

    std::vector<bool> referencedAsChild(document.nodes.size(), false);
    for (const GltfNode& node : document.nodes)
    {
        for (const std::uint32_t childIndex : node.children)
        {
            if (childIndex >= document.nodes.size())
            {
                throw std::runtime_error("glTF node child index is invalid.");
            }

            referencedAsChild[childIndex] = true;
        }
    }

    std::vector<std::uint32_t> rootNodes;
    for (std::uint32_t nodeIndex = 0; nodeIndex < document.nodes.size(); ++nodeIndex)
    {
        if (!referencedAsChild[nodeIndex])
        {
            rootNodes.push_back(nodeIndex);
        }
    }

    return rootNodes;
}

void TraverseNode(
    const GltfDocument& document,
    const std::uint32_t nodeIndex,
    const DirectX::XMMATRIX& parentWorldTransform,
    std::vector<GltfSceneMeshInstance>& instances)
{
    if (nodeIndex >= document.nodes.size())
    {
        throw std::runtime_error("glTF node index is invalid.");
    }

    const GltfNode& node = document.nodes[nodeIndex];
    const DirectX::XMMATRIX worldTransform = BuildNodeLocalTransform(node) * parentWorldTransform;

    if (node.mesh != kInvalidGltfIndex)
    {
        if (node.mesh >= document.meshes.size())
        {
            throw std::runtime_error("glTF node mesh index is invalid.");
        }

        GltfSceneMeshInstance instance = {};
        instance.node = nodeIndex;
        instance.mesh = node.mesh;
        DirectX::XMStoreFloat4x4(&instance.worldTransform, worldTransform);
        instances.push_back(instance);
    }

    for (const std::uint32_t childIndex : node.children)
    {
        TraverseNode(document, childIndex, worldTransform, instances);
    }
}
} // namespace

std::vector<GltfSceneMeshInstance> GltfSceneBuilder::BuildMeshInstances(const GltfDocument& document)
{
    std::vector<GltfSceneMeshInstance> instances;
    const std::vector<std::uint32_t> rootNodes = ResolveRootNodes(document);
    instances.reserve(rootNodes.size());

    for (const std::uint32_t nodeIndex : rootNodes)
    {
        TraverseNode(document, nodeIndex, DirectX::XMMatrixIdentity(), instances);
    }

    return instances;
}
} // namespace ugc_renderer
