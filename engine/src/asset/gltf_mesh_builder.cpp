#include "ugc_renderer/asset/gltf_mesh_builder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace ugc_renderer
{
namespace
{
constexpr std::uint32_t kComponentTypeUnsignedByte = 5121;
constexpr std::uint32_t kComponentTypeUnsignedShort = 5123;
constexpr std::uint32_t kComponentTypeUnsignedInt = 5125;
constexpr std::uint32_t kComponentTypeFloat = 5126;

std::vector<std::byte> ReadBinaryFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Failed to open glTF binary buffer.");
    }

    std::vector<char> bytes {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>(),
    };
    std::vector<std::byte> result(bytes.size());
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

std::vector<std::vector<std::byte>> LoadExternalBuffers(const GltfDocument& document)
{
    std::vector<std::vector<std::byte>> buffers;
    buffers.reserve(document.buffers.size());

    for (const GltfBuffer& buffer : document.buffers)
    {
        if (buffer.isDataUri)
        {
            throw std::runtime_error("glTF data URI buffers are not supported yet.");
        }
        if (buffer.resolvedPath.empty())
        {
            throw std::runtime_error("glTF buffer URI is empty.");
        }

        auto bytes = ReadBinaryFile(buffer.resolvedPath);
        if (bytes.size() < buffer.byteLength)
        {
            throw std::runtime_error("glTF binary buffer is smaller than declared byteLength.");
        }

        buffers.push_back(std::move(bytes));
    }

    return buffers;
}

std::size_t GetComponentSize(const std::uint32_t componentType)
{
    switch (componentType)
    {
    case kComponentTypeUnsignedByte:
        return sizeof(std::uint8_t);
    case kComponentTypeUnsignedShort:
        return sizeof(std::uint16_t);
    case kComponentTypeUnsignedInt:
        return sizeof(std::uint32_t);
    case kComponentTypeFloat:
        return sizeof(float);
    default:
        throw std::runtime_error("Unsupported glTF accessor component type.");
    }
}

std::size_t GetComponentCount(const std::string& type)
{
    if (type == "SCALAR")
    {
        return 1;
    }
    if (type == "VEC2")
    {
        return 2;
    }
    if (type == "VEC3")
    {
        return 3;
    }
    if (type == "VEC4")
    {
        return 4;
    }

    throw std::runtime_error("Unsupported glTF accessor type.");
}

const GltfAccessor& GetAccessor(const GltfDocument& document, const std::uint32_t accessorIndex)
{
    if (accessorIndex == kInvalidGltfIndex || accessorIndex >= document.accessors.size())
    {
        throw std::runtime_error("glTF accessor index is invalid.");
    }

    return document.accessors[accessorIndex];
}

const GltfBufferView& GetBufferView(const GltfDocument& document, const GltfAccessor& accessor)
{
    if (accessor.bufferView == kInvalidGltfIndex || accessor.bufferView >= document.bufferViews.size())
    {
        throw std::runtime_error("glTF accessor bufferView is invalid.");
    }

    return document.bufferViews[accessor.bufferView];
}

const std::byte* GetElementPointer(
    const GltfDocument& document,
    const std::vector<std::vector<std::byte>>& buffers,
    const GltfAccessor& accessor,
    const std::uint32_t elementIndex)
{
    const GltfBufferView& bufferView = GetBufferView(document, accessor);
    if (bufferView.buffer >= buffers.size())
    {
        throw std::runtime_error("glTF bufferView buffer index is invalid.");
    }

    const std::size_t componentSize = GetComponentSize(accessor.componentType);
    const std::size_t elementSize = componentSize * GetComponentCount(accessor.type);
    const std::size_t stride = bufferView.byteStride != 0 ? bufferView.byteStride : elementSize;
    const std::size_t byteOffset =
        static_cast<std::size_t>(bufferView.byteOffset) +
        static_cast<std::size_t>(accessor.byteOffset) +
        static_cast<std::size_t>(elementIndex) * stride;

    const auto& buffer = buffers[bufferView.buffer];
    if (byteOffset + elementSize > buffer.size())
    {
        throw std::runtime_error("glTF accessor element points outside binary buffer.");
    }

    return buffer.data() + byteOffset;
}

float ReadFloat(const std::byte* source)
{
    float value = 0.0f;
    std::memcpy(&value, source, sizeof(float));
    return value;
}

std::uint32_t ReadIndex(const GltfAccessor& accessor, const std::byte* source)
{
    if (accessor.componentType == kComponentTypeUnsignedByte)
    {
        return static_cast<std::uint32_t>(*reinterpret_cast<const std::uint8_t*>(source));
    }
    if (accessor.componentType == kComponentTypeUnsignedShort)
    {
        std::uint16_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return value;
    }
    if (accessor.componentType == kComponentTypeUnsignedInt)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return value;
    }

    throw std::runtime_error("glTF index accessor must use an unsigned integer component type.");
}

