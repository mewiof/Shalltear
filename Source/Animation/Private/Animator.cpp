#include "Shalltear/Animation/Animator.h"

#include "AnimationPass.h"
#include "AnimationSettings.h"
#include "Binder.h"
#include "ClipSampler.h"
#include "PoseOps.h"
#include "Retarget.h"

#include "Shalltear/Core/Asset/AssetCues.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Math/Quat.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		constexpr f32 CAPTURE_FALLBACK_STEP = 1.0f / 60.0f;

		f32 SampleAt(const AnimationClip& asset, f32 clock, f32 rate, f32 back) {
			return AnimationImpl::WrapTime(clock - back * rate, asset.length, asset.loop);
		}

		BoneTransform RestPose(const SkeletonAsset& skeleton, i16 bone) {
			const size_t index = static_cast<size_t>(bone);
			return index < skeleton.restPose.size() ? skeleton.restPose[index] : BoneTransform{};
		}

		u32 IntervalRuns(f32 from, f32 to, const AnimationClip& asset, AnimationImpl::ClipSegment* out, bool& warned,
						 std::string_view owner) {
			bool truncated = false;
			const u32 runs = AnimationImpl::SplitInterval(from, to, asset.length, asset.loop, out,
														  AnimationImpl::ANIMATION_MAX_CLIP_RUNS, &truncated);
			if (truncated && !warned) {
				warned = true;
				SLT_LOG_WARN("Animation", "Animator on '{}': clip '{}' wrapped more than {} times in one frame (oldest events + root motion dropped)",
							 owner, asset.virtualPath, AnimationImpl::ANIMATION_MAX_CLIP_RUNS);
			}
			return runs;
		}

		void PatchChannel(BoneTransform& transform, const AnimationClip::Track& track, const Vec4& value) {
			switch (track.kind) {
			case AnimationTrackKind::BonePosition:
				transform.position = Vec3(value.x, value.y, value.z);
				break;
			case AnimationTrackKind::BoneRotation:
				transform.rotation = Quat(value.x, value.y, value.z, value.w);
				break;
			case AnimationTrackKind::BoneScale:
				transform.scale = Vec3(value.x, value.y, value.z);
				break;
			default:
				break;
			}
		}

		BoneTransform SampleChannels(const AnimationClip& asset, i32 position, i32 rotation, f32 at) {
			BoneTransform out;
			const i32 channels[2] = {position, rotation};
			for (i32 track : channels) {
				if (track < 0 || static_cast<size_t>(track) >= asset.tracks.size()) {
					continue;
				}
				const AnimationClip::Track& curve = asset.tracks[static_cast<size_t>(track)];
				PatchChannel(out, curve, AnimationImpl::SampleTrack(curve, at));
			}
			return out;
		}

	} // anonymous namespace

	struct Animator::Transition {
		Vector<AnimationImpl::InertialBone> offsets{GetMemoryResource(MemTag::Scene)};
		f32 elapsed = 0.0f;
		f32 duration = 0.0f;
	};

	struct Animator::Retargeting {
		Handle<RetargetProfile> source;
		Handle<RetargetProfile> target;

		AnimationImpl::RetargetMap map;

		Vector<BoneTransform> sourcePose{GetMemoryResource(MemTag::Scene)};
		Vector<BoneTransform> targetPose{GetMemoryResource(MemTag::Scene)};

		u64 builtSource = 0;
		u64 builtTarget = 0;
		u64 builtRig = 0;

		bool valid = false;

		bool warned = false;
	};

	Animator::~Animator() {
		EndTransition();
		Delete(_retarget);
		_retarget = nullptr;
		for (AnimationLayer* layer : _layers) {
			Delete(layer);
		}
		_layers.clear();
	}

	void Animator::OnEnable() {
		AnimationImpl::RegisterAnimator(Handle<Animator>(this));
	}

	void Animator::OnDisable() {
		AnimationImpl::UnregisterAnimator(Handle<Animator>(this));

		for (AnimationLayer* layer : _layers) {
			layer->ReleaseBindings();
			layer->_active = false;
			layer->_fadeValid = false;
			layer->_fadeMotions.clear();
		}
		_foldBones.clear();
		EndTransition();

		_rootAnchored = false;
		_rootBone = -1;
		_rootDelta = {};
	}

	AnimationLayer& Animator::EnsureBaseLayer() {
		if (_layers.empty()) {
			_layers.push_back(New<AnimationLayer>(MemTag::Scene));
		}
		return *_layers[0];
	}

	i32 Animator::AddLayer() {
		EnsureBaseLayer();
		_layers.push_back(New<AnimationLayer>(MemTag::Scene));
		return static_cast<i32>(_layers.size()) - 1;
	}

	AnimationLayer* Animator::GetLayer(i32 index) {
		return index >= 0 && static_cast<size_t>(index) < _layers.size() ? _layers[static_cast<size_t>(index)] : nullptr;
	}

	const AnimationLayer* Animator::GetLayer(i32 index) const {
		return index >= 0 && static_cast<size_t>(index) < _layers.size() ? _layers[static_cast<size_t>(index)] : nullptr;
	}

	bool Animator::RemoveLayer(i32 index) {
		if (index <= 0 || static_cast<size_t>(index) >= _layers.size()) {
			return false;
		}
		Delete(_layers[static_cast<size_t>(index)]);
		_layers.erase(_layers.begin() + index);
		RebuildFoldSet();
		return true;
	}

	PlayHandle Animator::Play(Handle<AnimationClip> what, f32 fade, i32 layerIndex) {
		AnimationLayer::Motion motion;
		motion.clip = what;
		return PlayBlend(&motion, 1, fade, layerIndex);
	}

	PlayHandle Animator::PlayBlend(const AnimationLayer::Motion* motions, u32 count, f32 fade, i32 layerIndex) {
		EnsureBaseLayer();
		AnimationLayer* target = GetLayer(layerIndex);
		if (!target) {
			SLT_LOG_WARN("Animation", "Play on layer {} of an animator with {} layer(s) (ignored)", layerIndex, _layers.size());
			return {};
		}
		AnimationLayer& layer = *target;

		const AnimationSettings& settings = AnimationImpl::GetSettings();
		const f32 duration = fade < 0.0f ? settings.defaultTransitionDuration : fade;
		if (duration > 0.0f && layer.IsPlaying()) {
			layer._fadeMotions = layer._motions;
			layer._fadeTime = layer._time;
			layer._fadeValid = true;
			_pendingFade = Math::Max(_pendingFade, duration);
		}

		layer.SetMotions(motions, count);
		layer._time = 0.0f;
		layer._playing = layer.GetClip().IsValid();

		++layer._playId;

		layer.ReleaseBindings();
		if (layerIndex == 0) {
			_clip = layer.GetClip();
		}
		return layer._playing ? PlayHandle(Handle<Animator>(this), layerIndex, layer._playId) : PlayHandle();
	}

	void Animator::Stop() {
		for (AnimationLayer* layer : _layers) {
			layer->Stop();
		}
	}

	bool Animator::IsPlaying() const {
		for (const AnimationLayer* layer : _layers) {
			if (layer->IsPlaying()) {
				return true;
			}
		}
		return false;
	}

	f32 Animator::GetTime() const { return _layers.empty() ? 0.0f : _layers[0]->GetTime(); }

	void Animator::SetTime(f32 seconds) {
		if (!_layers.empty()) {
			_layers[0]->SetTime(seconds);
		}
	}

	u32 Animator::GetBoundTrackCount() const {
		u32 total = 0;
		for (const AnimationLayer* layer : _layers) {
			total += layer->GetBoundTrackCount();
		}
		return total;
	}

	Handle<Skeleton> Animator::GetSkeleton() const {
		SceneObject* owner = GetOwner().Get();
		if (!owner) {
			return {};
		}
		return owner->GetComponent<Skeleton>();
	}

	void Animator::DropPoseSpace() {
		for (AnimationLayer* layer : _layers) {
			layer->ReleaseBindings();
		}
		_foldBones.clear();
		EndTransition();
		_rootAnchored = false;
		_rootBone = -1;
		_rootDelta = {};
	}

	void Animator::SetRetarget(Handle<RetargetProfile> source, Handle<RetargetProfile> target) {
		if (!source.IsValid() || !target.IsValid()) {
			ClearRetarget();
			return;
		}
		if (!_retarget) {
			_retarget = New<Retargeting>(MemTag::Scene);
		}
		_retarget->source = source;
		_retarget->target = target;
		_retarget->valid = false;
		_retarget->warned = false;
		_retarget->builtSource = 0;
		_retarget->builtTarget = 0;
		_retarget->builtRig = 0;
		DropPoseSpace();
	}

	void Animator::ClearRetarget() {
		if (!_retarget) {
			return;
		}
		Delete(_retarget);
		_retarget = nullptr;
		DropPoseSpace();
	}

	bool Animator::IsRetargeting() const {
		return _retarget != nullptr && _retarget->valid;
	}

	bool Animator::EnsureRetargetMap(const SkeletonAsset* rig) {
		if (!_retarget) {
			return false;
		}
		const RetargetProfile* source = _retarget->source.Get();
		const RetargetProfile* target = _retarget->target.Get();
		if (!source || !target || !rig) {
			_retarget->valid = false;
			return false;
		}

		const u64 rigId = rig->GetInstanceId();
		if (_retarget->builtSource == _retarget->source.GetInstanceId() &&
			_retarget->builtTarget == _retarget->target.GetInstanceId() && _retarget->builtRig == rigId) {
			return _retarget->valid;
		}
		_retarget->builtSource = _retarget->source.GetInstanceId();
		_retarget->builtTarget = _retarget->target.GetInstanceId();
		_retarget->builtRig = rigId;
		_retarget->valid = false;

		DropPoseSpace();

		if (target->skeleton.Get() != rig) {
			if (!_retarget->warned) {
				_retarget->warned = true;
				SLT_LOG_WARN("Animation", "Animator on '{}': the retarget target profile '{}' doesn't use this object's skeleton (retarget off)",
							 GetOwner().IsValid() ? GetOwner()->name : str("<detached>"), target->virtualPath);
			}
			return false;
		}
		if (!AnimationImpl::BuildRetargetMap(*source, *target, _retarget->map,
											 AnimationImpl::RetargetSides::AsAuthored)) {
			return false;
		}

		const SkeletonAsset* sourceRig = source->skeleton.Get();
		_retarget->sourcePose.assign(sourceRig->parents.size(), BoneTransform{});
		_retarget->targetPose.assign(rig->parents.size(), BoneTransform{});
		_retarget->valid = true;
		_retarget->warned = false;
		return true;
	}

	const SkeletonAsset* Animator::PoseSkeleton(const SkeletonAsset* rig) const {
		if (_retarget && _retarget->valid) {
			return _retarget->source.Get()->skeleton.Get();
		}
		return rig;
	}

	BoneTransform Animator::PoseSeed(const SkeletonAsset& skeleton, i16 bone) const {
		if (_retarget && _retarget->valid) {
			const std::span<const BoneTransform> reference = _retarget->source.Get()->GetReferencePose();
			const size_t index = static_cast<size_t>(bone);
			return index < reference.size() ? reference[index] : BoneTransform{};
		}
		return RestPose(skeleton, bone);
	}

	void Animator::AdoptAuthoredClip() {
		AnimationLayer& base = EnsureBaseLayer();
		if (base.GetClip().GetInstanceId() == _clip.GetInstanceId()) {
			return;
		}
		AnimationLayer::Motion motion;
		motion.clip = _clip;
		base.SetMotions(&motion, 1);
		base.ReleaseBindings();
	}

	void Animator::RebuildFoldSet() {
		_foldBones.clear();
		for (const AnimationLayer* layer : _layers) {
			if (layer->_active) {
				for (const AnimationLayer::MotionSlot& slot : layer->_motions) {
					for (const AnimationLayer::BoneBinding& row : slot.bones) {
						_foldBones.push_back(row.bone);
					}
				}
			}
			for (const AnimationLayer::MotionSlot& slot : layer->_fadeMotions) {
				for (const AnimationLayer::BoneBinding& row : slot.bones) {
					_foldBones.push_back(row.bone);
				}
			}
		}
		if (_transition) {
			for (const AnimationImpl::InertialBone& offset : _transition->offsets) {
				_foldBones.push_back(offset.bone);
			}
		}
		std::sort(_foldBones.begin(), _foldBones.end());
		_foldBones.erase(std::unique(_foldBones.begin(), _foldBones.end()), _foldBones.end());
	}

	void Animator::EndTransition() {
		if (_transition) {
			Delete(_transition);
			_transition = nullptr;
		}
	}

	void Animator::Evaluate(f32 deltaTime) {
		AdoptAuthoredClip();

		Handle<SceneObject> self = GetOwner();
		Skeleton* rig = GetSkeleton().Get();
		SkeletonAsset* rigSkeleton = rig ? rig->GetSkeleton().Get() : nullptr;

		const bool retargeting = EnsureRetargetMap(rigSkeleton);
		const SkeletonAsset* skeleton = PoseSkeleton(rigSkeleton);

		bool rebound = false;
		for (size_t index = 0; index < _layers.size(); ++index) {
			AnimationLayer& layer = *_layers[index];

			if (!layer.IsPlaying() && _playAutomatically && layer._playId == 0 && layer.GetClip().IsValid()) {
				Play(layer.GetClip(), 0.0f, static_cast<i32>(index));
			}

			if (layer.NeedsRebind()) {
				layer.Rebind(self, skeleton, static_cast<i32>(index));
				rebound = true;
			}

			layer._prevTime = layer._time;

			AnimationLayer::RefreshShares(layer._motions);

			const i32 dominant = layer.DominantMotion();
			const AnimationClip* asset = dominant >= 0 ? layer._motions[static_cast<size_t>(dominant)].motion.clip.Get() : nullptr;
			layer._active = layer._playId != 0 && asset != nullptr;
			if (!layer._active || !layer.IsPlaying()) {
				continue;
			}
			const f32 rate = _speed * layer.speed;
			if (Math::IsFinite(deltaTime)) {
				layer._time += deltaTime * rate;
				if (layer._fadeValid) {
					layer._fadeTime += deltaTime * rate;
				}
			}
			const f32 dominantSpeed = layer._motions[static_cast<size_t>(dominant)].motion.speed;
			const f32 layerLength = dominantSpeed != 0.0f ? asset->length / Math::Abs(dominantSpeed) : 0.0f;
			if (asset->loop == AnimationLoopMode::None && layerLength > 0.0f) {
				if (layer._time >= layerLength) {
					layer._time = layerLength;
					layer._playing = false;
				} else if (layer._time <= 0.0f && rate < 0.0f) {
					layer._time = 0.0f;
					layer._playing = false;
				}
			}
		}

		if (_pendingFade > 0.0f) {
			CaptureTransition(skeleton, deltaTime);
			_pendingFade = 0.0f;
			for (AnimationLayer* layer : _layers) {
				layer->_fadeValid = false;
				layer->_fadeMotions.clear();
			}
			RebuildFoldSet();
		} else if (rebound) {
			RebuildFoldSet();
		}
		if (_transition && _transition->elapsed >= _transition->duration) {
			EndTransition();
			RebuildFoldSet();
		}

		const i16 rootBone = RootMotionBone(skeleton);
		if (rootBone < 0) {
			_rootAnchored = false;
			_rootBone = -1;
			_rootDelta = {};
		} else {
			if (!_rootAnchored || _rootBone != rootBone) {
				const f32 back = Math::IsFinite(deltaTime) ? deltaTime : 0.0f;
				_rootAnchor = FoldBone(PoseSeed(*skeleton, rootBone), rootBone, false, back);
				_rootAnchored = true;
				_rootBone = rootBone;
			}
			AccumulateRootMotion(rootBone);
		}

		if (retargeting) {
			const std::span<const BoneTransform> reference = _retarget->source.Get()->GetReferencePose();
			_retarget->sourcePose.assign(reference.begin(), reference.end());
			_retarget->sourcePose.resize(skeleton->parents.size());
		}
		if (skeleton && (rig || retargeting)) {
			size_t offsetCursor = 0;
			const size_t offsetCount = _transition ? _transition->offsets.size() : 0;
			for (i16 bone : _foldBones) {
				BoneTransform folded;
				if (bone == rootBone && _rootAnchored) {
					folded = _rootAnchor;
				} else {
					folded = FoldBone(PoseSeed(*skeleton, bone), bone, false, 0.0f);

					while (offsetCursor < offsetCount && _transition->offsets[offsetCursor].bone < bone) {
						++offsetCursor;
					}
					if (offsetCursor < offsetCount && _transition->offsets[offsetCursor].bone == bone) {
						folded = AnimationImpl::ApplyInertialBone(folded, _transition->offsets[offsetCursor], _transition->elapsed);
					}
				}
				if (retargeting) {
					_retarget->sourcePose[static_cast<size_t>(bone)] = folded;
				} else {
					rig->SetBoneLocalPose(static_cast<u32>(bone), folded);
				}
			}
		}
		if (retargeting && rig) {
			AnimationImpl::SolveRetargetPose(_retarget->map, *_retarget->source.Get(), *_retarget->target.Get(),
											 std::span<const BoneTransform>(_retarget->sourcePose.data(), _retarget->sourcePose.size()),
											 std::span<BoneTransform>(_retarget->targetPose.data(), _retarget->targetPose.size()));
			for (size_t bone = 0; bone < _retarget->targetPose.size(); ++bone) {
				if (_retarget->map.bones[bone].sourceA >= 0) {
					rig->SetBoneLocalPose(static_cast<u32>(bone), _retarget->targetPose[bone]);
				}
			}
		}
		if (_transition && Math::IsFinite(deltaTime)) {
			_transition->elapsed += deltaTime;
		}

		if (_applyRootMotion && _rootAnchored) {
			if (SceneObject* owner = self.Get()) {
				const Vec3 scaled = owner->GetLocalScale() * _rootDelta.position;
				owner->SetLocalPosition(owner->GetLocalPosition() + owner->GetLocalRotation() * scaled);
				owner->SetLocalRotation((owner->GetLocalRotation() * _rootDelta.rotation).Normalized());
			}
		}

		WriteProperties(*_layers[0]);

		if (rig) {
			rig->UpdatePose();
		}

		Vector<PendingSignal> pending{GetMemoryResource(MemTag::Scene)};
		for (size_t index = 0; index < _layers.size(); ++index) {
			AnimationLayer& layer = *_layers[index];
			const i32 dominant = layer.DominantMotion();
			if (dominant < 0 || layer._motions[static_cast<size_t>(dominant)].events.empty()) {
				continue;
			}
			const AnimationClip* asset = layer._motions[static_cast<size_t>(dominant)].motion.clip.Get();
			const bool closeEnd = asset != nullptr && asset->loop == AnimationLoopMode::None && !layer._playing;
			CollectSignals(layer, static_cast<i32>(index), closeEnd, pending);
		}
		if (!pending.empty()) {
			Handle<Animator> alive(this);
			for (const PendingSignal& signal : pending) {
				if (!alive.Get()) {
					break;
				}
				if (signal.kind == AnimationTrackKind::Audio) {
					AssetCue cue;
					cue.asset = signal.cue;
					cue.object = signal.object;
					cue.volume = signal.event.value.x;
					AssetCues::Cue(cue);
					continue;
				}
				if (signal.kind == AnimationTrackKind::Event) {
					for (size_t at = 0; at < _subscribers.size(); ++at) {
						AnimationEventFn fn = _subscribers[at].fn;
						void* user = _subscribers[at].user;
						if (fn) {
							fn(signal.event, user);
						}
						if (!alive.Get()) {
							break;
						}
					}
					continue;
				}
				SceneObject* object = signal.object.Get();
				if (!object) {
					continue;
				}
				Component* component = signal.component.Get();
				if (signal.component.GetInstanceId() != 0 && !component) {
					continue;
				}
				void* instance = component ? static_cast<void*>(component) : static_cast<void*>(object);
				const TypeInfo* type = component ? component->GetType() : SceneObject::GetReflectType();
				AnimationImpl::CallAnimatedMethod(instance, type, signal.event.name);
			}
		}
	}

	BoneTransform Animator::FoldBone(const BoneTransform& seed, i16 bone, bool outgoing, f32 back) const {
		BoneTransform accumulated = seed;
		for (const AnimationLayer* layerPtr : _layers) {
			const AnimationLayer& layer = *layerPtr;

			const Vector<AnimationLayer::MotionSlot>* slots = nullptr;
			f32 clock = 0.0f;
			if (outgoing && layer._fadeValid) {
				slots = &layer._fadeMotions;
				clock = layer._fadeTime;
			} else if (layer._active) {
				slots = &layer._motions;
				clock = layer._time;
			}
			if (!slots || slots->empty()) {
				continue;
			}
			const f32 weight = layer.WeightForBone(bone);
			if (!(weight > 0.0f)) {
				continue;
			}
			const f32 rate = _speed * layer.speed;
			const bool additive = layer.blend == AnimationBlendMode::Additive;

			BoneTransform motionPose = accumulated;
			BoneTransform motionReference = accumulated;
			f32 running = 0.0f;
			bool touched = false;
			for (size_t motionIndex = 0; motionIndex < slots->size(); ++motionIndex) {
				const AnimationLayer::MotionSlot& slot = (*slots)[motionIndex];
				const AnimationClip* asset = slot.motion.clip.Get();
				if (!asset) {
					continue;
				}
				const AnimationLayer::BoneBinding* row = AnimationLayer::FindRow(slot.bones, bone);
				if (!row) {
					continue;
				}
				const f32 share = slot.share;
				if (!(share > 0.0f)) {
					continue;
				}
				running += share;

				BoneTransform sampled = accumulated;
				BoneTransform reference = accumulated;
				const f32 at = SampleAt(*asset, clock * slot.motion.speed, rate, back * slot.motion.speed);
				const i32 channels[3] = {row->position, row->rotation, row->scale};
				const f32 referenceAt =
					additive ? AnimationImpl::WrapTime(asset->additiveReferenceTime, asset->length, AnimationLoopMode::None)
							 : 0.0f;
				for (i32 track : channels) {
					if (track < 0 || static_cast<size_t>(track) >= asset->tracks.size()) {
						continue;
					}
					const AnimationClip::Track& curve = asset->tracks[static_cast<size_t>(track)];
					PatchChannel(sampled, curve, AnimationImpl::SampleTrack(curve, at));
					if (additive) {
						PatchChannel(reference, curve, AnimationImpl::SampleTrack(curve, referenceAt));
					}
				}
				if (!touched) {
					motionPose = sampled;
					motionReference = reference;
				} else {
					const f32 step = share / running;
					motionPose = AnimationImpl::BlendBone(motionPose, sampled, step);
					if (additive) {
						motionReference = AnimationImpl::BlendBone(motionReference, reference, step);
					}
				}
				touched = true;
			}
			if (!touched) {
				continue;
			}
			accumulated = additive ? AnimationImpl::AddBone(accumulated, motionPose, motionReference, weight)
								   : AnimationImpl::BlendBone(accumulated, motionPose, weight);
		}
		return accumulated;
	}

	void Animator::CaptureTransition(const SkeletonAsset* skeleton, f32 deltaTime) {
		EndTransition();
		RebuildFoldSet();
		if (!skeleton || _foldBones.empty()) {
			return;
		}
		const f32 step = deltaTime > 0.0f && Math::IsFinite(deltaTime) ? deltaTime : CAPTURE_FALLBACK_STEP;

		Transition* transition = New<Transition>(MemTag::Scene);
		transition->duration = _pendingFade;
		for (i16 bone : _foldBones) {
			const BoneTransform seed = PoseSeed(*skeleton, bone);

			const BoneTransform source = FoldBone(seed, bone, true, 0.0f);
			const BoneTransform sourcePrev = FoldBone(seed, bone, true, step);
			const BoneTransform target = FoldBone(seed, bone, false, 0.0f);
			const BoneTransform targetPrev = FoldBone(seed, bone, false, step);

			AnimationImpl::InertialBone offset;
			offset.bone = bone;
			if (AnimationImpl::CaptureInertialBone(source, target, sourcePrev, targetPrev, step, _pendingFade, offset)) {
				transition->offsets.push_back(offset);
			}
		}
		if (transition->offsets.empty()) {
			Delete(transition);
			return;
		}
		_transition = transition;
	}

	void Animator::WriteProperties(AnimationLayer& layer) {
		if (layer._motions.empty()) {
			return;
		}
		const AnimationLayer::MotionSlot& slot = layer._motions[0];
		const AnimationClip* asset = slot.motion.clip.Get();
		if (!layer._active || !asset || (layer._properties.empty() && layer._blendShapes.empty())) {
			return;
		}
		const f32 at = SampleAt(*asset, layer._time * slot.motion.speed, _speed * layer.speed, 0.0f);

		for (const AnimationLayer::BlendShapeBinding& binding : layer._blendShapes) {
			if (binding.track < 0 || static_cast<size_t>(binding.track) >= asset->tracks.size()) {
				continue;
			}
			SkinnedMeshRenderer* renderer = binding.renderer.Get();
			if (!renderer) {
				continue;
			}
			const AnimationClip::Track& track = asset->tracks[static_cast<size_t>(binding.track)];
			renderer->SetBlendShapeWeight(static_cast<u32>(binding.shape), AnimationImpl::SampleTrack(track, at).x);
		}

		if (layer._properties.empty()) {
			return;
		}

		for (AnimationLayer::PropertyBinding& binding : layer._properties) {
			if (binding.track < 0 || static_cast<size_t>(binding.track) >= asset->tracks.size()) {
				continue;
			}
			const AnimationClip::Track& track = asset->tracks[static_cast<size_t>(binding.track)];
			const Vec4 value = AnimationImpl::SampleTrack(track, at);

			SceneObject* object = binding.object.Get();
			if (!object) {
				continue;
			}
			Component* component = binding.component.Get();
			if (binding.component.GetInstanceId() != 0 && !component) {
				continue;
			}
			const FieldInfo* field = binding.field.Resolve();
			if (!field) {
				continue;
			}
			void* instance = component ? static_cast<void*>(component) : static_cast<void*>(object);
			if (AnimationImpl::WriteAnimatedField(instance, field, value, track.component) && !component) {
				object->NotifyLocalTransformWritten();
			}
		}
	}

	u32 Animator::Subscribe(AnimationEventFn fn, void* user) {
		if (!fn) {
			return 0;
		}
		EventSubscriber entry;
		entry.token = _nextSubscriberToken++;
		entry.fn = fn;
		entry.user = user;
		_subscribers.push_back(entry);
		return entry.token;
	}

	bool Animator::Unsubscribe(u32 token) {
		if (token == 0) {
			return false;
		}
		auto found = std::find_if(_subscribers.begin(), _subscribers.end(),
								  [token](const EventSubscriber& entry) { return entry.token == token; });
		if (found == _subscribers.end()) {
			return false;
		}
		_subscribers.erase(found);
		return true;
	}

	void Animator::CollectSignals(AnimationLayer& layer, i32 layerIndex, bool closeEnd, Vector<PendingSignal>& out) {
		const i32 dominant = layer.DominantMotion();
		if (dominant < 0 || !layer._active) {
			return;
		}
		const AnimationLayer::MotionSlot& slot = layer._motions[static_cast<size_t>(dominant)];
		const AnimationClip* asset = slot.motion.clip.Get();
		if (!asset) {
			return;
		}
		const f32 rate = slot.motion.speed;
		AnimationImpl::ClipSegment runs[AnimationImpl::ANIMATION_MAX_CLIP_RUNS];
		const u32 count = IntervalRuns(layer._prevTime * rate, layer._time * rate, *asset, runs, layer._intervalWarned,
									   GetOwner().IsValid() ? GetOwner()->name : str("<detached>"));

		for (u32 index = 0; index < count; ++index) {
			const AnimationImpl::ClipSegment& run = runs[index];
			const bool forward = run.end > run.begin;
			const bool inclusive = closeEnd && index + 1 == count;
			const size_t first = out.size();

			for (const AnimationLayer::EventBinding& binding : slot.events) {
				if (binding.track < 0 || static_cast<size_t>(binding.track) >= asset->tracks.size()) {
					continue;
				}
				const AnimationClip::Track& track = asset->tracks[static_cast<size_t>(binding.track)];
				for (size_t key = 0; key < track.times.size(); ++key) {
					const f32 at = track.times[key];
					const bool inside = forward ? (at >= run.begin && (inclusive ? at <= run.end : at < run.end))
												: (at <= run.begin && (inclusive ? at >= run.end : at > run.end));
					if (!inside) {
						continue;
					}
					PendingSignal signal;
					signal.event.name = StringHash(track.name);
					signal.event.clip = slot.motion.clip;
					signal.event.layer = layerIndex;
					signal.event.time = at;
					signal.event.value = key < track.values.size() ? track.values[key] : Vec4{};
					signal.kind = track.kind;
					signal.object = binding.object;
					signal.component = binding.component;
					if (track.kind == AnimationTrackKind::Audio) {
						const Guid guid = key < track.assets.size() ? track.assets[key] : Guid{};
						signal.cue = Handle<Asset>(guid.IsValid() ? AssetDatabase::FindLoadedByGuid(guid) : nullptr);
					}
					out.push_back(signal);
				}
			}

			std::stable_sort(out.begin() + static_cast<ptrdiff_t>(first), out.end(),
							 [forward](const PendingSignal& a, const PendingSignal& b) {
								 return forward ? a.event.time < b.event.time : a.event.time > b.event.time;
							 });
		}
	}

	i16 Animator::RootMotionBone(const SkeletonAsset* skeleton) const {
		if (!skeleton || _layers.empty()) {
			return -1;
		}
		const AnimationLayer& base = *_layers[0];
		const i32 dominant = base.DominantMotion();
		const AnimationClip* asset = dominant >= 0 ? base._motions[static_cast<size_t>(dominant)].motion.clip.Get() : nullptr;
		if (!asset || !base._active || asset->bakeRootMotionIntoPose) {
			return -1;
		}
		for (size_t bone = 0; bone < skeleton->parents.size(); ++bone) {
			if (skeleton->parents[bone] < 0) {
				return static_cast<i16>(bone);
			}
		}
		return -1;
	}

	void Animator::AccumulateRootMotion(i16 bone) {
		BoneTransform folded;

		for (AnimationLayer* layerPtr : _layers) {
			AnimationLayer& layer = *layerPtr;
			if (!layer._active) {
				continue;
			}
			const f32 weight = layer.WeightForBone(bone);
			if (!(weight > 0.0f)) {
				continue;
			}

			BoneTransform travelled;
			f32 running = 0.0f;
			bool touched = false;
			for (size_t motionIndex = 0; motionIndex < layer._motions.size(); ++motionIndex) {
				const AnimationLayer::MotionSlot& slot = layer._motions[motionIndex];
				const AnimationClip* asset = slot.motion.clip.Get();
				if (!asset) {
					continue;
				}
				const AnimationLayer::BoneBinding* row = AnimationLayer::FindRow(slot.bones, bone);
				if (!row) {
					continue;
				}
				const f32 share = slot.share;
				if (!(share > 0.0f)) {
					continue;
				}
				running += share;

				AnimationImpl::ClipSegment runs[AnimationImpl::ANIMATION_MAX_CLIP_RUNS];
				const f32 rate = slot.motion.speed;
				const u32 count = IntervalRuns(layer._prevTime * rate, layer._time * rate, *asset, runs,
											   layer._intervalWarned,
											   GetOwner().IsValid() ? GetOwner()->name : str("<detached>"));
				BoneTransform own;
				for (u32 index = 0; index < count; ++index) {
					const BoneTransform begin = SampleChannels(*asset, row->position, row->rotation, runs[index].begin);
					const BoneTransform end = SampleChannels(*asset, row->position, row->rotation, runs[index].end);
					own = AnimationImpl::ComposeDelta(own, AnimationImpl::DeltaBetween(begin, end));
				}
				travelled = touched ? AnimationImpl::BlendBone(travelled, own, share / running) : own;
				touched = true;
			}
			if (!touched) {
				continue;
			}

			folded = layer.blend == AnimationBlendMode::Additive
						 ? AnimationImpl::ComposeDelta(folded, AnimationImpl::ScaleDelta(travelled, weight))
						 : AnimationImpl::BlendBone(folded, travelled, weight);
		}

		if (_retarget && _retarget->valid) {
			const RetargetProfile& source = *_retarget->source.Get();
			const RetargetProfile& target = *_retarget->target.Get();
			_rootDelta = AnimationImpl::ConjugateDelta(
				AnimationImpl::SolveRetargetRoot(_retarget->map, source, target, _rootAnchor),
				AnimationImpl::SolveRetargetRootDelta(_retarget->map, source, target, folded));
			return;
		}

		_rootDelta = AnimationImpl::ConjugateDelta(_rootAnchor, folded);
	}

	const AnimationLayer* PlayHandle::Resolve() const {
		const Animator* animator = _animator.Get();
		if (!animator || _id == 0) {
			return nullptr;
		}
		const AnimationLayer* layer = animator->GetLayer(_layer);
		return layer && layer->_playId == _id ? layer : nullptr;
	}

	AnimationLayer* PlayHandle::ResolveMutable() const {
		Animator* animator = _animator.Get();
		if (!animator || _id == 0) {
			return nullptr;
		}
		AnimationLayer* layer = animator->GetLayer(_layer);
		return layer && layer->_playId == _id ? layer : nullptr;
	}

	bool PlayHandle::IsValid() const {
		const AnimationLayer* layer = Resolve();
		return layer != nullptr && layer->IsPlaying();
	}

	Handle<AnimationClip> PlayHandle::GetClip() const {
		const AnimationLayer* layer = Resolve();
		return layer ? layer->GetClip() : Handle<AnimationClip>();
	}

	f32 PlayHandle::GetTime() const {
		const AnimationLayer* layer = Resolve();
		return layer ? layer->GetTime() : 0.0f;
	}

	void PlayHandle::SetTime(f32 seconds) {
		if (AnimationLayer* layer = ResolveMutable()) {
			layer->SetTime(seconds);
		}
	}

	f32 PlayHandle::GetSpeed() const {
		const AnimationLayer* layer = Resolve();
		return layer ? layer->speed : 0.0f;
	}

	void PlayHandle::SetSpeed(f32 value) {
		if (AnimationLayer* layer = ResolveMutable()) {
			layer->speed = value;
		}
	}

	void PlayHandle::Stop() {
		if (AnimationLayer* layer = ResolveMutable()) {
			layer->Stop();
		}
	}

} // namespace Shalltear
