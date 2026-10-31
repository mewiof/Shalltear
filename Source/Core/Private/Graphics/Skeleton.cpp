#include "Shalltear/Core/Graphics/Skeleton.h"

#include "Shalltear/Core/Scene/SceneObject.h"

namespace Shalltear {

	Skeleton::~Skeleton() = default;

	void Skeleton::OnEnable() {
		RebindPose();
	}

	void Skeleton::RebindPose() {
		SkeletonAsset* asset = _skeleton.Get();
		const u32 boneCount = asset ? asset->GetBoneCount() : 0;

		_localPose.assign(boneCount, BoneTransform{});
		_modelPose.assign(boneCount, Mat4::Identity());
		if (asset) {
			for (u32 i = 0; i < boneCount && i < asset->restPose.size(); ++i) {
				_localPose[i] = asset->restPose[i];
			}
		}

		for (size_t k = 0; k < _exposedBones.size() && k < _exposedObjects.size(); ++k) {
			const i16 bone = _exposedBones[k];
			SceneObject* object = _exposedObjects[k].Get();
			if (!object || bone < 0 || static_cast<u32>(bone) >= boneCount) {
				continue;
			}
			BoneTransform& pose = _localPose[static_cast<size_t>(bone)];
			pose.position = object->GetLocalPosition();
			pose.rotation = object->GetLocalRotation();
			pose.scale = object->GetLocalScale();
		}

		_boundSkeleton = _skeleton.GetInstanceId();
		++_poseVersion;
	}

	void Skeleton::EnsureBound() {
		if (_boundSkeleton != _skeleton.GetInstanceId()) {
			RebindPose();
		}
	}

	void Skeleton::SetBoneLocalPose(u32 bone, const BoneTransform& transform) {
		EnsureBound();
		if (bone >= _localPose.size()) {
			return;
		}
		_localPose[bone] = transform;
		++_poseVersion;
	}

	void Skeleton::UpdatePose() {
		EnsureBound();
		if (_composedVersion == _poseVersion) {
			return;
		}
		const SkeletonAsset* asset = _skeleton.Get();
		if (!asset) {
			_composedVersion = _poseVersion;
			return;
		}

		const size_t boneCount = _modelPose.size();
		for (size_t b = 0; b < boneCount; ++b) {
			const Mat4 local = _localPose[b].ToMatrix();
			const i16 parent = b < asset->parents.size() ? asset->parents[b] : static_cast<i16>(-1);
			_modelPose[b] = (parent >= 0 && static_cast<size_t>(parent) < b) ? _modelPose[static_cast<size_t>(parent)] * local
																			 : local;
		}

		for (size_t k = 0; k < _exposedBones.size() && k < _exposedObjects.size(); ++k) {
			const i16 bone = _exposedBones[k];
			SceneObject* object = _exposedObjects[k].Get();
			if (!object || bone < 0 || static_cast<size_t>(bone) >= boneCount) {
				continue;
			}
			const BoneTransform& pose = _localPose[static_cast<size_t>(bone)];
			object->SetLocalPosition(pose.position);
			object->SetLocalRotation(pose.rotation);
			object->SetLocalScale(pose.scale);
		}

		_composedVersion = _poseVersion;
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::Skeleton)
