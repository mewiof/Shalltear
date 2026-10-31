#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Graphics/SkinAsset.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	class SHALLTEAR_CORE_API SkinnedMeshRenderer : public Component {
	public:
		SLT_REFLECT_COMPONENT(SkinnedMeshRenderer, Component)
		SLT_REFLECT_FIELD(_mesh)
		SLT_REFLECT_ARRAY(_materials, SLT_HIDDEN)
		SLT_REFLECT_FIELD(_skin)
		SLT_REFLECT_FIELD(_skeleton)
		SLT_REFLECT_ARRAY(_blendShapeWeights, SLT_HIDDEN)
		SLT_REFLECT_FIELD(_renderingLayerMask)
		SLT_END_REFLECT()

		SkinnedMeshRenderer() = default;
		~SkinnedMeshRenderer() override;

		Handle<MeshAsset> GetMesh() const { return _mesh; }
		void SetMesh(Handle<MeshAsset> mesh) { _mesh = mesh; }

		Handle<SkinAsset> GetSkin() const { return _skin; }
		void SetSkin(Handle<SkinAsset> skin) { _skin = skin; }

		Handle<Skeleton> GetSkeleton() const { return _skeleton; }
		void SetSkeleton(Handle<Skeleton> skeleton) { _skeleton = skeleton; }

		u32 GetMaterialCount() const { return static_cast<u32>(_materials.size()); }

		Handle<MaterialAsset> GetMaterial(u32 slot) const {
			return slot < _materials.size() ? _materials[slot] : Handle<MaterialAsset>{};
		}
		void SetMaterial(u32 slot, Handle<MaterialAsset> material) {
			if (slot >= _materials.size()) {
				_materials.resize(slot + 1);
			}
			_materials[slot] = material;
		}

		u32 GetBlendShapeWeightCount() const { return static_cast<u32>(_blendShapeWeights.size()); }

		f32 GetBlendShapeWeight(u32 index) const {
			return index < _blendShapeWeights.size() ? _blendShapeWeights[index] : 0.0f;
		}
		void SetBlendShapeWeight(u32 index, f32 weight) {
			if (index >= _blendShapeWeights.size()) {
				_blendShapeWeights.resize(index + 1, 0.0f);
			}
			_blendShapeWeights[index] = weight;
		}
		void SetBlendShapeWeightCount(u32 count) { _blendShapeWeights.resize(count, 0.0f); }

		u32 GetRenderingLayerMask() const { return _renderingLayerMask; }
		void SetRenderingLayerMask(u32 mask) { _renderingLayerMask = mask; }

		std::span<const f32> GetBlendShapeWeights() const {
			return {_blendShapeWeights.data(), _blendShapeWeights.size()};
		}
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		Handle<MeshAsset> _mesh;

		Vector<Handle<MaterialAsset>> _materials{GetMemoryResource(MemTag::Scene)};

		Handle<SkinAsset> _skin;
		Handle<Skeleton> _skeleton;

		// 0-100 scale
		Vector<f32> _blendShapeWeights{GetMemoryResource(MemTag::Scene)};

		u32 _renderingLayerMask = 1;
	};

} // namespace Shalltear
