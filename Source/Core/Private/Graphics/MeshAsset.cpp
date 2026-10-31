#include "Shalltear/Core/Graphics/MeshAsset.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/RHIDevice.h"

namespace Shalltear {

	static_assert(sizeof(MeshVertex) == 52);
	static_assert(sizeof(SubMesh) == 36, "3 u32 plus a 24-byte AABB, no padding");
	static_assert(sizeof(MeshSkinInfluence) == 24);
	static_assert(std::is_trivially_copyable_v<MeshVertex>);
	static_assert(std::is_trivially_copyable_v<SubMesh>);
	static_assert(std::is_trivially_copyable_v<MeshSkinInfluence>);

	MeshAsset::~MeshAsset() = default;

	void MeshAsset::OnLoad() {
		if (!RenderSystem::IsRenderingAvailable() || vertices.empty()) {
			return;
		}
		RHI::Device* device = RenderSystem::GetDevice();

		RHI::BufferDesc vertexDesc;
		vertexDesc.size = vertices.size() * sizeof(MeshVertex);
		vertexDesc.usage = RHI::BufferUsage::Storage | RHI::BufferUsage::DeviceAddress | RHI::BufferUsage::TransferDst;
		vertexDesc.debugName = virtualPath.c_str();
		vertexBuffer = device->CreateBufferResource(vertexDesc);
		if (vertexBuffer.IsValid()) {
			device->UploadBuffer(vertexBuffer, {reinterpret_cast<const u8*>(vertices.data()), vertexDesc.size});
			vertexBufferAddress = device->GetBufferDeviceAddress(vertexBuffer);
		}

		RHI::BufferDesc indexDesc;
		indexDesc.size = indices.size() * sizeof(u32);
		indexDesc.usage = RHI::BufferUsage::Index | RHI::BufferUsage::Storage | RHI::BufferUsage::DeviceAddress | RHI::BufferUsage::TransferDst;
		indexDesc.debugName = virtualPath.c_str();
		indexBuffer = device->CreateBufferResource(indexDesc);
		if (indexBuffer.IsValid()) {
			device->UploadBuffer(indexBuffer, {reinterpret_cast<const u8*>(indices.data()), indexDesc.size});
			indexBufferAddress = device->GetBufferDeviceAddress(indexBuffer);
		}
	}

	void MeshAsset::OnUnload() {
		if (!RenderSystem::IsRenderingAvailable()) {
			vertexBuffer = {};
			indexBuffer = {};
			vertexBufferAddress = 0;
			indexBufferAddress = 0;
			return;
		}
		RHI::Device* device = RenderSystem::GetDevice();
		if (vertexBuffer.IsValid()) {
			device->DestroyResource(vertexBuffer);
			vertexBuffer = {};
		}
		if (indexBuffer.IsValid()) {
			device->DestroyResource(indexBuffer);
			indexBuffer = {};
		}
		vertexBufferAddress = 0;
		indexBufferAddress = 0;
	}

	bool MeshAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		descriptor.Write(bounds.min.x);
		descriptor.Write(bounds.min.y);
		descriptor.Write(bounds.min.z);
		descriptor.Write(bounds.max.x);
		descriptor.Write(bounds.max.y);
		descriptor.Write(bounds.max.z);
		descriptor.Write(static_cast<u32>(vertices.size()));
		descriptor.Write(static_cast<u32>(indices.size()));
		descriptor.Write(static_cast<u32>(submeshes.size()));
		descriptor.WriteBytes(submeshes.data(), submeshes.size() * sizeof(SubMesh));

		// appended (older artifact runs out of bytes -> reimports)
		descriptor.Write(static_cast<u32>(skinInfluences.size()));

		descriptor.Write(static_cast<u32>(blendShapes.size()));
		for (const MeshBlendShape& shape : blendShapes) {
			descriptor.WriteString(shape.name);
			descriptor.Write(static_cast<u32>(shape.frames.size()));
			for (const MeshBlendShapeFrame& frame : shape.frames) {
				descriptor.Write(frame.weight);
				descriptor.Write(static_cast<u32>(frame.vertices.size()));
				descriptor.Write(static_cast<u8>(frame.normalDeltas.empty() ? 0 : 1));
			}
		}

