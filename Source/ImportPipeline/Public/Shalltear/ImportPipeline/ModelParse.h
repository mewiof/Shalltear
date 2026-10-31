#pragma once

#include "Shalltear/ImportPipeline/ImportPipelineAPI.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct ModelImage {
		str name;
		str externalPath;
		Vector<u8> embeddedBytes{GetMemoryResource(MemTag::Asset)};
		str extension;
		bool IsExternal() const { return !externalPath.empty(); }
	};

	// maps are indices into images
	struct ModelMaterial {
		str name;
		Color baseColorFactor = Color::White();
		i32 baseColorImage = -1;
		f32 metallicFactor = 1.0f;
		f32 roughnessFactor = 1.0f;

		// roughness in G, metallic in B
		i32 metallicRoughnessImage = -1;
		i32 normalImage = -1;
		i32 occlusionImage = -1;
		i32 emissiveImage = -1;

		// linear
		Vec3 emissiveFactor = Vec3(0.0f, 0.0f, 0.0f);

		f32 normalScale = 1.0f;
		f32 occlusionStrength = 1.0f;
	};

	using ModelSkinInfluence = MeshSkinInfluence;

	struct ModelSkin {
		Vector<i32> joints{GetMemoryResource(MemTag::Asset)};

		// mesh local -> joint local at bind
		Vector<Mat4> inverseBind{GetMemoryResource(MemTag::Asset)};
	};

	struct ModelBlendShape {
		// percent, 100 = full influence
		f32 targetWeight = 100.0f;

		Vector<u32> offsetVertices{GetMemoryResource(MemTag::Asset)};
		Vector<Vec3> positionOffsets{GetMemoryResource(MemTag::Asset)};
		Vector<Vec3> normalOffsets{GetMemoryResource(MemTag::Asset)};
	};

	struct ModelBlendChannel {
		str name;

		Vector<ModelBlendShape> shapes{GetMemoryResource(MemTag::Asset)};
	};

	struct ModelMesh {
		str name;

		Vector<MeshVertex> vertices{GetMemoryResource(MemTag::Asset)};
		Vector<u32> indices{GetMemoryResource(MemTag::Asset)};
		Vector<SubMesh> submeshes{GetMemoryResource(MemTag::Asset)};
		AABB bounds = AABB::Empty();

		Vector<i32> materials{GetMemoryResource(MemTag::Asset)};

		Vector<ModelSkinInfluence> influences{GetMemoryResource(MemTag::Asset)};

		Vector<ModelBlendChannel> blendChannels{GetMemoryResource(MemTag::Asset)};
	};

	struct ModelNode {
		str name;
		i32 parent = -1;

		Vec3 position{};
		Quat rotation = Quat::Identity();
		Vec3 scale{1.0f, 1.0f, 1.0f};
		bool shearedMatrix = false;
		Mat4 localMatrix = Mat4::Identity();

		i32 mesh = -1;

		i32 skin = -1;
	};

	enum class ModelChannel : u8 {
		Translation,
		Rotation,
		Scale,

		// percent 0-100 in values x
		MorphWeight,
	};

	enum class ModelInterpolation : u8 {
		Step,
		Linear,
		CubicSpline,
	};

	inline constexpr u32 ModelValuesPerKey(ModelInterpolation interp) {
		return interp == ModelInterpolation::CubicSpline ? 3u : 1u;
	}

	struct ModelTrack {
		i32 node = -1;
		ModelChannel channel = ModelChannel::Translation;
		ModelInterpolation interp = ModelInterpolation::Linear;

		i32 blendChannel = -1;

		// ascending, file timeline, never rebased
		Vector<f32> times{GetMemoryResource(MemTag::Asset)};
		Vector<Vec4> values{GetMemoryResource(MemTag::Asset)};
	};

	struct ModelTakeRange {
		str name;
		f32 begin = 0.0f, end = 0.0f; // file timeline
		Vector<i32> tracks{GetMemoryResource(MemTag::Asset)};
	};

	struct ModelParseOptions {
		bool tracks = false;
	};

	struct ModelParseResult {
		bool success = false;
		str error;

		Vector<ModelNode> nodes{GetMemoryResource(MemTag::Asset)};
		Vector<ModelMesh> meshes{GetMemoryResource(MemTag::Asset)};

		Vector<ModelMaterial> materials{GetMemoryResource(MemTag::Asset)};
		Vector<ModelImage> images{GetMemoryResource(MemTag::Asset)};

		Vector<ModelSkin> skins{GetMemoryResource(MemTag::Asset)};

		Vector<ModelTrack> tracks{GetMemoryResource(MemTag::Asset)};
		Vector<ModelTakeRange> takes{GetMemoryResource(MemTag::Asset)};
	};

} // namespace Shalltear
