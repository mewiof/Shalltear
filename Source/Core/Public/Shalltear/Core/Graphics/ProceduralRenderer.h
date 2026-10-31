#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	class ComputeContext;
	class MaterialAsset;

	namespace RHI {
		class CommandList;
		class Device;
	} // namespace RHI

	namespace GraphicsInternal {
		struct ProceduralPlanInit;
		struct ProceduralContextInit;
		struct ProceduralFrame;
		struct ProceduralCulling;
	} // namespace GraphicsInternal

	// frame scoped, invalid after Declare
	class SHALLTEAR_CORE_API ProceduralPlan {
	public:
		explicit ProceduralPlan(const GraphicsInternal::ProceduralPlanInit& init);

		void SetBounds(const AABB& worldBounds);

		u32 AddItem(const Handle<MaterialAsset>& material, const Vec3& sortPoint, bool castShadows = false);

		u32 AddViewStep();

		void Read(RHI::BufferHandle buffer, RHI::ResourceState state);
		void Write(RHI::BufferHandle buffer, RHI::ResourceState state);
	private:
		GraphicsInternal::ProceduralFrame* _frame = nullptr;
		u32 _renderer = 0;
		bool _inStep = false;
	};

	struct ProceduralView {
		Mat4 view = Mat4::Identity();
		Mat4 projection = Mat4::Identity();
		Vec3 cameraPosition{};
		Vec2I extent{1, 1};
	};

	enum class ProceduralPass : u8 {
		Scene = 0,
		Prepass,
		Shadow,
		Value,
	};

	class SHALLTEAR_CORE_API ProceduralContext {
	public:
		explicit ProceduralContext(const GraphicsInternal::ProceduralContextInit& init);

		u32 GetItem() const { return _item; }

		ProceduralPass GetPass() const { return _pass; }

		u32 GetValue() const { return _value; }

		bool Draw(std::span<const u8> pushConstants, u32 vertexCount, u32 instanceCount = 1);

		bool DrawIndirect(std::span<const u8> pushConstants, RHI::BufferHandle arguments, u64 offset = 0);

		const Mat4& GetView() const { return _view; }
		const Mat4& GetProjection() const { return _projection; }
		const Vec3& GetCameraPosition() const { return _cameraPosition; }
		Vec2I GetExtent() const { return _extent; }
	private:
		bool BindItem(std::span<const u8> pushConstants) const;

		RHI::CommandList* _cmd = nullptr;
		RHI::Device* _device = nullptr;
		const GraphicsInternal::ProceduralFrame* _frame = nullptr;
		u32 _flatItem = 0;
		u32 _item = 0;
		ProceduralPass _pass = ProceduralPass::Scene;
		u32 _value = 0;
		RHI::Format _colorFormat = RHI::Format::Unknown;
		RHI::Format _depthFormat = RHI::Format::Unknown;
		u32 _samples = 1;
		RHI::TextureHandle _sceneDepth;
		RHI::TransientAllocation _viewBlock{};
		RHI::TextureHandle _shadowAtlas;
		RHI::TextureHandle _punctualAtlas;
		RHI::TextureHandle _environmentCube;
		RHI::TextureHandle _environmentCube2;
		RHI::TextureHandle _ambientOcclusion;
		u32* _drawCalls = nullptr;
		Mat4 _view = Mat4::Identity();
		Mat4 _projection = Mat4::Identity();
		Vec3 _cameraPosition{};
		Vec2I _extent{1, 1};
	};

	class SHALLTEAR_CORE_API ProceduralRenderer : public Component {
	public:
		SLT_REFLECT_COMPONENT_ABSTRACT(ProceduralRenderer, Component)
		SLT_END_REFLECT()

		~ProceduralRenderer() override;

		virtual void Declare(ProceduralPlan& plan) = 0;

		virtual void Record(ProceduralContext& context) = 0;

		virtual void RecordView(ComputeContext& context, const ProceduralView& view);

		bool IsCulled() const { return _tested && !_drawn; }
	protected:
		// overrides must call this
		void OnEnable() override;
		void OnDisable() override;
	private:
		friend struct GraphicsInternal::ProceduralCulling;
		bool _tested = false;
		bool _drawn = false;
	};

} // namespace Shalltear
