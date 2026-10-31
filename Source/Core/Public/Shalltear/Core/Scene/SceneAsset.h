#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct SceneDocument;

	class SHALLTEAR_CORE_API SceneAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(SceneAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		SceneAsset();
		~SceneAsset() override;

		bool Import(const Path& sourcePath) override;
		bool Export(const Path& destPath) const override;
		StringHash GetAssetTypeId() const override { return SceneAsset::GetReflectType()->name; }

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;

		bool CookImportState(CookWriter& importState) const override;

		const SceneDocument* GetDocument() const { return _document; }
	private:
		friend class SceneSystem;
		friend struct SceneInternalAccess;

		SceneDocument* _document = nullptr;
	};

} // namespace Shalltear
