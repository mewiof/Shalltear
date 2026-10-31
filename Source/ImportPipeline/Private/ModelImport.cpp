#include "ModelImport.h"

#include "FBXParser.h"
#include "glTFParser.h"
#include "ModelContributorsInternal.h"
#include "Scene/FragmentBuilder.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Graphics/SkinAsset.h"
#include "Shalltear/Core/Graphics/SkinnedMeshRenderer.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene/Prefab.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/StringUtil.h"

#include <algorithm>
#include <filesystem>
#include <format>

namespace Shalltear {

	namespace {

		struct ModelFormat {
			std::string_view extension;
			ModelParseResult (*parse)(const Path& sourcePhysicalPath, const ModelParseOptions& options);
		};

		const ModelFormat modelFormats[] = {
			{".gltf", &ParseGLTF},
			{".glb", &ParseGLTF},
			{".fbx", &ParseFBX},
		};

	} // anonymous namespace

	str SanitizeName(std::string_view name, std::string_view fallback, u32 index) {
		str result;
		for (char c : name) {
			bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
			result.push_back(ok ? c : '_');
		}
		if (result.empty()) {
			result = std::format("{}{}", fallback, index);
		}
		return result;
	}

	str UniquifyName(str candidate, Vector<str>& taken) {
		str result = candidate;
		u32 suffix = 1;
		while (std::find(taken.begin(), taken.end(), result) != taken.end()) {
			result = std::format("{}_{}", candidate, suffix++);
		}
		taken.push_back(result);
		return result;
	}

	str ProductKey(std::string_view kind, std::string_view name, Vector<str>& taken) {
		str candidate = std::format("{}/{}", kind, SanitizeName(name, "unnamed", static_cast<u32>(taken.size())));
		str result = candidate;
		u32 ordinal = 2;
		while (std::find(taken.begin(), taken.end(), result) != taken.end()) {
			result = std::format("{}#{}", candidate, ordinal++);
		}
		taken.push_back(result);
		return result;
	}

	str ModelRootName(const Path& virtualPath) {
		return std::filesystem::path(virtualPath.GetFilename()).stem().string();
	}

	u32 MaterialSlotFor(ModelMesh& mesh, i32 fileMaterial) {
		auto existing = std::find(mesh.materials.begin(), mesh.materials.end(), fileMaterial);
		if (existing == mesh.materials.end()) {
			mesh.materials.push_back(fileMaterial);
			existing = mesh.materials.end() - 1;
		}
		return static_cast<u32>(existing - mesh.materials.begin());
	}

	void NormalizeInfluence(ModelSkinInfluence& influence) {
		f32 total = 0.0f;
		for (int i = 0; i < 4; ++i) {
			if (!(influence.weights[i] > 0.0f) || !Math::IsFinite(influence.weights[i])) {
				influence.weights[i] = 0.0f;
				influence.joints[i] = 0;
				continue;
			}
			total += influence.weights[i];
		}
		if (total <= 0.0f) {
			return;
		}
		const f32 scale = 1.0f / total;
		for (int i = 0; i < 4; ++i) {
			influence.weights[i] *= scale;
		}
	}

	ModelParseResult ParseModel(const Path& sourcePhysicalPath, const ModelParseOptions& options) {
		str extension = StringUtil::ToLower(sourcePhysicalPath.GetExtension());
		for (const ModelFormat& format : modelFormats) {
			if (extension == format.extension) {
				return format.parse(sourcePhysicalPath, options);
			}
		}

		ModelParseResult result;
		result.error = std::format("'{}' isn't a model format this build can read", extension);
		return result;
	}

	Vector<std::string_view> ModelExtensions() {
		Vector<std::string_view> extensions{GetMemoryResource(MemTag::Asset)};
		extensions.reserve(std::size(modelFormats));
		for (const ModelFormat& format : modelFormats) {
			extensions.push_back(format.extension);
		}
		return extensions;
	}

	str GeneratedSiblingPath(const Path& modelVirtualPath, std::string_view kind,
							 size_t index, std::string_view name, std::string_view extension) {
		return std::format("{}/{}{}/{}_{}{}", modelVirtualPath.GetDirectory(), modelVirtualPath.GetFilename(),
						   kind, index, name, extension);
	}

	namespace {

		constexpr u64 SYNTHESIZED_ROOT_ROW_ID = 1;
		constexpr u64 ObjectRowId(size_t node) { return 2 * static_cast<u64>(node) + 2; }
		constexpr u64 ComponentRowId(size_t node) { return 2 * static_cast<u64>(node) + 3; }

		constexpr u64 SKELETON_COMPONENT_ROW_ID = 1'000'000'000ull;

