#include "PrimitiveMeshImporter.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

namespace Shalltear {

	namespace {

		constexpr const char* DEFAULT_MATERIAL_PATH = "engine://Materials/Default.mat";

	} // anonymous namespace

	Asset* PrimitiveMeshImporter::Import(const Path& sourcePath) {
		str text = FileSystem::ReadTextFile(sourcePath, MemTag::Asset);
		if (text.empty()) {
			SLT_LOG_ERROR("Graphics", "PrimitiveMeshImporter: '{}' is empty or unreadable", sourcePath.ToString());
			return nullptr;
		}

		LinearAllocator arena(text.size() + 64, MemTag::Asset);
		JSONValue root = JSONParse(text, arena);
		std::string_view shapeName = root["shape"].AsString();

		PrimitiveShape shape{};
		if (!ParsePrimitiveShape(shapeName, shape)) {
			SLT_LOG_ERROR("Graphics", "PrimitiveMeshImporter: '{}' has unknown shape '{}'", sourcePath.ToString(), shapeName);
			return nullptr;
		}

		MeshAsset* asset = New<MeshAsset>(MemTag::Asset);
		if (!BuildPrimitiveMesh(shape, *asset)) {
			Delete(asset);
			return nullptr;
		}

		Asset* defaultMaterial = AssetDatabase::LoadAsset(Path(DEFAULT_MATERIAL_PATH));
		if (defaultMaterial) {
			asset->defaultMaterials.push_back(defaultMaterial->assetId);
			AssetDatabase::UnloadAsset(defaultMaterial);
		}
		return asset;
	}

} // namespace Shalltear
