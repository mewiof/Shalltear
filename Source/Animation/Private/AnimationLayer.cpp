#include "Shalltear/Animation/AnimationLayer.h"

#include "Binder.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"

#include <algorithm>

namespace Shalltear {

	bool AnimationLayer::SetMaskSubtree(const SkeletonAsset& skeleton, std::string_view rootBone, f32 value) {
		const u32 count = skeleton.GetBoneCount();
		const i32 root = skeleton.FindBone(rootBone);
		if (root < 0 || count == 0) {
			SLT_LOG_WARN("Animation", "mask root bone '{}' isn't in this rig (mask unchanged)", rootBone);
			return false;
		}
		if (mask.size() != count) {
			mask.clear();
			mask.resize(count, 0.0f);
		}
		for (u32 i = 0; i < count; ++i) {
			i32 at = static_cast<i32>(i);
			while (at >= 0) {
				if (at == root) {
					mask[i] = value;
					break;
				}
				at = skeleton.parents[static_cast<size_t>(at)];
			}
		}
		return true;
	}

	void AnimationLayer::ClearMask() { mask.clear(); }

	f32 AnimationLayer::WeightForBone(i32 bone) const {
		const f32 base = Math::Clamp01(weight);
		if (mask.empty() || bone < 0 || static_cast<size_t>(bone) >= mask.size()) {
			return base;
		}
		return base * Math::Clamp01(mask[static_cast<size_t>(bone)]);
	}

	AnimationLayer::Motion AnimationLayer::GetMotion(u32 index) const {
		return index < _motions.size() ? _motions[index].motion : Motion{};
	}

	bool AnimationLayer::SetMotionWeight(u32 index, f32 value) {
		if (index >= _motions.size()) {
			return false;
		}
		_motions[index].motion.weight = Math::IsFinite(value) ? value : 0.0f;
		return true;
	}

	bool AnimationLayer::IsPlaying() const { return _playing && GetClip().IsValid(); }

	Handle<AnimationClip> AnimationLayer::GetClip() const {
		return _motions.empty() ? Handle<AnimationClip>() : _motions[0].motion.clip;
	}

	u32 AnimationLayer::GetBoundTrackCount() const {
		u32 total = 0;
		for (const MotionSlot& slot : _motions) {
			total += slot.boundTracks;
		}
		return total;
	}

	void AnimationLayer::SetMotions(const Motion* motions, u32 count) {
		_motions.clear();
		for (u32 index = 0; index < count; ++index) {
			MotionSlot slot;
			slot.motion = motions[index];
			if (!Math::IsFinite(slot.motion.weight)) {
				slot.motion.weight = 0.0f;
			}
			if (!Math::IsFinite(slot.motion.speed)) {
				slot.motion.speed = 1.0f;
			}
			_motions.push_back(std::move(slot));
		}
	}

	f32 AnimationLayer::MotionShare(const Vector<MotionSlot>& slots, u32 index) {
		if (index >= slots.size()) {
			return 0.0f;
		}
		f32 total = 0.0f;
		for (const MotionSlot& slot : slots) {
			if (slot.motion.weight > 0.0f) {
				total += slot.motion.weight;
			}
		}
		const f32 own = slots[index].motion.weight;
		if (!(own > 0.0f) || !(total > 0.0f)) {
			return 0.0f;
		}
		return own / total;
	}

	void AnimationLayer::RefreshShares(Vector<MotionSlot>& slots) {
		for (size_t index = 0; index < slots.size(); ++index) {
			slots[index].share = MotionShare(slots, static_cast<u32>(index));
		}
	}

	i32 AnimationLayer::DominantMotion() const {
		i32 best = -1;
		f32 bestWeight = 0.0f;
		for (size_t index = 0; index < _motions.size(); ++index) {
			const MotionSlot& slot = _motions[index];
			if (!slot.motion.clip.IsValid() || !(slot.motion.weight > 0.0f)) {
				continue;
			}
			if (slot.motion.weight > bestWeight) {
				bestWeight = slot.motion.weight;
				best = static_cast<i32>(index);
			}
		}
		return best;
	}

	void AnimationLayer::SetTime(f32 seconds) { _time = Math::IsFinite(seconds) ? seconds : 0.0f; }