		constexpr u64 CONTRIBUTED_COMPONENT_ROW_BASE = 2'000'000'000ull;
		constexpr u64 CONTRIBUTED_COMPONENT_ROW_SPAN = 1'000'000'000ull;
		constexpr u64 ContributedComponentRowId(StringHash type) {
			return CONTRIBUTED_COMPONENT_ROW_BASE + (type.GetValue() % CONTRIBUTED_COMPONENT_ROW_SPAN);
		}

		i32 SoleRootNode(const ModelParseResult& parsed) {
			i32 sole = -1;
			for (size_t i = 0; i < parsed.nodes.size(); ++i) {
				if (parsed.nodes[i].parent >= 0) {
					continue;
				}
				if (sole >= 0) {
					return -1;
				}
				sole = static_cast<i32>(i);
			}
			return sole;
		}

	} // anonymous namespace

	ModelRig BuildModelRig(const ModelParseResult& parsed) {
		ModelRig rig;
		const size_t nodeCount = parsed.nodes.size();
		rig.nodeToBone.assign(nodeCount, -1);
		if (parsed.skins.empty() || nodeCount == 0) {
			return rig;
		}

		Vector<u8> isBone{GetMemoryResource(MemTag::Asset)};
		isBone.assign(nodeCount, 0);
		for (const ModelSkin& skin : parsed.skins) {
			for (i32 joint : skin.joints) {
				i32 walk = joint;
				while (walk >= 0 && static_cast<size_t>(walk) < nodeCount && !isBone[static_cast<size_t>(walk)]) {
					isBone[static_cast<size_t>(walk)] = 1;
					walk = parsed.nodes[static_cast<size_t>(walk)].parent;
				}
			}
		}

		const i32 host = SoleRootNode(parsed);
		if (host >= 0 && static_cast<size_t>(host) < nodeCount) {
			isBone[static_cast<size_t>(host)] = 0;
		}

		for (size_t i = 0; i < nodeCount; ++i) {
			if (!isBone[i]) {
				continue;
			}
			rig.nodeToBone[i] = static_cast<i32>(rig.boneNodes.size());
			rig.boneNodes.push_back(static_cast<i32>(i));
		}
		return rig;
	}

	namespace {

		Mat4 NodeWorldMatrix(const ModelParseResult& parsed, i32 node) {
			Mat4 world = Mat4::Identity();
			i32 walk = node;
			for (size_t step = 0; step <= parsed.nodes.size(); ++step) {
				if (walk < 0 || static_cast<size_t>(walk) >= parsed.nodes.size()) {
					break;
				}
				const ModelNode& entry = parsed.nodes[static_cast<size_t>(walk)];
				world = entry.localMatrix * world;
				walk = entry.parent;
			}
			return world;
		}

	} // anonymous namespace

	ModelSkinFrame FindSkinFrame(const ModelParseResult& parsed, std::span<const i32> nodeToBone) {
		ModelSkinFrame found;
		auto boneOfNode = [&](i32 node) -> i32 {
			return node >= 0 && static_cast<size_t>(node) < nodeToBone.size() ? nodeToBone[static_cast<size_t>(node)] : -1;
		};

		size_t widest = 0;
		for (size_t index = 0; index < parsed.skins.size(); ++index) {
			size_t coverage = 0;
			for (i32 joint : parsed.skins[index].joints) {
				coverage += boneOfNode(joint) >= 0 ? 1u : 0u;
			}
			if (coverage > widest) {
				widest = coverage;
				found.skin = static_cast<i32>(index);
			}
		}
		if (found.skin < 0) {
			return found;
		}

		i32 hostNode = -1;
		for (size_t node = 0; node < parsed.nodes.size(); ++node) {
			if (parsed.nodes[node].skin == found.skin) {
				hostNode = static_cast<i32>(node);
				break;
			}
		}

		i32 frameNode = -1;
		for (size_t node = 0; node < parsed.nodes.size() && node < nodeToBone.size(); ++node) {
			if (nodeToBone[node] >= 0 && boneOfNode(parsed.nodes[node].parent) < 0) {
				frameNode = parsed.nodes[node].parent;
				break;
			}
		}

		Mat4 rigFromFile = Mat4::Identity();
		if (frameNode >= 0 && !NodeWorldMatrix(parsed, frameNode).TryInvert(rigFromFile)) {
			rigFromFile = Mat4::Identity();
			SLT_LOG_WARN("Graphics", "FindSkinFrame: the rig hangs off a node with no invertible transform (bind pose read in the file's frame)");
		}
		found.meshToRig = rigFromFile * NodeWorldMatrix(parsed, hostNode);
		return found;
	}

	namespace {

