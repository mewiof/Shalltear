#pragma once

#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"

#include "Shalltear/Editor/EditorViewportOps.h"

#include <span>

namespace PassProbes {

	using namespace Shalltear;

	struct GridPush {
		Mat4 invGridProjection;
		Vec3 gridCameraPosition;
		f32 cellPixelBudget;
		f32 fadeDistance;
		f32 minorLineWidthPx;
		f32 majorLineWidthPx;
		u32 minorColor;
		u32 majorColor;
		u32 axisAlongXColor;
		u32 axisAlongZColor;
	};
	inline constexpr size_t GRID_PUSH_BYTES = 108;
	static_assert(sizeof(GridPush) == 112 && GRID_PUSH_BYTES < sizeof(GridPush),
				  "the grid push block must match the shader's field for field");

	inline GridPush MakeVisiblePush(const Mat4& view, const Mat4& projection, const Vec3& position) {
		GridPush push{};
		const EditorViewportOps::GridBasis basis =
			EditorViewportOps::GridBasisFor(EditorViewportOps::GridPlaneFor(view, projection));
		push.invGridProjection = basis.gridFromWorld * (projection * view).Inverse();
		push.gridCameraPosition = basis.gridFromWorld.TransformPoint(position);
		push.cellPixelBudget = 24.0f;
		push.fadeDistance = 4000.0f;
		push.minorLineWidthPx = 1.0f;
		push.majorLineWidthPx = 1.8f;
		push.minorColor = Color32(200, 200, 210, 255).ToU32();
		push.majorColor = Color32(255, 255, 255, 255).ToU32();
		push.axisAlongXColor = Color32(255, 0, 0, 255).ToU32();
		push.axisAlongZColor = Color32(0, 0, 255, 255).ToU32();
		return push;
	}

	class DrawingProbePass : public RenderPass {
	public:
		Handle<ShaderAsset> shader;
		std::span<const u8> bytes;
		bool drew = false;

		void Record(PassContext& context) override {
			drew = context.DrawProcedural(shader, bytes);
		}
	};

} // namespace PassProbes
