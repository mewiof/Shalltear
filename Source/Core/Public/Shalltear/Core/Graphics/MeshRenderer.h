#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/ShadowCasting.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Scene/Component.h"

namespace Shalltear {

	class SHALLTEAR_CORE_API MeshRenderer : public Component {
	public:
		SLT_REFLECT_COMPONENT(MeshRenderer, Component)
		SLT_REFLECT_FIELD(_mesh)
		SLT_REFLECT_ARRAY(_materials, SLT_HIDDEN)
		SLT_REFLECT_FIELD(_castShadows)
		SLT_REFLECT_FIELD(_receiveShadows)
		SLT_REFLECT_FIELD(_renderingLayerMask)
		SLT_END_REFLECT()

		MeshRenderer() = default;
		~MeshRenderer() override;

		Handle<MeshAsset> GetMesh() const { return _mesh; }
		void SetMesh(Handle<MeshAsset> mesh) { _mesh = mesh; }

		// bound slots (not the mesh's submesh count)
		u32 GetMaterialCount() const { return static_cast<u32>(_materials.size()); }

		Handle<MaterialAsset> GetMaterial(u32 slot) const {
			return slot < _materials.size() ? _materials[slot] : Handle<MaterialAsset>{};
		}
		void SetMaterial(u32 slot, Handle<MaterialAsset> material) {
			if (slot >= _materials.size()) {
				_materials.resize(slot + 1);
			}
			_materials[slot] = material;
			_instances.Release(slot);
		}

		// renderer's copy, made on 1st call (freed with renderer/on SetMaterial)
		Handle<MaterialAsset> GetMaterialInstance(u32 slot) { return _instances.Acquire(slot, GetMaterial(slot)); }

		Handle<MaterialAsset> GetRenderedMaterial(u32 slot) const {
			const Handle<MaterialAsset> instance = _instances.Find(slot);
			return instance.IsValid() ? instance : GetMaterial(slot);
		}

		ShadowCasting GetCastShadows() const { return _castShadows; }
		void SetCastShadows(ShadowCasting casting) { _castShadows = casting; }

		bool GetReceiveShadows() const { return _receiveShadows; }
		void SetReceiveShadows(bool receive) { _receiveShadows = receive; }

		u32 GetRenderingLayerMask() const { return _renderingLayerMask; }
		void SetRenderingLayerMask(u32 mask) { _renderingLayerMask = mask; }
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		Handle<MeshAsset> _mesh;

		Vector<Handle<MaterialAsset>> _materials{GetMemoryResource(MemTag::Scene)};
		MaterialInstanceSlots _instances;

		u32 _renderingLayerMask = 1;

		ShadowCasting _castShadows = ShadowCasting::On;
		bool _receiveShadows = true;
	};

} // namespace Shalltear
