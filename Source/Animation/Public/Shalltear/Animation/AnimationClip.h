#pragma once

#include "Shalltear/Animation/AnimationAPI.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	enum class AnimationTrackKind : u8 {
		BonePosition = 0,
		BoneRotation = 1,
		BoneScale = 2,
		Property = 3,
		Event = 4,
		Method = 5,

		BlendShape = 6,

		Audio = 7,
	};

	enum class AnimationInterpolation : u8 {
		Step = 0,
		Linear = 1,
		CubicSpline = 2,
	};

	enum class AnimationLoopMode : u8 {
		None = 0,
		Loop = 1,
		PingPong = 2,
	};

	inline constexpr u32 AnimationValuesPerKey(AnimationInterpolation interp) {
		return interp == AnimationInterpolation::CubicSpline ? 3u : 1u;
	}

	inline constexpr u8 ANIMATION_WHOLE_VALUE = 0xFF;

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::AnimationLoopMode)
SLT_ENUM_VALUE(None)
SLT_ENUM_VALUE(Loop)
SLT_ENUM_VALUE(PingPong)
SLT_END_ENUM()

namespace Shalltear {

	class SHALLTEAR_ANIMATION_API AnimationClip : public Asset {
	public:
		SLT_REFLECT_CLASS(AnimationClip)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_REFLECT_FIELD(length, SLT_READONLY)
		SLT_REFLECT_FIELD(frameRate, SLT_READONLY)
		SLT_REFLECT_FIELD(loop)
		SLT_REFLECT_FIELD(additiveReferenceTime)
		SLT_REFLECT_FIELD(bakeRootMotionIntoPose)
		SLT_END_REFLECT()

		struct Track {
			AnimationTrackKind kind = AnimationTrackKind::BonePosition;
			AnimationInterpolation interp = AnimationInterpolation::Linear;

			u8 component = ANIMATION_WHOLE_VALUE;

			str objectPath;

			StringHash componentType;

			str name;

			// ascending, clip starts at 0
			Vector<f32> times{GetMemoryResource(MemTag::Asset)};

			Vector<Vec4> values{GetMemoryResource(MemTag::Asset)};

			Vector<Guid> assets{GetMemoryResource(MemTag::Asset)};
		};

		AnimationClip() = default;
		~AnimationClip() override;

		Vector<Track> tracks{GetMemoryResource(MemTag::Asset)};

		f32 length = 0.0f;

		f32 frameRate = 0.0f;

		AnimationLoopMode loop = AnimationLoopMode::None;

		f32 additiveReferenceTime = 0.0f;

		bool bakeRootMotionIntoPose = true;

		Guid skeletonGuid;
		Handle<SkeletonAsset> skeleton;

		u32 generation = 1;

		StringHash GetAssetTypeId() const override { return AnimationClip::GetReflectType()->name; }

		bool Import(const Path& sourcePath) override;
		bool Export(const Path& destPath) const override;

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;

		void HoldCueAssets();
	protected:
		void OnLoad() override;
		void OnUnload() override;
	private:
		Vector<Handle<Asset>> _heldCues{GetMemoryResource(MemTag::Asset)};
	};

} // namespace Shalltear
