#include "ugc_renderer/asset/gltf_mesh_builder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <numeric>
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

void GenerateFlatNormals(GltfRuntimeMesh& mesh)
{
    for (auto& vertex : mesh.vertices)
    {
        vertex.normal[0] = 0.0f;
        vertex.normal[1] = 0.0f;
        vertex.normal[2] = 0.0f;
    }

    for (std::size_t indexOffset = 0; indexOffset + 2 < mesh.indices.size(); indexOffset += 3)
    {
        const std::uint32_t index0 = mesh.indices[indexOffset + 0];
        const std::uint32_t index1 = mesh.indices[indexOffset + 1];
        const std::uint32_t index2 = mesh.indices[indexOffset + 2];

        if (index0 >= mesh.vertices.size() || index1 >= mesh.vertices.size() || index2 >= mesh.vertices.size())
        {
            throw std::runtime_error("glTF mesh index points outside decoded vertex range.");
        }

        const auto& position0 = mesh.vertices[index0].position;
        const auto& position1 = mesh.vertices[index1].position;
        const auto& position2 = mesh.vertices[index2].position;

        const std::array<float, 3> edge0 = {
            position1[0] - position0[0],
            position1[1] - position0[1],
            position1[2] - position0[2]};
        const std::array<float, 3> edge1 = {
            position2[0] - position0[0],
            position2[1] - position0[1],
            position2[2] - position0[2]};
        const std::array<float, 3> faceNormal = {
            edge0[1] * edge1[2] - edge0[2] * edge1[1],
            edge0[2] * edge1[0] - edge0[0] * edge1[2],
            edge0[0] * edge1[1] - edge0[1] * edge1[0]};

        for (const std::uint32_t vertexIndex : {index0, index1, index2})
        {
            mesh.vertices[vertexIndex].normal[0] += faceNormal[0];
            mesh.vertices[vertexIndex].normal[1] += faceNormal[1];
            mesh.vertices[vertexIndex].normal[2] += faceNormal[2];
        }
    }

    for (auto& vertex : mesh.vertices)
    {
        const float lengthSquared =
            vertex.normal[0] * vertex.normal[0] +
            vertex.normal[1] * vertex.normal[1] +
            vertex.normal[2] * vertex.normal[2];
        if (lengthSquared <= std::numeric_limits<float>::epsilon())
        {
            vertex.normal[0] = 0.0f;
            vertex.normal[1] = 0.0f;
            vertex.normal[2] = 1.0f;
            continue;
        }

        const float inverseLength = 1.0f / std::sqrt(lengthSquared);
        vertex.normal[0] *= inverseLength;
        vertex.normal[1] *= inverseLength;
        vertex.normal[2] *= inverseLength;
    }
}

void DecodeNormals(
    const GltfDocument& document,
    const std::vector<std::vector<std::byte>>& buffers,
    const GltfPrimitive& primitive,
    GltfRuntimeMesh& mesh)
{
    const std::uint32_t normalAccessorIndex = FindOptionalAttribute(primitive, "NORMAL");
    if (normalAccessorIndex == kInvalidGltfIndex)
    {
        GenerateFlatNormals(mesh);
        return;
    }

    const GltfAccessor& accessor = GetAccessor(document, normalAccessorIndex);
    if (accessor.componentType != kComponentTypeFloat || accessor.type != "VEC3" || accessor.count != mesh.vertices.size())
    {
        throw std::runtime_error("glTF NORMAL accessor must be VEC3 float and match POSITION count.");
    }

    for (std::uint32_t vertexIndex = 0; vertexIndex < accessor.count; ++vertexIndex)
    {
        const std::byte* element = GetElementPointer(document, buffers, accessor, vertexIndex);
        mesh.vertices[vertexIndex].normal[0] = ReadFloat(element + 0 * sizeof(float));
        mesh.vertices[vertexIndex].normal[1] = ReadFloat(element + 1 * sizeof(float));
        mesh.vertices[vertexIndex].normal[2] = ReadFloat(element + 2 * sizeof(float));
    }
}

std::array<float, 3> MakeVector3(const float x, const float y, const float z)
{
    return {x, y, z};
}

std::array<float, 3> Subtract(const std::array<float, 3>& left, const std::array<float, 3>& right)
{
    return {
        left[0] - right[0],
        left[1] - right[1],
        left[2] - right[2]};
}