std::uint32_t FindRequiredAttribute(const GltfPrimitive& primitive, const std::string& name)
{
    if (const auto iterator = primitive.attributes.find(name); iterator != primitive.attributes.end())
    {
        return iterator->second;
    }

    throw std::runtime_error("glTF primitive is missing a required attribute.");
}

std::uint32_t FindOptionalAttribute(const GltfPrimitive& primitive, const std::string& name)
{
    if (const auto iterator = primitive.attributes.find(name); iterator != primitive.attributes.end())
    {
        return iterator->second;
    }

    return kInvalidGltfIndex;
}

void DecodePositions(
    const GltfDocument& document,
    const std::vector<std::vector<std::byte>>& buffers,
    const GltfPrimitive& primitive,
    GltfRuntimeMesh& mesh)
{
    const GltfAccessor& accessor = GetAccessor(document, FindRequiredAttribute(primitive, "POSITION"));
    if (accessor.componentType != kComponentTypeFloat || accessor.type != "VEC3")
    {
        throw std::runtime_error("glTF POSITION accessor must be VEC3 float.");
    }

    mesh.vertices.resize(accessor.count);
    for (std::uint32_t vertexIndex = 0; vertexIndex < accessor.count; ++vertexIndex)
    {
        const std::byte* element = GetElementPointer(document, buffers, accessor, vertexIndex);
        mesh.vertices[vertexIndex].position[0] = ReadFloat(element + 0 * sizeof(float));
        mesh.vertices[vertexIndex].position[1] = ReadFloat(element + 1 * sizeof(float));
        mesh.vertices[vertexIndex].position[2] = ReadFloat(element + 2 * sizeof(float));
    }
}

void DecodeTexCoords(
    const GltfDocument& document,
    const std::vector<std::vector<std::byte>>& buffers,
    const GltfPrimitive& primitive,
    GltfRuntimeMesh& mesh)
{
    const std::uint32_t texCoordAccessorIndex = FindOptionalAttribute(primitive, "TEXCOORD_0");
    if (texCoordAccessorIndex == kInvalidGltfIndex)
    {
        for (auto& vertex : mesh.vertices)
        {
            vertex.texCoord[0] = std::clamp(vertex.position[0] + 0.5f, 0.0f, 1.0f);
            vertex.texCoord[1] = std::clamp(0.5f - vertex.position[1], 0.0f, 1.0f);
        }
        return;
    }

    const GltfAccessor& accessor = GetAccessor(document, texCoordAccessorIndex);
    if (accessor.componentType != kComponentTypeFloat || accessor.type != "VEC2" || accessor.count != mesh.vertices.size())
    {
        throw std::runtime_error("glTF TEXCOORD_0 accessor must be VEC2 float and match POSITION count.");
    }

    for (std::uint32_t vertexIndex = 0; vertexIndex < accessor.count; ++vertexIndex)
    {
        const std::byte* element = GetElementPointer(document, buffers, accessor, vertexIndex);
        mesh.vertices[vertexIndex].texCoord[0] = ReadFloat(element + 0 * sizeof(float));
        mesh.vertices[vertexIndex].texCoord[1] = ReadFloat(element + 1 * sizeof(float));
    }
}