		SkeletonAsset* BuildSkeletonAsset(const ModelParseResult& parsed, const ModelRig& rig) {
			SkeletonAsset* skeleton = New<SkeletonAsset>(MemTag::Asset);
			const size_t boneCount = rig.boneNodes.size();
			skeleton->boneNames.reserve(boneCount);
			skeleton->parents.reserve(boneCount);
			skeleton->restPose.reserve(boneCount);
			for (i32 nodeIndex : rig.boneNodes) {
				const ModelNode& node = parsed.nodes[static_cast<size_t>(nodeIndex)];
				skeleton->boneNames.push_back(node.name);
				skeleton->parents.push_back(static_cast<i16>(rig.BoneOfNode(node.parent)));

				BoneTransform rest;
				rest.position = node.position;
				rest.rotation = node.rotation;
				rest.scale = node.scale;
				skeleton->restPose.push_back(rest);
			}
			return skeleton;
		}

	} // anonymous namespace

	SkeletonAsset* BuildNodeSkeleton(const ModelParseResult& parsed) {
		const size_t nodeCount = parsed.nodes.size();
		if (nodeCount == 0) {
			return nullptr;
		}

		ModelRig rig;
		rig.nodeToBone.reserve(nodeCount);
		rig.boneNodes.reserve(nodeCount);
		for (size_t i = 0; i < nodeCount; ++i) {
			rig.nodeToBone.push_back(static_cast<i32>(i));
			rig.boneNodes.push_back(static_cast<i32>(i));
		}
		return BuildSkeletonAsset(parsed, rig);
	}

	Vector<str> ReadExposedBoneNames(const JSONValue& settings) {
		Vector<str> names{GetMemoryResource(MemTag::Asset)};
		if (!settings.IsObject()) {
			return names;
		}
		const JSONValue* rig = settings.Find("rig");
		if (!rig || !rig->IsObject()) {
			return names;
		}
		const JSONValue* exposed = rig->Find("exposedBones");
		if (!exposed || !exposed->IsArray()) {
			return names;
		}
		names.reserve(exposed->Size());
		for (size_t i = 0; i < exposed->Size(); ++i) {
			std::string_view name = (*exposed)[i].AsString();
			if (!name.empty()) {
				names.push_back(str(name));
			}
		}
		return names;
	}

