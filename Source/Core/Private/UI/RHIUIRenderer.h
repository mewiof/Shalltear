#pragma once

#include "UI/UIInternal.h"

#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Handle.h"

namespace Shalltear {
	class ShaderAsset;
	namespace RHI {
		class Device;
	}
} //namespace Shalltear

namespace Shalltear::UI {

	class RHIUIRenderer final : public IRenderer, public IAtlasBackend {
	public:
		explicit RHIUIRenderer(RHI::Device* device);
		~RHIUIRenderer() override;
		RHIUIRenderer(const RHIUIRenderer&) = delete;
		RHIUIRenderer& operator=(const RHIUIRenderer&) = delete;

		void Submit(const RenderList& list, const SubmitInfo& info) override;

		RHI::TextureHandle CreateAtlas(Vec2I size, AtlasFormat format) override;
		void UpdateAtlas(RHI::TextureHandle atlas, RectI region, std::span<const u8> pixels) override;

		u32 GetLastDrawCalls() const { return _lastDrawCalls; }
		u32 GetLastQuads() const { return _lastQuads; }
	private:
		RHI::PipelineHandle GetOrCreatePipeline(RHI::Format targetFormat);
		RHI::TextureHandle AcquireTarget(Vec2I size, RHI::Format format, const char* debugName);
		RHI::TextureHandle DrawShadowMasks(RHI::CommandList& cmd, const RenderList& list, u64 quadsAddress,
										   u64 blurQuadsAddress, u32 blurQuadCount);

		RHI::Device* _device = nullptr;
		Handle<ShaderAsset> _shader;
		bool _shaderLoadAttempted = false;
		u32 _shaderVersion = 0;

		struct PipelineEntry {
			RHI::Format format = RHI::Format::Unknown;
			RHI::PipelineHandle pipeline;
		};
		Vector<PipelineEntry> _pipelines{GetMemoryResource(MemTag::UI)};

		RHI::SamplerHandle _samplers[static_cast<size_t>(SamplerKind::Count)];
		bool _samplersCreated = false;
		RHI::TextureHandle _whiteTexture;

		struct PooledTarget {
			RHI::TextureHandle texture;
			Vec2I size{};
			RHI::Format format = RHI::Format::Unknown;
			bool usedThisSubmit = false;
			u64 lastUsedSubmit = 0;
		};
		static constexpr u64 RETENTION_SUBMITS = 120;
		static constexpr size_t MAX_POOLED_TARGETS = 32;
		Vector<PooledTarget> _targetPool{GetMemoryResource(MemTag::UI)};
		u64 _submitIndex = 0;

		Vector<RHI::TextureHandle> _atlases{GetMemoryResource(MemTag::UI)};

		u32 _lastDrawCalls = 0;
		u32 _lastQuads = 0;
	};

} // namespace Shalltear::UI
