#include "ugc_renderer/asset/gltf_loader.h"

#include "ugc_renderer/asset/json_parser.h"

#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ugc_renderer
{
namespace
{
std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Failed to open glTF file.");
    }

    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

const JsonValue::Object& ExpectObject(const JsonValue& value, const std::string_view context)
{
    if (!value.IsObject())
    {
        throw std::runtime_error(std::string(context) + " must be a JSON object.");
    }

    return value.AsObject();
}

const JsonValue::Array& ExpectArray(const JsonValue& value, const std::string_view context)
{
    if (!value.IsArray())
    {
        throw std::runtime_error(std::string(context) + " must be a JSON array.");
    }

    return value.AsArray();
}

const JsonValue* FindMember(const JsonValue::Object& object, const std::string_view key)
{
    if (const auto iterator = object.find(std::string(key)); iterator != object.end())
    {
        return &iterator->second;
    }

    return nullptr;
}

std::string GetString(const JsonValue::Object& object, const std::string_view key, const std::string_view defaultValue = {})
{
    if (const JsonValue* member = FindMember(object, key); member != nullptr)
    {
        if (!member->IsString())
        {
            throw std::runtime_error("Expected string member in glTF JSON.");
        }

        return member->AsString();
    }

    return std::string(defaultValue);
}

std::uint32_t GetUInt32(const JsonValue::Object& object, const std::string_view key, const std::uint32_t defaultValue)
{
    if (const JsonValue* member = FindMember(object, key); member != nullptr)
    {
        if (!member->IsNumber())
        {
            throw std::runtime_error("Expected numeric member in glTF JSON.");
        }

        const double numericValue = member->AsNumber();
        if (numericValue < 0.0 || std::floor(numericValue) != numericValue)
        {
            throw std::runtime_error("Expected unsigned integer member in glTF JSON.");
        }

        return static_cast<std::uint32_t>(numericValue);
    }

    return defaultValue;
}

float GetFloat(const JsonValue::Object& object, const std::string_view key, const float defaultValue)
{
    if (const JsonValue* member = FindMember(object, key); member != nullptr)
    {
        if (!member->IsNumber())
        {
            throw std::runtime_error("Expected float member in glTF JSON.");
        }

        return static_cast<float>(member->AsNumber());
    }

    return defaultValue;
}

bool GetBool(const JsonValue::Object& object, const std::string_view key, const bool defaultValue)
{
    if (const JsonValue* member = FindMember(object, key); member != nullptr)
    {
        if (!member->IsBool())
        {
            throw std::runtime_error("Expected bool member in glTF JSON.");
        }

        return member->AsBool();
    }

    return defaultValue;
}

std::vector<std::uint32_t> ParseIndexArray(const JsonValue& value, const std::string_view context)
{
    const auto& array = ExpectArray(value, context);
    std::vector<std::uint32_t> indices;
    indices.reserve(array.size());

    for (const JsonValue& entry : array)
    {
        if (!entry.IsNumber())
        {
            throw std::runtime_error(std::string(context) + " must only contain numeric indices.");
        }

        const double numericValue = entry.AsNumber();
        if (numericValue < 0.0 || std::floor(numericValue) != numericValue)
        {
            throw std::runtime_error(std::string(context) + " contains non-integer index.");
        }

        indices.push_back(static_cast<std::uint32_t>(numericValue));
    }

    return indices;
}

template<std::size_t TValueCount>
std::array<float, TValueCount> ParseFloatArray(
    const JsonValue::Object& object,
    const std::string_view key,
    const std::array<float, TValueCount>& defaultValue)
{
    if (const JsonValue* member = FindMember(object, key); member != nullptr)
    {
        const auto& array = ExpectArray(*member, std::string(key));
        if (array.size() != TValueCount)
        {
            throw std::runtime_error("Unexpected float array size in glTF JSON.");
        }

        std::array<float, TValueCount> values = {};
        for (std::size_t valueIndex = 0; valueIndex < TValueCount; ++valueIndex)
        {
            if (!array[valueIndex].IsNumber())
            {
                throw std::runtime_error("Expected numeric value in glTF float array.");
            }

            values[valueIndex] = static_cast<float>(array[valueIndex].AsNumber());
        }
        return values;
    }

    return defaultValue;
}

std::vector<double> ParseDoubleArray(const JsonValue::Object& object, const std::string_view key)
{
    if (const JsonValue* member = FindMember(object, key); member != nullptr)
    {
        const auto& array = ExpectArray(*member, std::string(key));
        std::vector<double> values;
        values.reserve(array.size());
        for (const JsonValue& arrayValue : array)
        {
            if (!arrayValue.IsNumber())
            {
                throw std::runtime_error("Expected numeric value in glTF double array.");
            }

            values.push_back(arrayValue.AsNumber());
        }

        return values;
    }

    return {};
}

bool IsDataUri(const std::string_view uri)
{
    return uri.starts_with("data:");
}

std::filesystem::path ResolveUriPath(const std::filesystem::path& rootDirectory, const std::string_view uri)
{
    if (uri.empty() || IsDataUri(uri))
    {
        return {};
    }

    return (rootDirectory / std::filesystem::path(std::string(uri))).lexically_normal();
}

GltfTextureReference ParseTextureReference(const JsonValue::Object& object)
{
    GltfTextureReference textureReference = {};
    textureReference.texture = GetUInt32(object, "index", kInvalidGltfIndex);
    textureReference.texCoord = GetUInt32(object, "texCoord", 0);
    textureReference.scale = GetFloat(object, "scale", 1.0f);
    textureReference.strength = GetFloat(object, "strength", 1.0f);
    return textureReference;
}
} // namespace