	PrefabAsset* BuildModelPrefab(const ModelParseResult& parsed, const Path& virtualPath,
								  std::span<const Guid> meshGuids, const ModelRig& rig,
								  std::span<const str> exposedBoneNames,
								  std::span<const StringHash> rootComponents) {
		Vector<MeshAsset*> meshAssets{GetMemoryResource(MemTag::Asset)};
		meshAssets.reserve(meshGuids.size());
		for (const Guid& meshGuid : meshGuids) {
			Asset* resolved = meshGuid.IsValid() ? AssetDatabase::LoadAssetByGuid(meshGuid) : nullptr;
			meshAssets.push_back(static_cast<MeshAsset*>(resolved));
		}

		Asset* skeletonAsset = rig.skeletonGuid.IsValid() ? AssetDatabase::LoadAssetByGuid(rig.skeletonGuid) : nullptr;
		Vector<Asset*> skinAssets{GetMemoryResource(MemTag::Asset)};
		skinAssets.reserve(rig.skinGuids.size());
		for (const Guid& skinGuid : rig.skinGuids) {
			skinAssets.push_back(skinGuid.IsValid() ? AssetDatabase::LoadAssetByGuid(skinGuid) : nullptr);
		}

		str rootName = ModelRootName(virtualPath);
		SceneInternal::FragmentBuilder fragment(rootName, PrefabKind::Model);

		i32 soleRoot = SoleRootNode(parsed);

		Vector<u64> componentRows{GetMemoryResource(MemTag::Asset)};

		auto attachSkeleton = [&]() {
			if (rig.IsEmpty() || !skeletonAsset) {
				return;
			}
			auto* component = static_cast<Skeleton*>(fragment.AddComponent(Skeleton::GetReflectType()));
			if (!component) {
				return;
			}
			component->SetSkeleton(Handle<SkeletonAsset>(static_cast<SkeletonAsset*>(skeletonAsset)));
			const size_t boneCount = rig.boneNodes.size();
			for (size_t b = 0; b < boneCount; ++b) {
				if (!exposedBoneNames.empty()) {
					const i32 node = rig.boneNodes[b];
					const std::string_view boneName =
						(node >= 0 && static_cast<size_t>(node) < parsed.nodes.size()) ? std::string_view(parsed.nodes[static_cast<size_t>(node)].name) : std::string_view{};
					if (std::find(exposedBoneNames.begin(), exposedBoneNames.end(), boneName) == exposedBoneNames.end()) {
						continue;
					}
				}
				Handle<SceneObject> boneObject;
				fragment.SetHandleToRow(boneObject, ObjectRowId(static_cast<size_t>(rig.boneNodes[b])));
				component->AddExposedBone(static_cast<i16>(b), boneObject);
			}
			componentRows.push_back(SKELETON_COMPONENT_ROW_ID);
		};

		Vector<u64> contributedRows{GetMemoryResource(MemTag::Asset)};
		auto attachContributed = [&]() {
			for (const StringHash& typeName : rootComponents) {
				const TypeInfo* type = TypeRegistry::FindType(typeName);
				if (!type) {
					SLT_LOG_WARN("Graphics", "BuildModelPrefab: '{}' asked for a root component this build has no reflected type for. Skipped", virtualPath.ToString());
					continue;
				}
				const u64 rowId = ContributedComponentRowId(typeName);
				if (std::find(contributedRows.begin(), contributedRows.end(), rowId) != contributedRows.end()) {
					SLT_LOG_ERROR("Graphics", "BuildModelPrefab: '{}': '{}' maps to a row id another contributed component already uses. Skipped", virtualPath.ToString(), type->displayName ? type->displayName : "<unnamed>");
					continue;
				}
				if (!fragment.AddComponent(type)) {
					continue;
				}
				contributedRows.push_back(rowId);
				componentRows.push_back(rowId);
			}
		};

		if (soleRoot < 0) {
			SceneObject& root = fragment.BeginObject();
			root.name = rootName;
			componentRows.clear();
			attachSkeleton();
			attachContributed();
			fragment.EndObject(SYNTHESIZED_ROOT_ROW_ID, 0, {componentRows.data(), componentRows.size()});
		}

		Vector<Asset*> boundMaterials{GetMemoryResource(MemTag::Asset)};
		for (size_t i = 0; i < parsed.nodes.size(); ++i) {
			const ModelNode& node = parsed.nodes[i];

			SceneObject& object = fragment.BeginObject();
			componentRows.clear();
			object.name = (static_cast<i32>(i) == soleRoot) ? rootName : node.name;
			object.SetLocalPosition(node.position);
			object.SetLocalRotation(node.rotation);
			object.SetLocalScale(node.scale);
			if (node.shearedMatrix) {
				SLT_LOG_WARN("Graphics", "BuildModelPrefab: node '{}' of '{}' has shear (importing its closest TRS)", node.name, virtualPath.ToString());
			}

			if (node.mesh >= 0 && static_cast<size_t>(node.mesh) < meshAssets.size() && meshAssets[static_cast<size_t>(node.mesh)]) {
				MeshAsset* mesh = meshAssets[static_cast<size_t>(node.mesh)];

				Asset* skinAsset = (node.skin >= 0 && static_cast<size_t>(node.skin) < skinAssets.size())
									   ? skinAssets[static_cast<size_t>(node.skin)]
									   : nullptr;
				MeshRenderer* plainRenderer = nullptr;
				SkinnedMeshRenderer* skinnedRenderer = nullptr;

				if (skinAsset || !mesh->blendShapes.empty()) {
					auto* renderer = static_cast<SkinnedMeshRenderer*>(fragment.AddComponent(SkinnedMeshRenderer::GetReflectType()));
					if (renderer) {
						renderer->SetMesh(Handle<MeshAsset>(mesh));
						if (skinAsset) {
							renderer->SetSkin(Handle<SkinAsset>(static_cast<SkinAsset*>(skinAsset)));
							Handle<Skeleton> skeletonRow;
							fragment.SetHandleToRow(skeletonRow, SKELETON_COMPONENT_ROW_ID);
							renderer->SetSkeleton(skeletonRow);
						}
						renderer->SetBlendShapeWeightCount(static_cast<u32>(mesh->blendShapes.size()));
						skinnedRenderer = renderer;
					}
				} else {
					auto* renderer = static_cast<MeshRenderer*>(fragment.AddComponent(MeshRenderer::GetReflectType()));
					if (renderer) {
						renderer->SetMesh(Handle<MeshAsset>(mesh));
						plainRenderer = renderer;
					}
				}

				if (plainRenderer || skinnedRenderer) {
					componentRows.push_back(ComponentRowId(i));
					u32 slot = 0;
					for (const Guid& materialGuid : mesh->defaultMaterials) {
						Asset* material = materialGuid.IsValid() ? AssetDatabase::LoadAssetByGuid(materialGuid) : nullptr;
						if (material) {
							boundMaterials.push_back(material);
						}
						Handle<MaterialAsset> bound(static_cast<MaterialAsset*>(material));
						if (plainRenderer) {
							plainRenderer->SetMaterial(slot, bound);
						} else {
							skinnedRenderer->SetMaterial(slot, bound);
						}
						++slot;
					}
				}
			}

			if (static_cast<i32>(i) == soleRoot) {
				attachSkeleton();
				attachContributed();
			}

			u64 parentRowId = SYNTHESIZED_ROOT_ROW_ID;
			if (node.parent >= 0) {
				parentRowId = ObjectRowId(static_cast<size_t>(node.parent));
			} else if (static_cast<i32>(i) == soleRoot) {
				parentRowId = 0;
			}
			fragment.EndObject(ObjectRowId(i), parentRowId, {componentRows.data(), componentRows.size()});
		}

		PrefabAsset* prefab = fragment.Release();

		for (Asset* material : boundMaterials) {
			AssetDatabase::UnloadAsset(material);
		}
		for (MeshAsset* mesh : meshAssets) {
			if (mesh) {
				AssetDatabase::UnloadAsset(mesh);
			}
		}
		for (Asset* skin : skinAssets) {
			if (skin) {
				AssetDatabase::UnloadAsset(skin);
			}
		}
		if (skeletonAsset) {
			AssetDatabase::UnloadAsset(skeletonAsset);
		}
		return prefab;
	}