std::array<float, 3> Add(const std::array<float, 3>& left, const std::array<float, 3>& right)
{
    return {
        left[0] + right[0],
        left[1] + right[1],
        left[2] + right[2]};
}

std::array<float, 3> Multiply(const std::array<float, 3>& vector, const float scalar)
{
    return {
        vector[0] * scalar,
        vector[1] * scalar,
        vector[2] * scalar};
}

float Dot(const std::array<float, 3>& left, const std::array<float, 3>& right)
{
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

std::array<float, 3> Cross(const std::array<float, 3>& left, const std::array<float, 3>& right)
{
    return {
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0]};
}

float LengthSquared(const std::array<float, 3>& vector)
{
    return Dot(vector, vector);
}

std::array<float, 3> Normalize(const std::array<float, 3>& vector)
{
    const float lengthSquared = LengthSquared(vector);
    if (lengthSquared <= std::numeric_limits<float>::epsilon())
    {
        return {0.0f, 0.0f, 1.0f};
    }

    return Multiply(vector, 1.0f / std::sqrt(lengthSquared));
}

std::array<float, 3> LoadPosition(const GltfRuntimeVertex& vertex)
{
    return {vertex.position[0], vertex.position[1], vertex.position[2]};
}

std::array<float, 3> LoadNormal(const GltfRuntimeVertex& vertex)
{
    return {vertex.normal[0], vertex.normal[1], vertex.normal[2]};
}

std::array<float, 2> LoadTexCoord(const GltfRuntimeVertex& vertex)
{
    return {vertex.texCoord[0], vertex.texCoord[1]};
}

std::array<float, 3> BuildFallbackTangent(const std::array<float, 3>& normal)
{
    const std::array<float, 3> referenceAxis =
        std::abs(normal[2]) < 0.999f ? MakeVector3(0.0f, 0.0f, 1.0f) : MakeVector3(0.0f, 1.0f, 0.0f);
    return Normalize(Cross(referenceAxis, normal));
}

