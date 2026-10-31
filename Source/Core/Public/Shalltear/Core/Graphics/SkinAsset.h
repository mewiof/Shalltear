#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class SHALLTEAR_CORE_API SkinAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(SkinAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		SkinAsset() = default;
		~SkinAsset() override;

		Guid skeletonGuid;
		Handle<SkeletonAsset> skeleton;

		// influences idx this (not the skeleton)
		Vector<i16> jointToBone{GetMemoryResource(MemTag::Asset)};

		// mesh local -> joint local at bind pose
		Vector<Mat4> inverseBind{GetMemoryResource(MemTag::Asset)};

		u32 GetJointCount() const { return static_cast<u32>(jointToBone.size()); }

		StringHash GetAssetTypeId() const override { return SkinAsset::GetReflectType()->name; }

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;
	protected:
		void OnLoad() override;
		void OnUnload() override;
	};

} // namespace Shalltear
