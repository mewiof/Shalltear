#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UITypes.h"

namespace Shalltear::RHI {
	class CommandList;
}

namespace Shalltear::UI {

	class Context;

	inline constexpr u32 QUAD_FLAG_GLYPH = 1u << 0;
	inline constexpr u32 QUAD_FLAG_DITHER = 1u << 1;
	inline constexpr u32 QUAD_FLAG_GRADIENT_VERTICAL = 1u << 2;
	inline constexpr u32 QUAD_FLAG_SDF_FROM_UV = 1u << 3;
	inline constexpr u32 QUAD_FLAG_PREMULTIPLIED = 1u << 4;
	inline constexpr u32 QUAD_FLAG_RING = 1u << 5;
	inline constexpr u32 QUAD_FLAG_ROTATED = 1u << 6;

	struct UIQuad {
		Rect rect;
		Rect uv;
		Color color0; // linear, alpha premultiplied by opacity
		Color color1;
		Radii radii;
		Rect clipRect;
		Radii clipRadii;
		f32 softness = 0.0f;
		u32 flags = 0;
		// rads clockwise, +y down (QUAD_FLAG_ROTATED only)
		f32 rotation = 0.0f;
		f32 pad0 = 0.0f;
	};
	static_assert(sizeof(UIQuad) == 128, "UIQuad must match the UI.hlsl 128-byte stride");

	enum class UICommandKind : u8 {
		Quads = 0,
		PushLayer,
		PopLayer,
		PushShadow,
		PopShadow,
	};

	struct UICommand {
		UICommandKind kind = UICommandKind::Quads;
		SamplerKind sampler = SamplerKind::LinearClamp;
		i32 z = 0;
		RHI::TextureHandle texture;
		u32 firstQuad = 0;
		u32 quadCount = 0;
		Rect layerBounds{};
		Radii layerRadii{};
		f32 layerOpacity = 1.0f;
		u32 shadowCaster = 0;
	};

	inline constexpr RHI::TextureHandle LayerCompositeTexture{0xFFFFFFFFu, 0xFFFFFFFFu};

	inline constexpr RHI::TextureHandle ShadowMaskTexture{0xFFFFFFFEu, 0xFFFFFFFFu};

	struct UIShadowCaster {
		// context physical px, whole pixels
		Rect source{};
		RectI slot{};
		i32 marginPx = 0;
		f32 blurPx = 0.0f;
	};

	struct RenderList {
		Vector<UIQuad> quads{GetMemoryResource(MemTag::UI)};
		Vector<UICommand> commands{GetMemoryResource(MemTag::UI)};
		Vector<UIShadowCaster> shadowCasters{GetMemoryResource(MemTag::UI)};
		Vec2I shadowAtlasSizePx{};

		void Clear() {
			quads.clear();
			commands.clear();
			shadowCasters.clear();
			shadowAtlasSizePx = {};
		}
	};

	struct SubmitInfo {
		RHI::CommandList* commandList = nullptr;
		RHI::TextureHandle target;
		Vec2I targetSizePx{};
	};

	class SHALLTEAR_CORE_API IRenderer {
	public:
		virtual ~IRenderer() = default;
		virtual void Submit(const RenderList& list, const SubmitInfo& info) = 0;
	};

	enum class TexelEncoding : u8 {
		Straight = 0,
		Premultiplied,
		Coverage,
	};

	enum class LineCap : u8 {
		Butt = 0,
		Round,
	};

	class SHALLTEAR_CORE_API PaintScope {
	public:
		void EmitQuad(const Rect& rect, Color colorSRGB, const Radii& radii = {}, f32 softness = 0.0f);
		void EmitTexturedQuad(const Rect& rect, RHI::TextureHandle texture, const Rect& uv, Color tintSRGB,
							  SamplerKind sampler = SamplerKind::LinearClamp, const Radii& radii = {},
							  TexelEncoding encoding = TexelEncoding::Straight);
		void EmitRing(const Rect& rect, const Edges& widthsPx, Color colorSRGB, const Radii& radii = {});

		// physical px
		void EmitLine(Vec2 a, Vec2 b, f32 widthPx, Color colorSRGB, LineCap cap = LineCap::Butt);
	private:
		friend struct PaintScopeAccess;
		PaintScope() = default;

		RenderList* _list = nullptr;
		Rect* _ink = nullptr;
		Rect _clipRect = InfiniteClip;
		Radii _clipRadii{};
		f32 _opacity = 1.0f;
		i32 _z = 0;
	};

} // namespace Shalltear::UI