void DecodeColors(
    const GltfDocument& document,
    const std::vector<std::vector<std::byte>>& buffers,
    const GltfPrimitive& primitive,
    GltfRuntimeMesh& mesh)
{
    const std::uint32_t colorAccessorIndex = FindOptionalAttribute(primitive, "COLOR_0");
    if (colorAccessorIndex == kInvalidGltfIndex)
    {
        return;
    }

    const GltfAccessor& accessor = GetAccessor(document, colorAccessorIndex);
    if (accessor.componentType != kComponentTypeFloat ||
        (accessor.type != "VEC3" && accessor.type != "VEC4") ||
        accessor.count != mesh.vertices.size())
    {
        throw std::runtime_error("glTF COLOR_0 accessor must be VEC3/VEC4 float and match POSITION count.");
    }

    const std::size_t componentCount = GetComponentCount(accessor.type);
    for (std::uint32_t vertexIndex = 0; vertexIndex < accessor.count; ++vertexIndex)
    {
        const std::byte* element = GetElementPointer(document, buffers, accessor, vertexIndex);
        mesh.vertices[vertexIndex].color[0] = ReadFloat(element + 0 * sizeof(float));
        mesh.vertices[vertexIndex].color[1] = ReadFloat(element + 1 * sizeof(float));
        mesh.vertices[vertexIndex].color[2] = ReadFloat(element + 2 * sizeof(float));
        mesh.vertices[vertexIndex].color[3] = componentCount == 4 ? ReadFloat(element + 3 * sizeof(float)) : 1.0f;
    }
}

void DecodeIndices(
    const GltfDocument& document,
    const std::vector<std::vector<std::byte>>& buffers,
    const GltfPrimitive& primitive,
    GltfRuntimeMesh& mesh)
{
    if (primitive.indices == kInvalidGltfIndex)
    {
        if (mesh.vertices.size() > std::numeric_limits<std::uint16_t>::max())
        {
            throw std::runtime_error("glTF non-indexed mesh is too large for R16 index upload.");
        }

        mesh.indices.resize(mesh.vertices.size());
        for (std::uint32_t index = 0; index < mesh.vertices.size(); ++index)
        {
            mesh.indices[index] = static_cast<std::uint16_t>(index);
        }
        return;
    }

    const GltfAccessor& accessor = GetAccessor(document, primitive.indices);
    if (accessor.type != "SCALAR")
    {
        throw std::runtime_error("glTF index accessor must be SCALAR.");
    }

    mesh.indices.reserve(accessor.count);
    for (std::uint32_t index = 0; index < accessor.count; ++index)
    {
        const std::uint32_t value = ReadIndex(accessor, GetElementPointer(document, buffers, accessor, index));
        if (value > std::numeric_limits<std::uint16_t>::max())
        {
            throw std::runtime_error("glTF mesh index exceeds R16 upload support.");
        }

        mesh.indices.push_back(static_cast<std::uint16_t>(value));
    }
}
} // namespace

GltfRuntimeMesh GltfMeshBuilder::BuildFirstPrimitive(const GltfDocument& document)
{
    static_assert(sizeof(GltfRuntimeVertex) == 36);

    if (document.meshes.empty() || document.meshes.front().primitives.empty())
    {
        throw std::runtime_error("glTF document does not contain a mesh primitive.");
    }

    const GltfPrimitive& primitive = document.meshes.front().primitives.front();
    if (primitive.mode != 4)
    {
        throw std::runtime_error("Only glTF triangle-list primitives are supported for runtime mesh upload.");
    }

    const auto buffers = LoadExternalBuffers(document);

    GltfRuntimeMesh mesh = {};
    mesh.material = primitive.material;
    DecodePositions(document, buffers, primitive, mesh);
    DecodeTexCoords(document, buffers, primitive, mesh);
    DecodeColors(document, buffers, primitive, mesh);
    DecodeIndices(document, buffers, primitive, mesh);
    return mesh;
}
} // namespace ugc_renderer
