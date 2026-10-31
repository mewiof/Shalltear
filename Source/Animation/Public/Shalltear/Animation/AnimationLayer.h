#pragma once

#include "Shalltear/Animation/AnimationAPI.h"
#include "Shalltear/Animation/AnimationClip.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Graphics/SkinnedMeshRenderer.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	enum class AnimationBlendMode : u8 {
		Override = 0,
		Additive = 1,
	};

	class SHALLTEAR_ANIMATION_API AnimationLayer {
	public:
		AnimationLayer() = default;

		struct Motion {
			Handle<AnimationClip> clip;

			f32 weight = 1.0f;

			f32 speed = 1.0f;
		};

		u32 GetMotionCount() const { return static_cast<u32>(_motions.size()); }

		Motion GetMotion(u32 index) const;

		bool SetMotionWeight(u32 index, f32 value);

		bool IsPlaying() const;
		Handle<AnimationClip> GetClip() const;

		f32 weight = 1.0f;

		f32 speed = 1.0f;

		AnimationBlendMode blend = AnimationBlendMode::Override;

		// indices are the clip rig's
		Vector<f32> mask{GetMemoryResource(MemTag::Scene)};

		bool SetMaskSubtree(const SkeletonAsset& skeleton, std::string_view rootBone, f32 value);

		void ClearMask();

		f32 WeightForBone(i32 bone) const;

		f32 GetTime() const { return _time; }
		void SetTime(f32 seconds);

		void Stop() { _playing = false; }

		u32 GetBoundTrackCount() const;
	private:
		friend class Animator;
		friend class PlayHandle;

		struct BoneBinding {
			i16 bone = -1;
			i32 position = -1;
			i32 rotation = -1;
			i32 scale = -1;
		};

		struct PropertyBinding {
			i32 track = -1;
			Handle<SceneObject> object;
			Handle<Component> component;
			FieldRef field;
		};

		struct BlendShapeBinding {
			i32 track = -1;
			Handle<SkinnedMeshRenderer> renderer;
			i32 shape = -1;
		};

		struct EventBinding {
			i32 track = -1;
			Handle<SceneObject> object;
			Handle<Component> component;
		};

		struct MotionSlot {
			Motion motion;
			Vector<BoneBinding> bones{GetMemoryResource(MemTag::Scene)};
			Vector<EventBinding> events{GetMemoryResource(MemTag::Scene)};

			f32 share = 0.0f;

			u64 boundClip = 0;
			u32 boundGeneration = 0;
			u32 boundTracks = 0;
		};

		void ReleaseBindings();
		void Rebind(Handle<SceneObject> self, const SkeletonAsset* skeleton, i32 layerIndex);
		void RebindMotion(Handle<SceneObject> self, const SkeletonAsset* skeleton, i32 layerIndex, u32 motionIndex);
		bool NeedsRebind() const;

		void SetMotions(const Motion* motions, u32 count);

		static f32 MotionShare(const Vector<MotionSlot>& slots, u32 index);

		static void RefreshShares(Vector<MotionSlot>& slots);

		i32 DominantMotion() const;

		static const BoneBinding* FindRow(const Vector<BoneBinding>& rows, i16 bone);

		Vector<MotionSlot> _motions{GetMemoryResource(MemTag::Scene)};

		Vector<PropertyBinding> _properties{GetMemoryResource(MemTag::Scene)};

		Vector<BlendShapeBinding> _blendShapes{GetMemoryResource(MemTag::Scene)};

		f32 _time = 0.0f;

		f32 _prevTime = 0.0f;

		bool _active = false;

		u32 _playId = 0;
		bool _playing = false;

		bool _intervalWarned = false;

		Vector<MotionSlot> _fadeMotions{GetMemoryResource(MemTag::Scene)};
		f32 _fadeTime = 0.0f;
		bool _fadeValid = false;
	};

} // namespace Shalltear
