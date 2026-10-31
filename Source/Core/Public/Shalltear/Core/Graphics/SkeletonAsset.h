#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	struct BoneTransform {
		Vec3 position{};
		Quat rotation = Quat::Identity();
		Vec3 scale{1.0f, 1.0f, 1.0f};

		Mat4 ToMatrix() const { return Mat4::TRS(position, rotation, scale); }
	};

	class SHALLTEAR_CORE_API SkeletonAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(SkeletonAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		SkeletonAsset() = default;
		~SkeletonAsset() override;

		Vector<str> boneNames{GetMemoryResource(MemTag::Asset)};

		Vector<i16> parents{GetMemoryResource(MemTag::Asset)};

		Vector<BoneTransform> restPose{GetMemoryResource(MemTag::Asset)};

		u32 GetBoneCount() const { return static_cast<u32>(parents.size()); }

		i32 FindBone(std::string_view name) const;

		StringHash GetAssetTypeId() const override { return SkeletonAsset::GetReflectType()->name; }

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;
	};

} // namespace Shalltear
