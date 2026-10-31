#pragma once

#include "Shalltear/Animation/AnimationAPI.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class AnimationClip;

	class SHALLTEAR_ANIMATION_API RetargetProfile : public Asset {
	public:
		SLT_REFLECT_CLASS(RetargetProfile)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		struct Chain {
			str role;

			str mirror;

			Vector<i16> bones{GetMemoryResource(MemTag::Asset)};
		};

		RetargetProfile() = default;
		~RetargetProfile() override;

		Guid skeletonGuid;
		Handle<SkeletonAsset> skeleton;

		Vector<Chain> chains{GetMemoryResource(MemTag::Asset)};

		Vector<str> goals{GetMemoryResource(MemTag::Asset)};

		Vector<BoneTransform> referencePose{GetMemoryResource(MemTag::Asset)};

		const Chain* FindChain(std::string_view role) const;

		std::span<const BoneTransform> GetReferencePose() const;

		StringHash GetAssetTypeId() const override { return RetargetProfile::GetReflectType()->name; }

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;
	protected:
		void OnLoad() override;
		void OnUnload() override;
	};

	namespace Retarget {

		// caller owns the result
		SHALLTEAR_ANIMATION_API RetargetProfile* AutoMapHumanoid(Handle<SkeletonAsset> skeleton);

		// caller owns the clip
		SHALLTEAR_ANIMATION_API AnimationClip* Bake(const AnimationClip& clip, const RetargetProfile& source,
													const RetargetProfile& target);

	} // namespace Retarget

} // namespace Shalltear
