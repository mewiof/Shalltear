#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RHITypes.h"

#include <span>

namespace Shalltear::RHI {

	struct CommandListImpl;

	class SHALLTEAR_CORE_API CommandList {
	public:
		CommandList(const CommandList&) = delete;
		CommandList& operator=(const CommandList&) = delete;

		void TransitionTexture(TextureHandle texture, ResourceState newState);
		void TransitionBuffer(BufferHandle buffer, ResourceState newState);

		void UnorderedAccessBarrier(BufferHandle buffer);

		// target formats must match, no pass open at EndFrame
		void BeginRenderPass(const RenderPassDesc& desc);
		void EndRenderPass();

		void SetPipeline(PipelineHandle pipeline);
		// until the next SetPipeline (which restores Clockwise)
		void SetFrontFace(FrontFace face);
		void SetViewportScissor(RectI rect);
		void SetScissor(RectI rect);
		void BindTransientSet(u32 setIndex, TransientSet set);
		void PushConstants(std::span<const u8> data);

		void BindIndexBuffer(BufferHandle buffer, bool sixteenBit = false);

		void Draw(u32 vertexCount, u32 instanceCount = 1);
		void DrawIndexed(u32 indexCount, u32 firstIndex = 0, u32 instanceCount = 1);
		// arguments need BufferUsage::Indirect
		void DrawIndirect(BufferHandle arguments, u64 offset = 0);

		// counts are thread groups
		void Dispatch(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1);
		// arguments need BufferUsage::Indirect + IndirectArgument state
		void DispatchIndirect(BufferHandle arguments, u64 offset = 0);

		void CopyBufferRegion(BufferHandle src, BufferHandle dst, u64 srcOffset, u64 dstOffset, u64 size);
		void CopyBufferToTexture(BufferHandle src, TextureHandle dst, u32 mipLevel = 0);
		void CopyTextureToBuffer(TextureHandle src, BufferHandle dst, u64 dstOffset = 0, u32 mipLevel = 0, u32 layer = 0);
		void GenerateMips(TextureHandle target);
		// same format/size
		void ResolveTexture(TextureHandle src, TextureHandle dst);
	private:
		friend class Device;
		friend struct DeviceImpl;
		friend struct FrameContext;
		CommandList() = default;
		~CommandList() = default;

		CommandListImpl* _impl = nullptr;
	};

} // namespace Shalltear::RHI