	const AnimationLayer::BoneBinding* AnimationLayer::FindRow(const Vector<BoneBinding>& rows, i16 bone) {
		auto found = std::lower_bound(rows.begin(), rows.end(), bone,
									  [](const BoneBinding& row, i16 value) { return row.bone < value; });
		return found != rows.end() && found->bone == bone ? &*found : nullptr;
	}

	void AnimationLayer::ReleaseBindings() {
		for (MotionSlot& slot : _motions) {
			slot.bones.clear();
			slot.events.clear();
			slot.boundClip = 0;
			slot.boundGeneration = 0;
			slot.boundTracks = 0;
		}
		_properties.clear();
		_blendShapes.clear();
		_intervalWarned = false;
	}

	bool AnimationLayer::NeedsRebind() const {
		for (const MotionSlot& slot : _motions) {
			const AnimationClip* asset = slot.motion.clip.Get();
			if (slot.boundClip != slot.motion.clip.GetInstanceId() ||
				slot.boundGeneration != (asset ? asset->generation : 0)) {
				return true;
			}
		}
		return false;
	}

	void AnimationLayer::Rebind(Handle<SceneObject> self, const SkeletonAsset* skeleton, i32 layerIndex) {
		_properties.clear();
		_blendShapes.clear();
		_intervalWarned = false;
		for (size_t motion = 0; motion < _motions.size(); ++motion) {
			RebindMotion(self, skeleton, layerIndex, static_cast<u32>(motion));
		}
	}