void GenerateTangents(GltfRuntimeMesh& mesh)
{
    std::vector<std::array<float, 3>> accumulatedTangents(mesh.vertices.size(), MakeVector3(0.0f, 0.0f, 0.0f));
    std::vector<std::array<float, 3>> accumulatedBitangents(mesh.vertices.size(), MakeVector3(0.0f, 0.0f, 0.0f));

    for (std::size_t indexOffset = 0; indexOffset + 2 < mesh.indices.size(); indexOffset += 3)
    {
        const std::uint32_t index0 = mesh.indices[indexOffset + 0];
        const std::uint32_t index1 = mesh.indices[indexOffset + 1];
        const std::uint32_t index2 = mesh.indices[indexOffset + 2];

        if (index0 >= mesh.vertices.size() || index1 >= mesh.vertices.size() || index2 >= mesh.vertices.size())
        {
            throw std::runtime_error("glTF mesh index points outside decoded vertex range.");
        }

        const auto position0 = LoadPosition(mesh.vertices[index0]);
        const auto position1 = LoadPosition(mesh.vertices[index1]);
        const auto position2 = LoadPosition(mesh.vertices[index2]);
        const auto texCoord0 = LoadTexCoord(mesh.vertices[index0]);
        const auto texCoord1 = LoadTexCoord(mesh.vertices[index1]);
        const auto texCoord2 = LoadTexCoord(mesh.vertices[index2]);

        const auto edge1 = Subtract(position1, position0);
        const auto edge2 = Subtract(position2, position0);
        const float deltaU1 = texCoord1[0] - texCoord0[0];
        const float deltaV1 = texCoord1[1] - texCoord0[1];
        const float deltaU2 = texCoord2[0] - texCoord0[0];
        const float deltaV2 = texCoord2[1] - texCoord0[1];
        const float denominator = deltaU1 * deltaV2 - deltaV1 * deltaU2;

        if (std::abs(denominator) <= std::numeric_limits<float>::epsilon())
        {
            continue;
        }

        const float inverseDeterminant = 1.0f / denominator;
        const auto tangent = MakeVector3(
            inverseDeterminant * (deltaV2 * edge1[0] - deltaV1 * edge2[0]),
            inverseDeterminant * (deltaV2 * edge1[1] - deltaV1 * edge2[1]),
            inverseDeterminant * (deltaV2 * edge1[2] - deltaV1 * edge2[2]));
        const auto bitangent = MakeVector3(
            inverseDeterminant * (-deltaU2 * edge1[0] + deltaU1 * edge2[0]),
            inverseDeterminant * (-deltaU2 * edge1[1] + deltaU1 * edge2[1]),
            inverseDeterminant * (-deltaU2 * edge1[2] + deltaU1 * edge2[2]));

        for (const std::uint32_t vertexIndex : {index0, index1, index2})
        {
            accumulatedTangents[vertexIndex] = Add(accumulatedTangents[vertexIndex], tangent);
            accumulatedBitangents[vertexIndex] = Add(accumulatedBitangents[vertexIndex], bitangent);
        }
    }

    for (std::size_t vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex)
    {
        const auto normal = Normalize(LoadNormal(mesh.vertices[vertexIndex]));
        auto tangent = accumulatedTangents[vertexIndex];
        auto bitangent = accumulatedBitangents[vertexIndex];

        if (LengthSquared(tangent) <= std::numeric_limits<float>::epsilon())
        {
            tangent = BuildFallbackTangent(normal);
        }

        tangent = Normalize(Subtract(tangent, Multiply(normal, Dot(normal, tangent))));

        if (LengthSquared(bitangent) <= std::numeric_limits<float>::epsilon())
        {
            bitangent = Cross(normal, tangent);
        }

        const float tangentSign = Dot(Cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
        mesh.vertices[vertexIndex].tangent[0] = tangent[0];
        mesh.vertices[vertexIndex].tangent[1] = tangent[1];
        mesh.vertices[vertexIndex].tangent[2] = tangent[2];
        mesh.vertices[vertexIndex].tangent[3] = tangentSign;
    }
}

void DecodeTangents(
    const GltfDocument& document,
    const std::vector<std::vector<std::byte>>& buffers,
    const GltfPrimitive& primitive,
    GltfRuntimeMesh& mesh)
{
    const std::uint32_t tangentAccessorIndex = FindOptionalAttribute(primitive, "TANGENT");
    if (tangentAccessorIndex == kInvalidGltfIndex)
    {
        GenerateTangents(mesh);
        return;
    }

    const GltfAccessor& accessor = GetAccessor(document, tangentAccessorIndex);
    if (accessor.componentType != kComponentTypeFloat || accessor.type != "VEC4" || accessor.count != mesh.vertices.size())
    {
        throw std::runtime_error("glTF TANGENT accessor must be VEC4 float and match POSITION count.");
    }

    for (std::uint32_t vertexIndex = 0; vertexIndex < accessor.count; ++vertexIndex)
    {
        const std::byte* element = GetElementPointer(document, buffers, accessor, vertexIndex);
        mesh.vertices[vertexIndex].tangent[0] = ReadFloat(element + 0 * sizeof(float));
        mesh.vertices[vertexIndex].tangent[1] = ReadFloat(element + 1 * sizeof(float));
        mesh.vertices[vertexIndex].tangent[2] = ReadFloat(element + 2 * sizeof(float));
        mesh.vertices[vertexIndex].tangent[3] = ReadFloat(element + 3 * sizeof(float));
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

GltfRuntimeMesh GltfMeshBuilder::BuildPrimitive(
    const GltfDocument& document,
    const std::uint32_t meshIndex,
    const std::uint32_t primitiveIndex)
{
    static_assert(sizeof(GltfRuntimeVertex) == 64);

    if (meshIndex >= document.meshes.size())
    {
        throw std::runtime_error("glTF mesh index is invalid.");
    }

    const GltfMesh& meshDefinition = document.meshes[meshIndex];
    if (primitiveIndex >= meshDefinition.primitives.size())
    {
        throw std::runtime_error("glTF primitive index is invalid.");
    }

    const GltfPrimitive& primitive = meshDefinition.primitives[primitiveIndex];
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
    DecodeNormals(document, buffers, primitive, mesh);
    DecodeTangents(document, buffers, primitive, mesh);
    return mesh;
}

GltfRuntimeMesh GltfMeshBuilder::BuildFirstPrimitive(const GltfDocument& document)
{
    if (document.meshes.empty() || document.meshes.front().primitives.empty())
    {
        throw std::runtime_error("glTF document does not contain a mesh primitive.");
    }

    return BuildPrimitive(document, 0, 0);
}
} // namespace ugc_renderer
