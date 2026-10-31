#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Vector.h"

#include <string_view>

namespace Shalltear {

	struct MeshVertex {
		Vec3 position;
		f32 u = 0.0f;
		Vec3 normal;
		f32 v = 0.0f;
		Vec3 tangent;
		f32 tangentSign = 1.0f;
		Color32 color = Color32::White();
	};
	static_assert(sizeof(MeshVertex) == 52, "MeshVertex must match the shader-side 52-byte stride");

	struct MeshSkinInfluence {
		u16 joints[4] = {};
		f32 weights[4] = {};
	};
	static_assert(sizeof(MeshSkinInfluence) == 24, "MeshSkinInfluence must have no padding (the weld compares it byte-wise)");

	struct MeshBlendShapeFrame {
		// 0-100 scale, never clamped
		f32 weight = 100.0f;

		Vector<u32> vertices{GetMemoryResource(MemTag::Asset)};
		Vector<Vec3> positionDeltas{GetMemoryResource(MemTag::Asset)};
		Vector<Vec3> normalDeltas{GetMemoryResource(MemTag::Asset)};
	};

	struct MeshBlendShape {
		str name;
		Vector<MeshBlendShapeFrame> frames{GetMemoryResource(MemTag::Asset)};
	};

	struct SubMesh {
		u32 firstIndex = 0;
		u32 indexCount = 0;
		u32 materialSlot = 0;
		AABB bounds = AABB::Empty();
	};

	class SHALLTEAR_CORE_API MeshAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(MeshAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		MeshAsset() = default;
		~MeshAsset() override;

		Vector<MeshVertex> vertices{GetMemoryResource(MemTag::Asset)};
		Vector<u32> indices{GetMemoryResource(MemTag::Asset)};
		Vector<SubMesh> submeshes{GetMemoryResource(MemTag::Asset)};
		AABB bounds;

		Vector<MeshSkinInfluence> skinInfluences{GetMemoryResource(MemTag::Asset)};

		Vector<MeshBlendShape> blendShapes{GetMemoryResource(MemTag::Asset)};

		[[nodiscard]] i32 FindBlendShape(std::string_view name) const;

		Vector<Guid> defaultMaterials{GetMemoryResource(MemTag::Asset)};

		// valid only while loaded + rendering available
		RHI::BufferHandle vertexBuffer;
		RHI::BufferHandle indexBuffer;
		u64 vertexBufferAddress = 0;
		// frame scoped
		u64 indexBufferAddress = 0;

		StringHash GetAssetTypeId() const override { return MeshAsset::GetReflectType()->name; }

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;

		bool CookImportState(CookWriter& importState) const override;
		bool LoadImportState(CookReader& importState) override;
	protected:
		void OnLoad() override;
		void OnUnload() override;
	};

} // namespace Shalltear
