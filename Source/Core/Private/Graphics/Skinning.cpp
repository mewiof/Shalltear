#include "Skinning.h"

#include "Shalltear/Core/Graphics/RHICommandList.h"
#include "Shalltear/Core/Graphics/RHIDevice.h"

#include <algorithm>
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

		void AddReach(AABB& reach, const AABB& delta, f32 scale) {
			if (scale == 0.0f) {
				return;
			}
			const Vec3 low = delta.min * scale;
			const Vec3 high = delta.max * scale;
			reach.min += Vec3::Min(low, high);
			reach.max += Vec3::Max(low, high);
		}

		size_t FrameDeltaCount(const MeshBlendShapeFrame& frame) {
			return frame.vertices.size() < frame.positionDeltas.size() ? frame.vertices.size() : frame.positionDeltas.size();
		}

		void BuildBounds(const MeshAsset& mesh, SkinBounds& bounds) {
			const size_t vertexCount = mesh.vertices.size();
			for (size_t v = 0; v < vertexCount; ++v) {
				const Vec3& position = mesh.vertices[v].position;
				bool jointed = false;
				if (v < mesh.skinInfluences.size()) {
					const MeshSkinInfluence& influence = mesh.skinInfluences[v];
					for (u32 k = 0; k < 4; ++k) {
						const f32 weight = influence.weights[k];
						if (weight == 0.0f) {
							continue;
						}
						if (!(weight > 0.0f)) {
							bounds.convex = false;
							continue;
						}
						const u16 joint = influence.joints[k];
						if (joint >= bounds.joints.size()) {
							bounds.joints.resize(static_cast<size_t>(joint) + 1, AABB::Empty());
						}
						bounds.joints[joint].Encapsulate(position);
						jointed = true;
					}
				}
				if (!jointed) {
					bounds.unjointed.Encapsulate(position);
				}
			}

			for (const MeshBlendShape& shape : mesh.blendShapes) {
				for (const MeshBlendShapeFrame& frame : shape.frames) {
					AABB& delta = bounds.frameDeltas.emplace_back();
					const size_t deltas = FrameDeltaCount(frame);
					for (size_t d = 0; d < deltas; ++d) {
						if (frame.vertices[d] < vertexCount) {
							delta.Encapsulate(frame.positionDeltas[d]);
						}
					}
				}
			}
		}

		RHI::BufferHandle UploadStream(RHI::Device& device, std::span<const u8> bytes, const char* name, u64& outAddress) {
			RHI::BufferDesc desc;
			desc.size = bytes.size();
			desc.usage = RHI::BufferUsage::Storage | RHI::BufferUsage::DeviceAddress | RHI::BufferUsage::TransferDst;
			desc.debugName = name;
			RHI::BufferHandle buffer = device.CreateBufferResource(desc);
			if (buffer.IsValid()) {
				device.UploadBuffer(buffer, bytes);
				outAddress = device.GetBufferDeviceAddress(buffer);
			}
			return buffer;
		}

		template <typename T>
		std::span<const u8> AsBytes(const Vector<T>& values) {
			return {reinterpret_cast<const u8*>(values.data()), values.size() * sizeof(T)};
		}

		void UploadMorphStreams(const MeshAsset& mesh, MeshDeform& deform, RHI::Device& device) {
			const size_t vertexCount = mesh.vertices.size();
			Vector<u32> ranges{GetMemoryResource(MemTag::Graphics)};
			ranges.assign(vertexCount * 2, 0u);
			for (const MeshBlendShape& shape : mesh.blendShapes) {
				for (const MeshBlendShapeFrame& frame : shape.frames) {
					const size_t deltas = FrameDeltaCount(frame);
					for (size_t d = 0; d < deltas; ++d) {
						if (frame.vertices[d] < vertexCount) {
							++ranges[static_cast<size_t>(frame.vertices[d]) * 2 + 1];
						}
					}
				}
			}
			u32 total = 0;
			for (size_t v = 0; v < vertexCount; ++v) {
				ranges[v * 2] = total;
				total += ranges[v * 2 + 1];
			}
			if (total == 0) {
				return;
			}

			Vector<GPUMorphDelta> deltas{GetMemoryResource(MemTag::Graphics)};
			deltas.resize(total);
			Vector<u32> next{GetMemoryResource(MemTag::Graphics)};
			next.resize(vertexCount);
			for (size_t v = 0; v < vertexCount; ++v) {
				next[v] = ranges[v * 2];
			}
			u32 frameIndex = 0;
			for (const MeshBlendShape& shape : mesh.blendShapes) {
				for (const MeshBlendShapeFrame& frame : shape.frames) {
					const size_t count = FrameDeltaCount(frame);
					const bool normals = frame.normalDeltas.size() == frame.vertices.size();
					for (size_t d = 0; d < count; ++d) {
						const u32 vertex = frame.vertices[d];
						if (vertex >= vertexCount) {
							continue;
						}
						GPUMorphDelta& delta = deltas[next[vertex]++];
						delta.position = frame.positionDeltas[d];
						delta.frame = frameIndex;
						delta.normal = normals ? frame.normalDeltas[d] : Vec3{};
					}
					++frameIndex;
				}
			}

			deform.morphRanges = UploadStream(device, AsBytes(ranges), mesh.virtualPath.c_str(), deform.morphRangesAddress);
			deform.morphDeltas = UploadStream(device, AsBytes(deltas), mesh.virtualPath.c_str(), deform.morphDeltasAddress);
			if (!deform.morphRanges.IsValid() || !deform.morphDeltas.IsValid()) {
				deform.morphRangesAddress = 0;
				deform.morphDeltasAddress = 0;
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

	void BlendShapeFrameScales(const MeshAsset& mesh, std::span<const f32> weights, std::span<f32> scales, bool& outMovesNormals) {
		std::fill(scales.begin(), scales.end(), 0.0f);
		outMovesNormals = false;
		const size_t count = mesh.blendShapes.size() < weights.size() ? mesh.blendShapes.size() : weights.size();
		size_t firstFrame = 0;
		for (size_t s = 0; s < count; ++s) {
			const MeshBlendShape& shape = mesh.blendShapes[s];
			const size_t frames = firstFrame;
			firstFrame += shape.frames.size();
			if (weights[s] == 0.0f || firstFrame > scales.size()) {
				continue;
			}
			const FrameBlend blend = BracketFrames(shape, weights[s]);
			if (blend.first >= 0) {
				scales[frames + static_cast<size_t>(blend.first)] += blend.firstScale;
				outMovesNormals = outMovesNormals || !shape.frames[static_cast<size_t>(blend.first)].normalDeltas.empty();
			}
			if (blend.second >= 0) {
				scales[frames + static_cast<size_t>(blend.second)] += blend.secondScale;
				outMovesNormals = outMovesNormals || !shape.frames[static_cast<size_t>(blend.second)].normalDeltas.empty();
			}
		}
	}

	void MeshDeform::Build(MeshAsset& mesh, RHI::Device* device) {
		Release(mesh, device);
		if (mesh.skinInfluences.empty() && mesh.blendShapes.empty()) {
			return;
		}
		MeshDeform* deform = New<MeshDeform>(MemTag::Asset);
		BuildBounds(mesh, deform->bounds);
		for (const MeshBlendShape& shape : mesh.blendShapes) {
			deform->frameCount += static_cast<u32>(shape.frames.size());
		}
		mesh._deform = deform;
		if (device) {
			Restore(mesh, *device);
		}
	}

	void MeshDeform::Restore(MeshAsset& mesh, RHI::Device& device) {
		MeshDeform* deform = mesh._deform;
		if (!deform || mesh.vertices.empty()) {
			return;
		}
		for (RHI::BufferHandle* buffer : {&deform->influences, &deform->morphRanges, &deform->morphDeltas}) {
			if (buffer->IsValid()) {
				device.DestroyResource(*buffer);
				*buffer = {};
			}
		}
		deform->influencesAddress = 0;
		deform->morphRangesAddress = 0;
		deform->morphDeltasAddress = 0;

		if (!mesh.skinInfluences.empty()) {
			deform->influences = UploadStream(device, AsBytes(mesh.skinInfluences), mesh.virtualPath.c_str(), deform->influencesAddress);
		}
		if (deform->frameCount > 0) {
			UploadMorphStreams(mesh, *deform, device);
		}
	}

	void MeshDeform::Release(MeshAsset& mesh, RHI::Device* device) {
		MeshDeform* deform = mesh._deform;
		if (!deform) {
			return;
		}
		if (device) {
			for (RHI::BufferHandle buffer : {deform->influences, deform->morphRanges, deform->morphDeltas}) {
				if (buffer.IsValid()) {
					device->DestroyResource(buffer);
				}
			}
		}
		Delete(deform);
		mesh._deform = nullptr;
	}

	AABB SkinBounds::Pose(const MeshAsset& mesh, std::span<const Mat4> skinMatrices, std::span<const f32> weights) {
		const MeshDeform* deform = MeshDeform::Of(mesh);
		if (!deform || !deform->bounds.convex) {
			return AABB::Empty();
		}
		const SkinBounds& bounds = deform->bounds;

		AABB reach;
		size_t firstFrame = 0;
		const size_t count = mesh.blendShapes.size() < weights.size() ? mesh.blendShapes.size() : weights.size();
		for (size_t s = 0; s < count; ++s) {
			const MeshBlendShape& shape = mesh.blendShapes[s];
			const size_t frames = firstFrame;
			firstFrame += shape.frames.size();
			if (weights[s] == 0.0f || shape.frames.empty()) {
				continue;
			}
			if (firstFrame > bounds.frameDeltas.size()) {
				return AABB::Empty();
			}
			const FrameBlend blend = BracketFrames(shape, weights[s]);
			if (blend.first >= 0) {
				AddReach(reach, bounds.frameDeltas[frames + static_cast<size_t>(blend.first)], blend.firstScale);
			}
			if (blend.second >= 0) {
				AddReach(reach, bounds.frameDeltas[frames + static_cast<size_t>(blend.second)], blend.secondScale);
			}
		}
		if (!reach.IsValid()) {
			return AABB::Empty();
		}

		AABB posed = AABB::Empty();
		const auto place = [&](const AABB& box, const Mat4* matrix) {
			if (!box.IsValid()) {
				return;
			}
			const AABB reached(box.min + reach.min, box.max + reach.max);
			posed.Encapsulate(matrix ? reached.Transformed(*matrix) : reached);
		};
		place(bounds.unjointed, nullptr);
		for (size_t j = 0; j < bounds.joints.size(); ++j) {
			place(bounds.joints[j], j < skinMatrices.size() ? &skinMatrices[j] : nullptr);
		}
		return posed;
	}

	void SkinDeformer::Begin(RHI::Device& device, u64 maxVertices) {
		_jobs.clear();
		_used = 0;
		const u64 bytes = maxVertices * sizeof(MeshVertex);
		if (bytes <= _capacity) {
			_outputAddress = _output.IsValid() ? device.GetBufferDeviceAddress(_output) : 0;
			return;
		}
		if (_output.IsValid()) {
			device.DestroyResource(_output);
		}
		u64 capacity = 64ull * 1024;
		while (capacity < bytes) {
			capacity *= 2;
		}
		RHI::BufferDesc desc;
		desc.size = capacity;
		desc.usage = RHI::BufferUsage::Storage | RHI::BufferUsage::DeviceAddress | RHI::BufferUsage::TransferSrc;
		desc.debugName = "SkinnedVertices";
		_output = device.CreateBufferResource(desc);
		_capacity = _output.IsValid() ? capacity : 0;
		_outputAddress = _output.IsValid() ? device.GetBufferDeviceAddress(_output) : 0;
	}

	u64 SkinDeformer::Add(RHI::Device& device, const MeshAsset& mesh, std::span<const Mat4> skinMatrices, std::span<const f32> weights) {
		const MeshDeform* deform = MeshDeform::Of(mesh);
		const u64 vertexCount = mesh.vertices.size();
		const u64 bytes = vertexCount * sizeof(MeshVertex);
		if (!deform || vertexCount == 0 || mesh.vertexBufferAddress == 0 || _used + bytes > _capacity) {
			return 0;
		}

		GPUSkinningPush push;
		if (!skinMatrices.empty() && deform->influencesAddress != 0) {
			RHI::TransientAllocation matrices = device.AllocateTransient(skinMatrices.size_bytes(), 16);
			if (!matrices.IsValid()) {
				return 0;
			}
			std::memcpy(matrices.cpu, skinMatrices.data(), skinMatrices.size_bytes());
			push.influences = deform->influencesAddress;
			push.influenceCount = static_cast<u32>(std::min<u64>(mesh.skinInfluences.size(), vertexCount));
			push.matrices = device.GetBufferDeviceAddress(matrices.buffer) + matrices.offset;
			push.jointCount = static_cast<u32>(skinMatrices.size());
		}
		if (deform->morphDeltasAddress != 0 && HasActiveBlendShapes(mesh, weights)) {
			RHI::TransientAllocation scales = device.AllocateTransient(static_cast<u64>(deform->frameCount) * sizeof(f32), 16);
			if (!scales.IsValid()) {
				return 0;
			}
			bool movesNormals = false;
			BlendShapeFrameScales(mesh, weights, {static_cast<f32*>(scales.cpu), deform->frameCount}, movesNormals);
			push.morphRanges = deform->morphRangesAddress;
			push.morphDeltas = deform->morphDeltasAddress;
			push.frameScales = device.GetBufferDeviceAddress(scales.buffer) + scales.offset;
			push.flags |= movesNormals ? SKINNING_RENORMALIZE : 0u;
		}
		if (push.matrices == 0 && push.frameScales == 0) {
			return 0;
		}

		push.source = mesh.vertexBufferAddress;
		push.output = _outputAddress + _used;
		push.vertexCount = static_cast<u32>(vertexCount);
		_jobs.push_back(push);
		_used += bytes;
		return push.output;
	}

	void SkinDeformer::Record(RHI::CommandList& cmd, RHI::PipelineHandle kernel) const {
		if (_jobs.empty() || !kernel.IsValid()) {
			return;
		}
		cmd.SetPipeline(kernel);
		for (const GPUSkinningPush& push : _jobs) {
			cmd.PushConstants({reinterpret_cast<const u8*>(&push), sizeof(push)});
			const u32 groups = (push.vertexCount + SKINNING_GROUP_SIZE - 1) / SKINNING_GROUP_SIZE;
			cmd.Dispatch(std::min(groups, SKINNING_MAX_GROUPS_X), (groups + SKINNING_MAX_GROUPS_X - 1) / SKINNING_MAX_GROUPS_X);
		}
	}

	void SkinDeformer::Destroy(RHI::Device& device) {
		if (_output.IsValid()) {
			device.DestroyResource(_output);
		}
		_output = {};
		_outputAddress = 0;
		_capacity = 0;
		_used = 0;
		_jobs.clear();
	}

} // namespace Shalltear::GraphicsInternal
