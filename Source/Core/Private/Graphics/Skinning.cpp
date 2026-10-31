#include "Skinning.h"

#include <cstring>

namespace Shalltear::GraphicsInternal {

	void BuildSkinMatrices(const SkinAsset& skin, std::span<const Mat4> modelPose, Mat4* out, u32 outCount) {
		const u32 jointCount = skin.GetJointCount();
		for (u32 j = 0; j < jointCount && j < outCount; ++j) {
			const i16 bone = skin.jointToBone[j];
			if (bone < 0 || static_cast<size_t>(bone) >= modelPose.size() || j >= skin.inverseBind.size()) {
				out[j] = Mat4::Identity();
				continue;
			}
			out[j] = modelPose[static_cast<size_t>(bone)] * skin.inverseBind[j];
		}
	}

	void SkinVertices(std::span<const MeshVertex> vertices,
					  std::span<const MeshSkinInfluence> influences,
					  std::span<const Mat4> skinMatrices,
					  MeshVertex* out) {
		const size_t count = vertices.size();
		for (size_t v = 0; v < count; ++v) {
			MeshVertex vertex = vertices[v];
			if (v >= influences.size()) {
				out[v] = vertex;
				continue;
			}
			const MeshSkinInfluence& influence = influences[v];

			Mat4 blended;
			f32 total = 0.0f;
			for (u32 k = 0; k < 4; ++k) {
				const f32 weight = influence.weights[k];
				if (weight == 0.0f) {
					continue;
				}
				const u32 joint = influence.joints[k];
				if (joint >= skinMatrices.size()) {
					continue;
				}
				const Mat4& m = skinMatrices[joint];
				for (u32 e = 0; e < 16; ++e) {
					blended.data[e] += m.data[e] * weight;
				}
				total += weight;
			}
			if (total <= 0.0f) {
				out[v] = vertex;
				continue;
			}

			if (total != 1.0f) {
				const f32 rescale = 1.0f / total;
				for (u32 e = 0; e < 16; ++e) {
					blended.data[e] *= rescale;
				}
			}

			const Vec3 position = vertex.position;
			const Vec3 normal = vertex.normal;
			const Vec3 tangent = vertex.tangent;
			vertex.position = blended.TransformPoint(position);
			vertex.normal = blended.TransformVector(normal).Normalized();
			vertex.tangent = blended.TransformVector(tangent);
			out[v] = vertex;
		}
	}

	namespace {

		struct FrameBlend {
			i32 first = -1, second = -1;
			f32 firstScale = 0.0f, secondScale = 0.0f;
		};

		FrameBlend BracketFrames(const MeshBlendShape& shape, f32 weight) {
			FrameBlend blend;
			const size_t count = shape.frames.size();
			if (count == 0) {
				return blend;
			}

			size_t restSlot = 0;
			while (restSlot < count && shape.frames[restSlot].weight < 0.0f) {
				++restSlot;
			}
			const bool hasRest = !(restSlot < count && shape.frames[restSlot].weight == 0.0f);
			const size_t total = count + (hasRest ? 1 : 0);

			const auto slotFrame = [&](size_t i) -> i32 {
				if (hasRest && i == restSlot) {
					return -1;
				}
				return static_cast<i32>(hasRest && i > restSlot ? i - 1 : i);
			};
			const auto slotWeight = [&](size_t i) -> f32 {
				const i32 frame = slotFrame(i);
				return frame < 0 ? 0.0f : shape.frames[static_cast<size_t>(frame)].weight;
			};

			if (total < 2) {
				blend.first = slotFrame(0);
				blend.firstScale = 1.0f;
				return blend;
			}

			size_t high = 1;
			while (high < total - 1 && slotWeight(high) <= weight) {
				++high;
			}
			const size_t low = high - 1;

			const f32 span = slotWeight(high) - slotWeight(low);
			const f32 t = span != 0.0f ? (weight - slotWeight(low)) / span : 1.0f;
			blend.first = slotFrame(low);
			blend.second = slotFrame(high);
			blend.firstScale = 1.0f - t;
			blend.secondScale = t;
			return blend;
		}

		void AddFrame(const MeshBlendShapeFrame& frame, f32 scale, size_t vertexCount, MeshVertex* out) {
			if (scale == 0.0f) {
				return;
			}
			const size_t deltas = frame.vertices.size();
			const bool normals = frame.normalDeltas.size() == deltas;
			for (size_t d = 0; d < deltas; ++d) {
				const u32 vertex = frame.vertices[d];
				if (vertex >= vertexCount) {
					continue;
				}
				out[vertex].position += frame.positionDeltas[d] * scale;
				if (normals) {
					out[vertex].normal += frame.normalDeltas[d] * scale;
				}
			}
		}

	} // namespace

	bool HasActiveBlendShapes(const MeshAsset& mesh, std::span<const f32> weights) {
		const size_t count = mesh.blendShapes.size() < weights.size() ? mesh.blendShapes.size() : weights.size();
		for (size_t s = 0; s < count; ++s) {
			if (weights[s] != 0.0f && !mesh.blendShapes[s].frames.empty()) {
				return true;
			}
		}
		return false;
	}

	void ApplyBlendShapes(const MeshAsset& mesh, std::span<const f32> weights, MeshVertex* out) {
		const size_t vertexCount = mesh.vertices.size();
		if (vertexCount == 0) {
			return;
		}
		std::memcpy(out, mesh.vertices.data(), vertexCount * sizeof(MeshVertex));

		const size_t count = mesh.blendShapes.size() < weights.size() ? mesh.blendShapes.size() : weights.size();
		bool movedNormals = false;
		for (size_t s = 0; s < count; ++s) {
			const f32 weight = weights[s];
			if (weight == 0.0f) {
				continue;
			}
			const MeshBlendShape& shape = mesh.blendShapes[s];
			const FrameBlend blend = BracketFrames(shape, weight);
			if (blend.first >= 0) {
				AddFrame(shape.frames[static_cast<size_t>(blend.first)], blend.firstScale, vertexCount, out);
				movedNormals = movedNormals || !shape.frames[static_cast<size_t>(blend.first)].normalDeltas.empty();
			}
			if (blend.second >= 0) {
				AddFrame(shape.frames[static_cast<size_t>(blend.second)], blend.secondScale, vertexCount, out);
				movedNormals = movedNormals || !shape.frames[static_cast<size_t>(blend.second)].normalDeltas.empty();
			}
		}

		if (movedNormals) {
			for (size_t v = 0; v < vertexCount; ++v) {
				out[v].normal = out[v].normal.Normalized();
			}
		}
	}

} // namespace Shalltear::GraphicsInternal
