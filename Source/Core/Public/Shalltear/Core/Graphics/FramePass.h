#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	class Camera;
	class ShaderAsset;
	class TextureAsset;

	namespace GraphicsInternal {
		struct FramePassPlanInit;
		struct ComputeContextInit;
		class RenderGraph;
		struct GraphExecution;
		struct GraphPass;
	} // namespace GraphicsInternal

	struct FrameImage {
		u32 id = ~0u;
		bool IsValid() const { return id != ~0u; }
	};

	struct CameraDepth {
		FrameImage image;
		Mat4 view = Mat4::Identity();
		Mat4 projection = Mat4::Identity();
		Vec2I size{};
	};

	class SHALLTEAR_CORE_API FramePassPlan {
	public:
		explicit FramePassPlan(const GraphicsInternal::FramePassPlanInit& init);

		u32 AddStep();

		void Read(RHI::BufferHandle buffer, RHI::ResourceState state);
		void Write(RHI::BufferHandle buffer, RHI::ResourceState state);

		// last frame's depth
		CameraDepth ReadCameraDepth(Handle<Camera> camera);
	private:
		GraphicsInternal::RenderGraph* _graph = nullptr;
		u32 _passIndex = 0;
		u32 _zoneSite = 0;
		u32 _stepCount = 0;
	};

	struct ComputeBinding {
		u32 binding = 0;
		RHI::BufferHandle buffer;
	};

	struct ComputeTexture {
		u32 binding = 0;
		u32 samplerBinding = 0;
		Handle<TextureAsset> texture;
		FrameImage image;
	};

	class SHALLTEAR_CORE_API ComputeContext {
	public:
		explicit ComputeContext(const GraphicsInternal::ComputeContextInit& init);

		u32 GetStep() const { return _step; }

		bool Dispatch(const Handle<ShaderAsset>& kernel, std::span<const ComputeBinding> buffers, std::span<const ComputeTexture> textures,
					  std::span<const u8> pushConstants, u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1);

		bool DispatchIndirect(const Handle<ShaderAsset>& kernel, std::span<const ComputeBinding> buffers, std::span<const ComputeTexture> textures,
							  std::span<const u8> pushConstants, RHI::BufferHandle arguments, u64 offset = 0);

		bool CopyBuffer(RHI::BufferHandle source, RHI::BufferHandle destination, u64 sourceOffset, u64 destinationOffset, u64 size);
	private:
		GraphicsInternal::GraphExecution* _exec = nullptr;
		const GraphicsInternal::GraphPass* _pass = nullptr;
		u32 _step = 0;
	};

	class SHALLTEAR_CORE_API FramePass {
	public:
		virtual ~FramePass();

		virtual void Declare(FramePassPlan& plan) = 0;

		virtual void Record(ComputeContext& context) = 0;
	};

	namespace FramePasses {

		SHALLTEAR_CORE_API bool Register(FramePass& pass, const void* owner);

		SHALLTEAR_CORE_API u32 UnregisterOwnedBy(const void* owner);

	} // namespace FramePasses

} // namespace Shalltear