	void AnimationLayer::RebindMotion(Handle<SceneObject> self, const SkeletonAsset* skeleton, i32 layerIndex, u32 motionIndex) {
		MotionSlot& slot = _motions[motionIndex];
		slot.bones.clear();
		slot.events.clear();
		slot.boundTracks = 0;
		AnimationClip* asset = slot.motion.clip.Get();
		slot.boundClip = slot.motion.clip.GetInstanceId();
		slot.boundGeneration = asset ? asset->generation : 0;
		if (!asset) {
			return;
		}

		u32 dropped = 0;
		u32 droppedProperties = 0;
		for (size_t index = 0; index < asset->tracks.size(); ++index) {
			const AnimationClip::Track& track = asset->tracks[index];

			if (track.kind == AnimationTrackKind::Event || track.kind == AnimationTrackKind::Method ||
				track.kind == AnimationTrackKind::Audio) {
				EventBinding signal;
				signal.track = static_cast<i32>(index);
				if (track.kind == AnimationTrackKind::Event) {
					slot.events.push_back(signal);
					++slot.boundTracks;
					continue;
				}

				Handle<SceneObject> target = AnimationImpl::ResolvePath(self, track.objectPath);
				SceneObject* object = target.Get();
				if (!object) {
					++dropped;
					continue;
				}
				signal.object = target;
				if (track.kind == AnimationTrackKind::Audio) {
					slot.events.push_back(signal);
					++slot.boundTracks;
					continue;
				}

				const TypeInfo* ownerType = nullptr;
				if (track.componentType == StringHash{}) {
					ownerType = SceneObject::GetReflectType();
				} else {
					const TypeInfo* wanted = TypeRegistry::FindType(track.componentType);
					Handle<Component> component =
						wanted ? AnimationImpl::FindComponentOfType(object, wanted) : Handle<Component>();
					Component* resolved = component.Get();
					if (!resolved) {
						++dropped;
						continue;
					}
					signal.component = component;
					ownerType = resolved->GetType();
				}
				if (!AnimationImpl::IsCallableFromClip(ownerType, StringHash(track.name))) {
					SLT_LOG_WARN("Animation", "Animator on '{}': clip '{}' calls '{}' on '{}' (not a reflected method with no arguments). Track dropped",
								 self.IsValid() ? self->name : str("<detached>"), asset->virtualPath, track.name,
								 ownerType && ownerType->displayName ? ownerType->displayName : "<unknown type>");
					++dropped;
					continue;
				}
				slot.events.push_back(signal);
				++slot.boundTracks;
				continue;
			}

			if (track.kind == AnimationTrackKind::BlendShape) {
				if (layerIndex != 0 || motionIndex != 0) {
					++droppedProperties;
					continue;
				}
				Handle<SceneObject> target = AnimationImpl::ResolvePath(self, track.objectPath);
				SceneObject* object = target.Get();
				if (!object) {
					++dropped;
					continue;
				}
				Handle<SkinnedMeshRenderer> renderer = object->GetComponent<SkinnedMeshRenderer>();
				if (!renderer.IsValid()) {
					renderer = object->GetComponentInChildren<SkinnedMeshRenderer>();
				}
				MeshAsset* mesh = renderer.IsValid() ? renderer->GetMesh().Get() : nullptr;
				const i32 shape = mesh ? mesh->FindBlendShape(track.name) : -1;
				if (shape < 0) {
					SLT_LOG_WARN("Animation", "Animator on '{}': clip '{}' drives blend shape '{}' (no SkinnedMeshRenderer on that object has it)",
								 self.IsValid() ? self->name : str("<detached>"), asset->virtualPath, track.name);
					++dropped;
					continue;
				}
				BlendShapeBinding binding;
				binding.track = static_cast<i32>(index);
				binding.renderer = renderer;
				binding.shape = shape;
				_blendShapes.push_back(binding);
				++slot.boundTracks;
				continue;
			}

			if (track.kind != AnimationTrackKind::Property) {
				const i32 bone = skeleton ? skeleton->FindBone(track.name) : -1;
				if (bone < 0 || bone > 0x7FFF) {
					++dropped;
					continue;
				}
				BoneBinding* entry = nullptr;
				if (!slot.bones.empty() && slot.bones.back().bone == static_cast<i16>(bone)) {
					entry = &slot.bones.back();
				} else {
					for (BoneBinding& candidate : slot.bones) {
						if (candidate.bone == static_cast<i16>(bone)) {
							entry = &candidate;
							break;
						}
					}
				}
				if (!entry) {
					BoneBinding fresh;
					fresh.bone = static_cast<i16>(bone);
					slot.bones.push_back(fresh);
					entry = &slot.bones.back();
				}
				i32* channel = track.kind == AnimationTrackKind::BonePosition	? &entry->position
							   : track.kind == AnimationTrackKind::BoneRotation ? &entry->rotation
																				: &entry->scale;
				if (*channel < 0) {
					*channel = static_cast<i32>(index);
					++slot.boundTracks;
				}
				continue;
			}

			if (layerIndex != 0 || motionIndex != 0) {
				++droppedProperties;
				continue;
			}

			PropertyBinding binding;
			binding.track = static_cast<i32>(index);
			Handle<SceneObject> target = AnimationImpl::ResolvePath(self, track.objectPath);
			SceneObject* object = target.Get();
			if (!object) {
				++dropped;
				continue;
			}
			binding.object = target;

			const TypeInfo* ownerType = nullptr;
			if (track.componentType == StringHash{}) {
				ownerType = SceneObject::GetReflectType();
			} else {
				const TypeInfo* wanted = TypeRegistry::FindType(track.componentType);
				Handle<Component> component =
					wanted ? AnimationImpl::FindComponentOfType(object, wanted) : Handle<Component>();
				Component* resolved = component.Get();
				if (!resolved) {
					++dropped;
					continue;
				}
				binding.component = component;
				ownerType = resolved->GetType();
			}

			binding.field.ownerType = ownerType->name;
			binding.field.field = StringHash(track.name);
			const FieldInfo* field = binding.field.Resolve();
			if (!field || AnimationImpl::ScalarCount(field->fieldType) == 0) {
				++dropped;
				continue;
			}
			_properties.push_back(binding);
			++slot.boundTracks;
		}

		std::sort(slot.bones.begin(), slot.bones.end(),
				  [](const BoneBinding& a, const BoneBinding& b) { return a.bone < b.bone; });

		if (dropped > 0) {
			SLT_LOG_WARN("Animation", "Animator on '{}': {} of {} track(s) of clip '{}' bind to nothing (dropped)",
						 self.IsValid() ? self->name : str("<detached>"), dropped,
						 asset->tracks.size(), asset->virtualPath);
		}
		if (droppedProperties > 0) {
			SLT_LOG_WARN("Animation", "Animator on '{}': {} property track(s) of clip '{}' dropped on layer {} (only the base layer writes reflected fields)",
						 self.IsValid() ? self->name : str("<detached>"), droppedProperties,
						 asset->virtualPath, layerIndex);
		}
	}

} // namespace Shalltear