	// bump when this importer's output changes
	constexpr u32 MODEL_IMPORTER_BASE_VERSION = 4;

	u32 ModelImporter::GetImporterVersion() const {
		const u64 fingerprint = ModelContributors::Fingerprint();
		return MODEL_IMPORTER_BASE_VERSION ^ static_cast<u32>(fingerprint ^ (fingerprint >> 32));
	}

	Asset* ModelImporter::Import(const Path& sourcePath) {
		SLT_LOG_ERROR("Graphics", "ModelImporter: '{}' has no import context (models import through AssetDatabase)", sourcePath.ToString());
		return nullptr;
	}

	Asset* ModelImporter::ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings) {
		(void)virtualPath;
		(void)settings;
		return Import(sourcePath);
	}

	namespace {

		void AdoptGeneratedSibling(AssetImportContext& context, const Path& virtualPath, std::string_view kind,
								   size_t index, std::string_view name, std::string_view extension, std::string_view key) {
			str legacyVirtual = GeneratedSiblingPath(virtualPath, kind, index, name, extension);
			if (!FileSystem::Exists(FileSystem::Resolve(Path(legacyVirtual)))) {
				return;
			}
			Guid existing = AssetDatabase::EnsureAssetIdentity(Path(legacyVirtual));
			if (!existing.IsValid()) {
				SLT_LOG_WARN("Graphics", "ModelImporter: '{}' has no identity to adopt for '{}'", legacyVirtual, key);
				return;
			}
			if (context.AdoptProduct(key, existing) == existing) {
				SLT_LOG_INFO("Graphics", "ModelImporter: adopted '{}' as the extracted product '{}' of '{}'", legacyVirtual, key, virtualPath.ToString());
			}
		}

		void MigrateMeshDescriptor(AssetImportContext& context, const Path& virtualPath, size_t index,
								   std::string_view name, std::string_view key) {
			str legacyVirtual = GeneratedSiblingPath(virtualPath, ".meshes", index, name, ".slmesh");
			Path legacyPhysical = FileSystem::Resolve(Path(legacyVirtual));
			if (!FileSystem::Exists(legacyPhysical)) {
				return;
			}
			Guid existing = AssetDatabase::EnsureAssetIdentity(Path(legacyVirtual));
			if (!existing.IsValid() || context.AdoptProduct(key, existing) != existing) {
				SLT_LOG_WARN("Graphics", "ModelImporter: '{}' couldn't be adopted as '{}' (left on disk)", legacyVirtual, key);
				return;
			}

			namespace fs = std::filesystem;
			std::error_code ec;
			fs::remove(fs::path(legacyPhysical.ToString()), ec);
			std::error_code metaEc;
			fs::remove(fs::path(FileSystem::Resolve(AssetDatabase::GetMetaPath(Path(legacyVirtual))).ToString()), metaEc);
			if (ec || metaEc) {
				SLT_LOG_WARN("Graphics", "ModelImporter: adopted '{}' but couldn't delete it ({})", legacyVirtual, ec ? ec.message() : metaEc.message());
				return;
			}
			SLT_LOG_INFO("Graphics", "ModelImporter: adopted '{}' as the product '{}' of '{}' and retired the descriptor", legacyVirtual, key, virtualPath.ToString());
		}

		void RemoveEmptySiblingDirectory(const Path& virtualPath, std::string_view kind) {
			namespace fs = std::filesystem;
			str directory = std::format("{}/{}{}", virtualPath.GetDirectory(), virtualPath.GetFilename(), kind);
			std::error_code ec;
			fs::path physical(FileSystem::Resolve(Path(directory)).ToString());
			if (fs::is_directory(physical, ec) && !ec && fs::is_empty(physical, ec) && !ec) {
				fs::remove(physical, ec);
			}
		}

		enum ImageRole : u8 {
			IMAGE_COLOR = 1 << 0, // base color + emissive, sRGB
			IMAGE_LINEAR = 1 << 1,
			IMAGE_NORMAL = 1 << 2
		};

		void SeedImageRole(const Path& image, u8 roles, bool minted) {
			const bool color = (roles & IMAGE_COLOR) != 0;
			const bool data = (roles & (IMAGE_LINEAR | IMAGE_NORMAL)) != 0;
			if (!data) {
				return;
			}
			if (color) {
				SLT_LOG_WARN("Graphics", "ModelImporter: '{}' is bound both as a color and as a data map (import settings left as they are)",
							 image.ToString());
				return;
			}
			const Path metaPath = FileSystem::Resolve(AssetDatabase::GetMetaPath(image));
			const str text = FileSystem::ReadTextFile(metaPath, MemTag::Asset);
			LinearAllocator arena(text.size() + 64, MemTag::Asset);
			const JSONValue root = JSONParse(text, arena);
			const JSONValue& existing = root["importer"]["settings"];
			if (!minted && existing.IsObject() && existing.HasKey("textureType")) {
				return;
			}
			const bool normalMap = (roles & IMAGE_NORMAL) != 0 && (roles & IMAGE_LINEAR) == 0;
			JSONValue settings = JSONValue::MakeObject();
			for (size_t member = 0; existing.IsObject() && member < existing.MemberCount(); ++member) {
				const std::string_view key = existing.KeyAt(member);
				if (key != "textureType" && key != "sRGB") {
					settings.AddMember(key, existing.ChildAt(member));
				}
			}
			settings.AddMember("textureType", JSONValue::MakeString(normalMap ? "normalMap" : "default"));
			settings.AddMember("sRGB", JSONValue::MakeBool(false));
			AssetDatabase::WriteImporterSettings(image, settings);
		}

	} // anonymous namespace

	Asset* ModelImporter::ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings,
											 AssetImportContext& context) {
		ModelParseOptions options;
		options.tracks = ModelContributors::AnyWantsTracks();

		ModelParseResult parsed = ParseModel(sourcePath, options);
		if (!parsed.success) {
			SLT_LOG_ERROR("Graphics", "ModelImporter: '{}' failed to parse: {}", sourcePath.ToString(), parsed.error);
			return nullptr;
		}

		namespace fs = std::filesystem;
		std::error_code ec;
		str virtualDir = virtualPath.GetDirectory();

		Vector<str> takenKeys{GetMemoryResource(MemTag::Asset)};
		takenKeys.reserve(parsed.materials.size() + parsed.meshes.size());

		Guid standardShaderGuid = AssetDatabase::EnsureAssetIdentity(Path("engine://Shaders/Lit.hlsl"));
		if (!standardShaderGuid.IsValid()) {
			SLT_LOG_WARN("Graphics", "ModelImporter: engine://Shaders/Lit.hlsl unavailable (generated materials get a null shader)");
		}

		Vector<u8> imageRoles{GetMemoryResource(MemTag::Asset)};
		imageRoles.assign(parsed.images.size(), 0);
		auto bindsAs = [&](i32 image, u8 role) {
			if (image >= 0 && static_cast<size_t>(image) < imageRoles.size()) {
				imageRoles[static_cast<size_t>(image)] |= role;
			}
		};
		for (const ModelMaterial& material : parsed.materials) {
			bindsAs(material.baseColorImage, IMAGE_COLOR);
			bindsAs(material.emissiveImage, IMAGE_COLOR);
			bindsAs(material.metallicRoughnessImage, IMAGE_LINEAR);
			bindsAs(material.occlusionImage, IMAGE_LINEAR);
			bindsAs(material.normalImage, IMAGE_NORMAL);
		}

		Vector<Guid> imageGuids{GetMemoryResource(MemTag::Asset)};
		for (size_t i = 0; i < parsed.images.size(); ++i) {
			ModelImage& image = parsed.images[i];
			str imageVirtual;
			if (image.IsExternal()) {
				str relativeName = fs::path(image.externalPath).filename().string();
				imageVirtual = virtualDir + "/" + relativeName;
			} else {
				imageVirtual = GeneratedSiblingPath(virtualPath, ".images", i, image.name, image.extension);
				Path extractedPhysical = FileSystem::Resolve(Path(imageVirtual));
				if (!FileSystem::Exists(extractedPhysical)) {
					fs::create_directories(fs::path(extractedPhysical.ToString()).parent_path(), ec);
					Buffer payload(image.embeddedBytes.size(), MemTag::Asset);
					std::memcpy(payload.data, image.embeddedBytes.data(), image.embeddedBytes.size());
					if (!FileSystem::WriteFile(extractedPhysical, payload)) {
						SLT_LOG_WARN("Graphics", "ModelImporter: failed to extract image '{}'", extractedPhysical.ToString());
					}
				}
			}

			const Path imagePath(imageVirtual);
			const bool minted = !FileSystem::Exists(FileSystem::Resolve(AssetDatabase::GetMetaPath(imagePath)));
			Guid guid = AssetDatabase::EnsureAssetIdentity(imagePath);
			if (!guid.IsValid()) {
				SLT_LOG_WARN("Graphics", "ModelImporter: image '{}' has no identity", imageVirtual);
			} else {
				SeedImageRole(imagePath, imageRoles[i], minted);
			}
			imageGuids.push_back(guid);
		}

		Vector<ModelProduct> minted{GetMemoryResource(MemTag::Asset)};
		auto mint = [&](str key, Asset* product) -> Guid {
			const StringHash type = product != nullptr ? product->GetAssetTypeId() : StringHash{};
			const Guid guid = context.AddProduct(key, product);
			if (!guid.IsValid()) {
				SLT_LOG_WARN("Graphics", "ModelImporter: couldn't add '{}' of '{}' to the import", key, virtualPath.ToString());
				return guid;
			}
			minted.push_back(ModelProduct{std::move(key), guid, type});
			return guid;
		};

		Vector<Guid> materialGuids{GetMemoryResource(MemTag::Asset)};
		materialGuids.reserve(parsed.materials.size());
		for (size_t i = 0; i < parsed.materials.size(); ++i) {
			const ModelMaterial& material = parsed.materials[i];
			str key = ProductKey(ProductKindMaterials, material.name, takenKeys);
			AdoptGeneratedSibling(context, virtualPath, ".materials", i, material.name, ".mat", key);

			MaterialAsset* draft = New<MaterialAsset>(MemTag::Asset);
			draft->shaderGuid = standardShaderGuid;
			draft->SetColor("baseColor", material.baseColorFactor);
			draft->SetFloat("metallic", material.metallicFactor);
			draft->SetFloat("roughness", material.roughnessFactor);
			draft->SetVec3("emissive", material.emissiveFactor);
			draft->SetFloat("normalScale", material.normalScale);
			draft->SetFloat("occlusionStrength", material.occlusionStrength);

			auto bindMap = [&](const char* slot, i32 image) {
				if (image >= 0 && static_cast<size_t>(image) < imageGuids.size()) {
					draft->SetTexture(slot, imageGuids[static_cast<size_t>(image)]);
				}
			};
			bindMap("baseColorTexture", material.baseColorImage);
			bindMap("metallicRoughnessTexture", material.metallicRoughnessImage);
			bindMap("normalTexture", material.normalImage);
			bindMap("occlusionTexture", material.occlusionImage);
			bindMap("emissiveTexture", material.emissiveImage);

			materialGuids.push_back(mint(std::move(key), draft));
		}

		Vector<Guid> meshGuids{GetMemoryResource(MemTag::Asset)};
		meshGuids.reserve(parsed.meshes.size());
		for (size_t i = 0; i < parsed.meshes.size(); ++i) {
			ModelMesh& mesh = parsed.meshes[i];
			str key = ProductKey(ProductKindMeshes, mesh.name, takenKeys);
			MigrateMeshDescriptor(context, virtualPath, i, mesh.name, key);

			MeshAsset* asset = New<MeshAsset>(MemTag::Asset);
			asset->vertices = std::move(mesh.vertices);
			asset->indices = std::move(mesh.indices);
			asset->submeshes = std::move(mesh.submeshes);
			asset->skinInfluences = std::move(mesh.influences);
			asset->bounds = mesh.bounds;

			asset->blendShapes.reserve(mesh.blendChannels.size());
			for (ModelBlendChannel& channel : mesh.blendChannels) {
				MeshBlendShape shape;
				shape.name = channel.name;
				shape.frames.reserve(channel.shapes.size());
				for (ModelBlendShape& target : channel.shapes) {
					MeshBlendShapeFrame frame;
					frame.weight = target.targetWeight;
					frame.vertices = std::move(target.offsetVertices);
					frame.positionDeltas = std::move(target.positionOffsets);
					frame.normalDeltas = std::move(target.normalOffsets);
					shape.frames.push_back(std::move(frame));
				}
				asset->blendShapes.push_back(std::move(shape));
			}

			for (i32 fileMaterial : mesh.materials) {
				Guid guid{};
				if (fileMaterial >= 0 && static_cast<size_t>(fileMaterial) < materialGuids.size()) {
					guid = materialGuids[static_cast<size_t>(fileMaterial)];
				}
				asset->defaultMaterials.push_back(guid);
			}

			meshGuids.push_back(mint(std::move(key), asset));
		}

		ModelRig rig = BuildModelRig(parsed);
		if (!rig.IsEmpty()) {
			str skeletonKey = ProductKey(ProductKindSkeletons, ModelRootName(virtualPath), takenKeys);
			rig.skeletonGuid = mint(std::move(skeletonKey), BuildSkeletonAsset(parsed, rig));

			rig.skinGuids.reserve(parsed.skins.size());
			for (size_t i = 0; i < parsed.skins.size(); ++i) {
				const ModelSkin& parsedSkin = parsed.skins[i];
				SkinAsset* skin = New<SkinAsset>(MemTag::Asset);
				skin->skeletonGuid = rig.skeletonGuid;
				skin->inverseBind = parsedSkin.inverseBind;
				skin->jointToBone.reserve(parsedSkin.joints.size());
				for (i32 jointNode : parsedSkin.joints) {
					skin->jointToBone.push_back(static_cast<i16>(rig.BoneOfNode(jointNode)));
				}

				str skinKey = ProductKey(ProductKindSkins, std::format("Skin{}", i), takenKeys);
				rig.skinGuids.push_back(mint(std::move(skinKey), skin));
			}
		}

		RemoveEmptySiblingDirectory(virtualPath, ".meshes");

		Vector<StringHash> rootComponents{GetMemoryResource(MemTag::Asset)};
		const ModelContribution contribution{parsed, virtualPath,
											 std::span<const ModelProduct>(minted.data(), minted.size()),
											 std::span<const i32>(rig.nodeToBone.data(), rig.nodeToBone.size()),
											 settings, context, nullptr, &takenKeys, &rootComponents};
		ModelContributors::Invoke(contribution);

		Vector<str> exposed = ReadExposedBoneNames(settings);
		return BuildModelPrefab(parsed, virtualPath, std::span<const Guid>(meshGuids.data(), meshGuids.size()), rig,
								std::span<const str>(exposed.data(), exposed.size()),
								std::span<const StringHash>(rootComponents.data(), rootComponents.size()));
	}

	Asset* MeshDescriptorImporter::Import(const Path& sourcePath) {
		str text = FileSystem::ReadTextFile(sourcePath, MemTag::Asset);
		if (text.empty()) {
			SLT_LOG_ERROR("Graphics", "MeshDescriptorImporter: '{}' is empty or unreadable", sourcePath.ToString());
			return nullptr;
		}

		LinearAllocator arena(text.size() + 64, MemTag::Asset);
		JSONValue root = JSONParse(text, arena);

		Guid modelGuid = Guid::FromString(root["model"].AsString());
		if (!modelGuid.IsValid()) {
			SLT_LOG_ERROR("Graphics", "MeshDescriptorImporter: '{}' has no model", sourcePath.ToString());
			return nullptr;
		}

		// parse by path, never load (import graph stays acyclic)
		Path modelVirtualPath = AssetDatabase::LookupPath(modelGuid);
		if (modelVirtualPath.ToString().empty()) {
			SLT_LOG_ERROR("Graphics", "MeshDescriptorImporter: '{}' uses model {} (unknown to the database)", sourcePath.ToString(), modelGuid.ToString());
			return nullptr;
		}

		ModelParseResult parsed = ParseModel(FileSystem::Resolve(modelVirtualPath));
		if (!parsed.success) {
			SLT_LOG_ERROR("Graphics", "MeshDescriptorImporter: model '{}' failed to parse: {}", modelVirtualPath.ToString(), parsed.error);
			return nullptr;
		}

		i64 meshIndex = root["mesh"].AsInt(-1);
		if (meshIndex < 0 || static_cast<size_t>(meshIndex) >= parsed.meshes.size()) {
			SLT_LOG_ERROR("Graphics", "MeshDescriptorImporter: '{}' asks for mesh {} of '{}' (it has {})", sourcePath.ToString(), meshIndex, modelVirtualPath.ToString(), parsed.meshes.size());
			return nullptr;
		}
		ModelMesh& mesh = parsed.meshes[static_cast<size_t>(meshIndex)];

		std::string_view recordedName = root["name"].AsString();
		if (!recordedName.empty() && recordedName != mesh.name) {
			SLT_LOG_WARN("Graphics", "MeshDescriptorImporter: '{}' recorded mesh {} as '{}' but '{}' now has '{}' there (the model was restructured)", sourcePath.ToString(), meshIndex, recordedName, modelVirtualPath.ToString(), mesh.name);
		}

		MeshAsset* asset = New<MeshAsset>(MemTag::Asset);
		asset->vertices = std::move(mesh.vertices);
		asset->indices = std::move(mesh.indices);
		asset->submeshes = std::move(mesh.submeshes);
		asset->bounds = mesh.bounds;

		for (i32 fileMaterial : mesh.materials) {
			Guid guid{};
			if (fileMaterial >= 0 && static_cast<size_t>(fileMaterial) < parsed.materials.size()) {
				str materialPath = GeneratedSiblingPath(modelVirtualPath, ".materials", static_cast<size_t>(fileMaterial),
														parsed.materials[static_cast<size_t>(fileMaterial)].name, ".mat");
				Asset* material = AssetDatabase::LoadAsset(Path(materialPath));
				if (material) {
					guid = material->assetId;
					AssetDatabase::UnloadAsset(material);
				} else {
					SLT_LOG_WARN("Graphics", "MeshDescriptorImporter: '{}' expected generated material '{}'", sourcePath.ToString(), materialPath);
				}
			}
			asset->defaultMaterials.push_back(guid);
		}
		return asset;
	}

} // namespace Shalltear