GltfDocument GltfLoader::LoadFromFile(const std::filesystem::path& path)
{
    const std::filesystem::path normalizedPath = std::filesystem::absolute(path).lexically_normal();
    const JsonValue root = JsonParser::Parse(ReadTextFile(normalizedPath));
    const auto& rootObject = ExpectObject(root, "glTF root");

    GltfDocument document = {};
    document.sourcePath = normalizedPath;
    document.rootDirectory = normalizedPath.parent_path();
    document.defaultScene = GetUInt32(rootObject, "scene", kInvalidGltfIndex);

    if (const JsonValue* assetValue = FindMember(rootObject, "asset"); assetValue != nullptr)
    {
        const auto& assetObject = ExpectObject(*assetValue, "glTF asset");
        document.asset.version = GetString(assetObject, "version");
        document.asset.generator = GetString(assetObject, "generator");
    }

    if (const JsonValue* buffersValue = FindMember(rootObject, "buffers"); buffersValue != nullptr)
    {
        for (const JsonValue& bufferValue : ExpectArray(*buffersValue, "glTF buffers"))
        {
            const auto& bufferObject = ExpectObject(bufferValue, "glTF buffer");
            GltfBuffer buffer = {};
            buffer.uri = GetString(bufferObject, "uri");
            buffer.byteLength = GetUInt32(bufferObject, "byteLength", 0);
            buffer.isDataUri = IsDataUri(buffer.uri);
            buffer.resolvedPath = ResolveUriPath(document.rootDirectory, buffer.uri);
            document.buffers.push_back(std::move(buffer));
        }
    }

    if (const JsonValue* bufferViewsValue = FindMember(rootObject, "bufferViews"); bufferViewsValue != nullptr)
    {
        for (const JsonValue& bufferViewValue : ExpectArray(*bufferViewsValue, "glTF bufferViews"))
        {
            const auto& bufferViewObject = ExpectObject(bufferViewValue, "glTF bufferView");
            GltfBufferView bufferView = {};
            bufferView.buffer = GetUInt32(bufferViewObject, "buffer", kInvalidGltfIndex);
            bufferView.byteOffset = GetUInt32(bufferViewObject, "byteOffset", 0);
            bufferView.byteLength = GetUInt32(bufferViewObject, "byteLength", 0);
            bufferView.byteStride = GetUInt32(bufferViewObject, "byteStride", 0);
            bufferView.target = GetUInt32(bufferViewObject, "target", 0);
            document.bufferViews.push_back(bufferView);
        }
    }

    if (const JsonValue* accessorsValue = FindMember(rootObject, "accessors"); accessorsValue != nullptr)
    {
        for (const JsonValue& accessorValue : ExpectArray(*accessorsValue, "glTF accessors"))
        {
            const auto& accessorObject = ExpectObject(accessorValue, "glTF accessor");
            GltfAccessor accessor = {};
            accessor.bufferView = GetUInt32(accessorObject, "bufferView", kInvalidGltfIndex);
            accessor.byteOffset = GetUInt32(accessorObject, "byteOffset", 0);
            accessor.componentType = GetUInt32(accessorObject, "componentType", 0);
            accessor.count = GetUInt32(accessorObject, "count", 0);
            accessor.normalized = GetBool(accessorObject, "normalized", false);
            accessor.type = GetString(accessorObject, "type");
            accessor.minValues = ParseDoubleArray(accessorObject, "min");
            accessor.maxValues = ParseDoubleArray(accessorObject, "max");
            document.accessors.push_back(std::move(accessor));
        }
    }

    if (const JsonValue* imagesValue = FindMember(rootObject, "images"); imagesValue != nullptr)
    {
        for (const JsonValue& imageValue : ExpectArray(*imagesValue, "glTF images"))
        {
            const auto& imageObject = ExpectObject(imageValue, "glTF image");
            GltfImage image = {};
            image.name = GetString(imageObject, "name");
            image.uri = GetString(imageObject, "uri");
            image.mimeType = GetString(imageObject, "mimeType");
            image.bufferView = GetUInt32(imageObject, "bufferView", kInvalidGltfIndex);
            image.isDataUri = IsDataUri(image.uri);
            image.resolvedPath = ResolveUriPath(document.rootDirectory, image.uri);
            document.images.push_back(std::move(image));
        }
    }

    if (const JsonValue* samplersValue = FindMember(rootObject, "samplers"); samplersValue != nullptr)
    {
        for (const JsonValue& samplerValue : ExpectArray(*samplersValue, "glTF samplers"))
        {
            const auto& samplerObject = ExpectObject(samplerValue, "glTF sampler");
            GltfSampler sampler = {};
            sampler.magFilter = GetUInt32(samplerObject, "magFilter", 0);
            sampler.minFilter = GetUInt32(samplerObject, "minFilter", 0);
            sampler.wrapS = GetUInt32(samplerObject, "wrapS", 10497);
            sampler.wrapT = GetUInt32(samplerObject, "wrapT", 10497);
            document.samplers.push_back(sampler);
        }
    }

    if (const JsonValue* texturesValue = FindMember(rootObject, "textures"); texturesValue != nullptr)
    {
        for (const JsonValue& textureValue : ExpectArray(*texturesValue, "glTF textures"))
        {
            const auto& textureObject = ExpectObject(textureValue, "glTF texture");
            GltfTexture texture = {};
            texture.name = GetString(textureObject, "name");
            texture.sampler = GetUInt32(textureObject, "sampler", kInvalidGltfIndex);
            texture.source = GetUInt32(textureObject, "source", kInvalidGltfIndex);
            document.textures.push_back(std::move(texture));
        }
    }

    if (const JsonValue* materialsValue = FindMember(rootObject, "materials"); materialsValue != nullptr)
    {
        for (const JsonValue& materialValue : ExpectArray(*materialsValue, "glTF materials"))
        {
            const auto& materialObject = ExpectObject(materialValue, "glTF material");
            GltfMaterial material = {};
            material.name = GetString(materialObject, "name");
            material.emissiveFactor = ParseFloatArray<3>(materialObject, "emissiveFactor", material.emissiveFactor);
            material.alphaMode = GetString(materialObject, "alphaMode", "OPAQUE");
            material.alphaCutoff = GetFloat(materialObject, "alphaCutoff", 0.5f);
            material.doubleSided = GetBool(materialObject, "doubleSided", false);

            if (const JsonValue* pbrValue = FindMember(materialObject, "pbrMetallicRoughness"); pbrValue != nullptr)
            {
                const auto& pbrObject = ExpectObject(*pbrValue, "glTF pbrMetallicRoughness");
                material.baseColorFactor = ParseFloatArray<4>(pbrObject, "baseColorFactor", material.baseColorFactor);
                material.metallicFactor = GetFloat(pbrObject, "metallicFactor", 1.0f);
                material.roughnessFactor = GetFloat(pbrObject, "roughnessFactor", 1.0f);

                if (const JsonValue* baseColorTextureValue = FindMember(pbrObject, "baseColorTexture"); baseColorTextureValue != nullptr)
                {
                    material.baseColorTexture = ParseTextureReference(ExpectObject(*baseColorTextureValue, "glTF baseColorTexture"));
                }
                if (const JsonValue* metallicRoughnessTextureValue = FindMember(pbrObject, "metallicRoughnessTexture");
                    metallicRoughnessTextureValue != nullptr)
                {
                    material.metallicRoughnessTexture =
                        ParseTextureReference(ExpectObject(*metallicRoughnessTextureValue, "glTF metallicRoughnessTexture"));
                }
            }

            if (const JsonValue* normalTextureValue = FindMember(materialObject, "normalTexture"); normalTextureValue != nullptr)
            {
                material.normalTexture = ParseTextureReference(ExpectObject(*normalTextureValue, "glTF normalTexture"));
            }
            if (const JsonValue* occlusionTextureValue = FindMember(materialObject, "occlusionTexture"); occlusionTextureValue != nullptr)
            {
                material.occlusionTexture = ParseTextureReference(ExpectObject(*occlusionTextureValue, "glTF occlusionTexture"));
            }
            if (const JsonValue* emissiveTextureValue = FindMember(materialObject, "emissiveTexture"); emissiveTextureValue != nullptr)
            {
                material.emissiveTexture = ParseTextureReference(ExpectObject(*emissiveTextureValue, "glTF emissiveTexture"));
            }

            document.materials.push_back(std::move(material));
        }
    }

    if (const JsonValue* meshesValue = FindMember(rootObject, "meshes"); meshesValue != nullptr)
    {
        for (const JsonValue& meshValue : ExpectArray(*meshesValue, "glTF meshes"))
        {
            const auto& meshObject = ExpectObject(meshValue, "glTF mesh");
            GltfMesh mesh = {};
            mesh.name = GetString(meshObject, "name");

            if (const JsonValue* primitivesValue = FindMember(meshObject, "primitives"); primitivesValue != nullptr)
            {
                for (const JsonValue& primitiveValue : ExpectArray(*primitivesValue, "glTF mesh primitives"))
                {
                    const auto& primitiveObject = ExpectObject(primitiveValue, "glTF primitive");
                    GltfPrimitive primitive = {};
                    primitive.indices = GetUInt32(primitiveObject, "indices", kInvalidGltfIndex);
                    primitive.material = GetUInt32(primitiveObject, "material", kInvalidGltfIndex);
                    primitive.mode = GetUInt32(primitiveObject, "mode", 4);

                    if (const JsonValue* attributesValue = FindMember(primitiveObject, "attributes"); attributesValue != nullptr)
                    {
                        const auto& attributesObject = ExpectObject(*attributesValue, "glTF primitive attributes");
                        for (const auto& [attributeName, attributeValue] : attributesObject)
                        {
                            if (!attributeValue.IsNumber())
                            {
                                throw std::runtime_error("glTF primitive attribute index must be numeric.");
                            }

                            primitive.attributes.emplace(attributeName, static_cast<std::uint32_t>(attributeValue.AsNumber()));
                        }
                    }

                    mesh.primitives.push_back(std::move(primitive));
                }
            }

            document.meshes.push_back(std::move(mesh));
        }
    }

    if (const JsonValue* nodesValue = FindMember(rootObject, "nodes"); nodesValue != nullptr)
    {
        for (const JsonValue& nodeValue : ExpectArray(*nodesValue, "glTF nodes"))
        {
            const auto& nodeObject = ExpectObject(nodeValue, "glTF node");
            GltfNode node = {};
            node.name = GetString(nodeObject, "name");
            node.mesh = GetUInt32(nodeObject, "mesh", kInvalidGltfIndex);
            node.translation = ParseFloatArray<3>(nodeObject, "translation", node.translation);
            node.rotation = ParseFloatArray<4>(nodeObject, "rotation", node.rotation);
            node.scale = ParseFloatArray<3>(nodeObject, "scale", node.scale);

            if (const JsonValue* childrenValue = FindMember(nodeObject, "children"); childrenValue != nullptr)
            {
                node.children = ParseIndexArray(*childrenValue, "glTF node children");
            }
            if (const JsonValue* matrixValue = FindMember(nodeObject, "matrix"); matrixValue != nullptr)
            {
                node.matrix = ParseFloatArray<16>(nodeObject, "matrix", node.matrix);
                node.hasMatrix = true;
            }

            document.nodes.push_back(std::move(node));
        }
    }

    if (const JsonValue* scenesValue = FindMember(rootObject, "scenes"); scenesValue != nullptr)
    {
        for (const JsonValue& sceneValue : ExpectArray(*scenesValue, "glTF scenes"))
        {
            const auto& sceneObject = ExpectObject(sceneValue, "glTF scene");
            GltfScene scene = {};
            scene.name = GetString(sceneObject, "name");
            if (const JsonValue* sceneNodesValue = FindMember(sceneObject, "nodes"); sceneNodesValue != nullptr)
            {
                scene.nodes = ParseIndexArray(*sceneNodesValue, "glTF scene nodes");
            }
            document.scenes.push_back(std::move(scene));
        }
    }

    return document;
}
} // namespace ugc_renderer
