#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	class SHALLTEAR_CORE_API Skeleton : public Component {
	public:
		SLT_REFLECT_COMPONENT(Skeleton, Component)
		SLT_REFLECT_FIELD(_skeleton)
		SLT_REFLECT_ARRAY(_exposedBones, SLT_HIDDEN)
		SLT_REFLECT_ARRAY(_exposedObjects, SLT_HIDDEN)
		SLT_END_REFLECT()

		Skeleton() = default;
		~Skeleton() override;

		Handle<SkeletonAsset> GetSkeleton() const { return _skeleton; }
		void SetSkeleton(Handle<SkeletonAsset> skeleton) { _skeleton = skeleton; }

		u32 GetExposedBoneCount() const { return static_cast<u32>(_exposedBones.size()); }

		i16 GetExposedBone(u32 index) const { return index < _exposedBones.size() ? _exposedBones[index] : i16{-1}; }
		Handle<SceneObject> GetExposedObject(u32 index) const {
			return index < _exposedObjects.size() ? _exposedObjects[index] : Handle<SceneObject>{};
		}

		void AddExposedBone(i16 bone, Handle<SceneObject> object) {
			_exposedBones.push_back(bone);
			_exposedObjects.push_back(object);
		}

		void SetExposedBoneCount(u32 count) {
			_exposedBones.resize(count);
			_exposedObjects.resize(count);
		}

		std::span<const BoneTransform> GetLocalPose() const { return {_localPose.data(), _localPose.size()}; }

		std::span<const Mat4> GetModelPose() const { return {_modelPose.data(), _modelPose.size()}; }

		// only sanctioned write
		void SetBoneLocalPose(u32 bone, const BoneTransform& transform);

		void UpdatePose();
	protected:
		void OnEnable() override;
	private:
		void RebindPose();

		// lazy (OnEnable fires before the field blob is read)
		void EnsureBound();

		Handle<SkeletonAsset> _skeleton;

		Vector<i16> _exposedBones{GetMemoryResource(MemTag::Scene)};
		Vector<Handle<SceneObject>> _exposedObjects{GetMemoryResource(MemTag::Scene)};

		Vector<BoneTransform> _localPose{GetMemoryResource(MemTag::Scene)};
		Vector<Mat4> _modelPose{GetMemoryResource(MemTag::Scene)};
		u64 _boundSkeleton = 0;
		u32 _poseVersion = 1;
		u32 _composedVersion = 0;
	};

} // namespace Shalltear
