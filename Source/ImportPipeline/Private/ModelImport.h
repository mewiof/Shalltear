#pragma once

#include "Shalltear/ImportPipeline/ModelContributors.h"
#include "Shalltear/ImportPipeline/ModelParse.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class PrefabAsset;

	str SanitizeName(std::string_view name, std::string_view fallback, u32 index);

	str UniquifyName(str candidate, Vector<str>& taken);

	// kind/name, ordinal from 2 in file order
	SHALLTEAR_IMPORTPIPELINE_API str ProductKey(std::string_view kind, std::string_view name, Vector<str>& taken);

	inline constexpr std::string_view ProductKindMeshes = "Meshes";
	inline constexpr std::string_view ProductKindMaterials = "Materials";

	struct ModelRig {
		Vector<i32> boneNodes{GetMemoryResource(MemTag::Asset)};
		Vector<i32> nodeToBone{GetMemoryResource(MemTag::Asset)};
		Guid skeletonGuid;
		Vector<Guid> skinGuids{GetMemoryResource(MemTag::Asset)};

		bool IsEmpty() const { return boneNodes.empty(); }
		i32 BoneOfNode(i32 node) const {
			return (node >= 0 && static_cast<size_t>(node) < nodeToBone.size()) ? nodeToBone[static_cast<size_t>(node)] : -1;
		}
	};

	SHALLTEAR_IMPORTPIPELINE_API ModelRig BuildModelRig(const ModelParseResult& parsed);

	inline constexpr std::string_view ProductKindSkins = "Skins";

	u32 MaterialSlotFor(ModelMesh& mesh, i32 fileMaterial);

	void NormalizeInfluence(ModelSkinInfluence& influence);

	SHALLTEAR_IMPORTPIPELINE_API ModelParseResult ParseModel(const Path& sourcePhysicalPath, const ModelParseOptions& options = {});

	SHALLTEAR_IMPORTPIPELINE_API Vector<std::string_view> ModelExtensions();

	str GeneratedSiblingPath(const Path& modelVirtualPath, std::string_view kind,
							 size_t index, std::string_view name, std::string_view extension);

	SHALLTEAR_IMPORTPIPELINE_API PrefabAsset* BuildModelPrefab(const ModelParseResult& parsed, const Path& virtualPath,
															   std::span<const Guid> meshGuids, const ModelRig& rig,
															   std::span<const str> exposedBoneNames = {},
															   std::span<const StringHash> rootComponents = {});

	class ModelImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("PrefabAsset"); }
		u32 GetImporterVersion() const override;
		Asset* Import(const Path& sourcePath) override;
		Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings) override;
		Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings,
								  AssetImportContext& context) override;
	};

	class MeshDescriptorImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("MeshAsset"); }
		Asset* Import(const Path& sourcePath) override;
	};

} // namespace Shalltear