		payload.WriteBytes(vertices.data(), vertices.size() * sizeof(MeshVertex));
		payload.WriteBytes(indices.data(), indices.size() * sizeof(u32));
		payload.WriteBytes(skinInfluences.data(), skinInfluences.size() * sizeof(MeshSkinInfluence));
		for (const MeshBlendShape& shape : blendShapes) {
			for (const MeshBlendShapeFrame& frame : shape.frames) {
				payload.WriteBytes(frame.vertices.data(), frame.vertices.size() * sizeof(u32));
				payload.WriteBytes(frame.positionDeltas.data(), frame.positionDeltas.size() * sizeof(Vec3));
				payload.WriteBytes(frame.normalDeltas.data(), frame.normalDeltas.size() * sizeof(Vec3));
			}
		}
		return true;
	}

	bool MeshAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		AABB read;
		descriptor.Read(read.min.x);
		descriptor.Read(read.min.y);
		descriptor.Read(read.min.z);
		descriptor.Read(read.max.x);
		descriptor.Read(read.max.y);
		descriptor.Read(read.max.z);
		u32 vertexCount = 0;
		u32 indexCount = 0;
		u32 submeshCount = 0;
		descriptor.Read(vertexCount);
		descriptor.Read(indexCount);
		descriptor.Read(submeshCount);

		descriptor.ReadInto(submeshes, submeshCount);
		u32 influenceCount = 0;
		descriptor.Read(influenceCount);

		u32 shapeCount = 0;
		descriptor.Read(shapeCount);
		Vector<MeshBlendShape> readShapes{GetMemoryResource(MemTag::Asset)};
		Vector<u8> frameHasNormals{GetMemoryResource(MemTag::Asset)};
		Vector<u32> frameDeltaCounts{GetMemoryResource(MemTag::Asset)};
		if (!descriptor.Failed()) {
			readShapes.reserve(shapeCount);
			for (u32 s = 0; s < shapeCount && !descriptor.Failed(); ++s) {
				MeshBlendShape shape;
				shape.name = str(descriptor.ReadString());
				u32 frameCount = 0;
				descriptor.Read(frameCount);
				if (descriptor.Failed()) {
					break;
				}
				shape.frames.reserve(frameCount);
				for (u32 f = 0; f < frameCount && !descriptor.Failed(); ++f) {
					MeshBlendShapeFrame frame;
					u32 deltaCount = 0;
					u8 hasNormals = 0;
					descriptor.Read(frame.weight);
					descriptor.Read(deltaCount);
					descriptor.Read(hasNormals);
					frameDeltaCounts.push_back(deltaCount);
					frameHasNormals.push_back(hasNormals);
					shape.frames.push_back(std::move(frame));
				}
				readShapes.push_back(std::move(shape));
			}
		}

		payload.ReadInto(vertices, vertexCount);
		payload.ReadInto(indices, indexCount);
		payload.ReadInto(skinInfluences, influenceCount);

		size_t frameIndex = 0;
		for (MeshBlendShape& shape : readShapes) {
			for (MeshBlendShapeFrame& frame : shape.frames) {
				const u32 deltaCount = frameDeltaCounts[frameIndex];
				payload.ReadInto(frame.vertices, deltaCount);
				payload.ReadInto(frame.positionDeltas, deltaCount);
				if (frameHasNormals[frameIndex] != 0) {
					payload.ReadInto(frame.normalDeltas, deltaCount);
				}
				++frameIndex;
			}
		}

		if (descriptor.Failed() || payload.Failed()) {
			return false;
		}
		bounds = read;
		blendShapes = std::move(readShapes);
		return true;
	}

	i32 MeshAsset::FindBlendShape(std::string_view name) const {
		for (size_t i = 0; i < blendShapes.size(); ++i) {
			if (blendShapes[i].name == name) {
				return static_cast<i32>(i);
			}
		}
		return -1;
	}

	bool MeshAsset::CookImportState(CookWriter& importState) const {
		importState.Write(static_cast<u32>(defaultMaterials.size()));
		for (const Guid& material : defaultMaterials) {
			importState.Write(material);
		}
		return true;
	}

	bool MeshAsset::LoadImportState(CookReader& importState) {
		u32 count = 0;
		importState.Read(count);
		Vector<Guid> restored{GetMemoryResource(MemTag::Asset)};
		if (!importState.ReadInto(restored, count)) {
			return false;
		}
		defaultMaterials = std::move(restored);
		return true;
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::MeshAsset)
