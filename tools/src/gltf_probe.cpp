#include "ugc_renderer/asset/gltf_loader.h"
#include "ugc_renderer/asset/gltf_mesh_builder.h"
#include "ugc_renderer/asset/gltf_scene_builder.h"

#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
std::filesystem::path GetDefaultSamplePath()
{
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
    {
        throw std::runtime_error("GetModuleFileNameW failed for glTF probe.");
    }

    return std::filesystem::path(modulePath).parent_path() / L"assets" / L"gltf" / L"sample_scene" / L"sample_scene.gltf";
}

std::string ToConsoleSafeString(const std::filesystem::path& path)
{
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');

    for (const wchar_t character : path.wstring())
    {
        if (character >= 0x20 && character <= 0x7E)
        {
            stream << static_cast<char>(character);
        }
        else
        {
            stream << "\\u" << std::setw(4) << static_cast<std::uint32_t>(character);
        }
    }

    return stream.str();
}

void PrintPath(const std::string_view label, const std::filesystem::path& path)
{
    std::cout << label << ": " << ToConsoleSafeString(path) << "\n";
}

void PrintText(const std::string_view label, const std::string_view value)
{
    std::cout << label << ": " << value << "\n";
}

void PrintCount(const std::string_view label, const std::size_t value)
{
    std::cout << label << ": " << value << "\n";
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::filesystem::path sourcePath =
            argc > 1 ? std::filesystem::path(argv[1]) : GetDefaultSamplePath();

        const ugc_renderer::GltfDocument document = ugc_renderer::GltfLoader::LoadFromFile(sourcePath);

        PrintPath("Source", document.sourcePath);
        PrintText("glTF version", document.asset.version);
        PrintText("Generator", document.asset.generator);
        std::cout << "Default scene: ";
        if (document.defaultScene == ugc_renderer::kInvalidGltfIndex)
        {
            std::cout << "none\n";
        }
        else
        {
            std::cout << document.defaultScene << "\n";
        }

        PrintCount("Scenes", document.scenes.size());
        PrintCount("Nodes", document.nodes.size());
        PrintCount("Meshes", document.meshes.size());
        PrintCount("Materials", document.materials.size());
        PrintCount("Textures", document.textures.size());
        PrintCount("Images", document.images.size());
        PrintCount("Buffers", document.buffers.size());
        PrintCount("BufferViews", document.bufferViews.size());
        PrintCount("Accessors", document.accessors.size());

        for (std::size_t imageIndex = 0; imageIndex < document.images.size(); ++imageIndex)
        {
            const auto& image = document.images[imageIndex];
            std::cout << "Image[" << imageIndex << "] uri: " << image.uri << "\n";
            if (!image.resolvedPath.empty())
            {
                PrintPath("Resolved image path", image.resolvedPath);
            }
        }

        const auto meshInstances = ugc_renderer::GltfSceneBuilder::BuildMeshInstances(document);
        PrintCount("Scene mesh instances", meshInstances.size());

        std::size_t primitiveAssetCount = 0;
        std::size_t scenePrimitiveInstances = 0;
        std::size_t runtimeVertexCount = 0;
        std::size_t runtimeIndexCount = 0;

        for (std::uint32_t meshIndex = 0; meshIndex < document.meshes.size(); ++meshIndex)
        {
            const auto& mesh = document.meshes[meshIndex];
            primitiveAssetCount += mesh.primitives.size();

            for (std::uint32_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
            {
                const ugc_renderer::GltfRuntimeMesh runtimeMesh =
                    ugc_renderer::GltfMeshBuilder::BuildPrimitive(document, meshIndex, primitiveIndex);
                runtimeVertexCount += runtimeMesh.vertices.size();
                runtimeIndexCount += runtimeMesh.indices.size();
            }
        }

        for (const auto& instance : meshInstances)
        {
            scenePrimitiveInstances += document.meshes[instance.mesh].primitives.size();
        }

        PrintCount("Primitive assets", primitiveAssetCount);
        PrintCount("Scene primitive instances", scenePrimitiveInstances);
        PrintCount("Runtime vertex sum", runtimeVertexCount);
        PrintCount("Runtime index sum", runtimeIndexCount);

        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cout << "glTF probe failed: " << exception.what() << "\n";
        return EXIT_FAILURE;
    }
}
