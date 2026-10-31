#pragma once

#include "Shalltear/ImportPipeline/ImportPipelineAPI.h"
#include "Shalltear/ImportPipeline/ModelParse.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class SHALLTEAR_CORE_API JSONValue;

	class SHALLTEAR_CORE_API SkeletonAsset;

	struct ModelProduct {
		str key;
		Guid guid;
		StringHash type;
	};

	struct ModelSkinFrame {
		i32 skin = -1;

		Mat4 meshToRig = Mat4::Identity();
	};

	SHALLTEAR_IMPORTPIPELINE_API ModelSkinFrame FindSkinFrame(const ModelParseResult& parsed,
															  std::span<const i32> nodeToBone);

	// caller owns the result
	SHALLTEAR_IMPORTPIPELINE_API SkeletonAsset* BuildNodeSkeleton(const ModelParseResult& parsed);

	// both skeleton producers must use this key
	inline constexpr std::string_view ProductKindSkeletons = "Skeletons";

	SHALLTEAR_IMPORTPIPELINE_API str ModelRootName(const Path& virtualPath);

	SHALLTEAR_IMPORTPIPELINE_API ModelParseResult ParseReferencedModel(const Guid& modelGuid);

	struct ModelContribution {
		// mesh payload arrays were moved out and read empty
		const ModelParseResult& parsed;

		const Path& modelVirtualPath;

		std::span<const ModelProduct> products;

		std::span<const i32> nodeToBone;

		// read only your namespaced member
		const JSONValue& settings;

		AssetImportContext& context;

		void* user = nullptr;

		// only through MakeProductKey
		Vector<str>* keyLedger = nullptr;

		// only through AddRootComponent
		Vector<StringHash>* rootComponents = nullptr;

		Guid FindProduct(StringHash type) const {
			for (const ModelProduct& product : products) {
				if (product.type == type) {
					return product.guid;
				}
			}
			return Guid{};
		}

		SHALLTEAR_IMPORTPIPELINE_API str MakeProductKey(std::string_view kind, std::string_view name) const;

		SHALLTEAR_IMPORTPIPELINE_API bool AddRootComponent(StringHash componentType) const;
	};

	// no std::function (erasing one could run unmapped code)
	using ModelContributorFn = void (*)(const ModelContribution& contribution);

	struct ModelContributorDesc {
		StringHash id;
		u32 version = 0;
		bool wantsTracks = false;
		ModelContributorFn contribute = nullptr;
		void* user = nullptr;
		const void* owner = nullptr;
	};

	namespace ModelContributors {

		SHALLTEAR_IMPORTPIPELINE_API bool Register(const ModelContributorDesc& desc);

		SHALLTEAR_IMPORTPIPELINE_API u32 UnregisterOwnedBy(const void* owner);

	} // namespace ModelContributors

} // namespace Shalltear
