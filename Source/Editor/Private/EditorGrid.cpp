#include "EditorGrid.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Log.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorViewportOps.h"

#include <cstring>

namespace Shalltear {

	namespace Ops = EditorViewportOps;

	namespace {

		using EditorTheme::ColorToken;

		constexpr const char* GRID_SHADER_PATH = "engine://Shaders/EditorGrid.hlsl";

		// framebuffer pxs (not theme metrics)
		constexpr f32 MINOR_LINE_WIDTH_PX = 1.0f;
		constexpr f32 MAJOR_LINE_WIDTH_PX = 1.8f;
		constexpr f32 CELL_PIXEL_BUDGET = 24.0f;
		constexpr f32 FADE_HEIGHT_MULTIPLE = 60.0f;
		constexpr f32 FADE_SIZE_MULTIPLE = 200.0f;
		constexpr f32 FADE_MINIMUM = 40.0f;

		// must match ShalltearGridPush in EditorGrid.hlsl
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
		constexpr size_t GRID_PUSH_BYTES = 108;
		static_assert(sizeof(GridPush) == 112 && GRID_PUSH_BYTES < sizeof(GridPush),
					  "the grid push block must match ShalltearGridPush in EditorGrid.hlsl");

		ColorToken AxisToken(u32 worldAxis) {
			return worldAxis == 0 ? ColorToken::AxisX : (worldAxis == 1 ? ColorToken::AxisY : ColorToken::AxisZ);
		}

		Handle<ShaderAsset> _shader;
		bool _attempted = false;

		ShaderAsset* GetShader() {
			if (!_attempted) {
				_attempted = true;
				Asset* asset = AssetDatabase::LoadAsset(Path(GRID_SHADER_PATH));
				if (asset && asset->GetAssetTypeId() == StringHash("ShaderAsset")) {
					_shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(asset));
				} else {
					if (asset) {
						AssetDatabase::UnloadAsset(asset);
					}
					SLT_LOG_WARN("Editor", "grid shader '{}' unavailable", GRID_SHADER_PATH);
				}
			}
			return _shader.Get();
		}

	} // anonymous namespace

	EditorGridPass::EditorGridPass() {
		point = PassPoint::Overlay;
		position = FrameStagePosition::Early;
	}

	void EditorGridPass::Shutdown() {
		if (Asset* asset = _shader.Get()) {
			AssetDatabase::UnloadAsset(asset);
		}
		_shader = {};
		_attempted = false;
	}

	void EditorGridPass::Record(PassContext& context) {
		if (!GetShader()) {
			return;
		}

		const Mat4& view = context.GetView();
		const Mat4& projection = context.GetProjection();
		const Ops::GridBasis basis = Ops::GridBasisFor(Ops::GridPlaneFor(view, projection));

		GridPush push{};
		push.invGridProjection = basis.gridFromWorld * (projection * view).Inverse();
		push.gridCameraPosition = basis.gridFromWorld.TransformPoint(context.GetCameraPosition());
		push.cellPixelBudget = CELL_PIXEL_BUDGET;
		const bool orthographic = Math::Abs(projection(3, 2)) < 1.0e-6f;
		const f32 reach = orthographic
							  ? (projection(1, 1) != 0.0f ? 1.0f / Math::Abs(projection(1, 1)) : FADE_MINIMUM) * FADE_SIZE_MULTIPLE
							  : Math::Abs(push.gridCameraPosition.y) * FADE_HEIGHT_MULTIPLE;
		push.fadeDistance = Math::Max(reach, FADE_MINIMUM);
		push.minorLineWidthPx = MINOR_LINE_WIDTH_PX;
		push.majorLineWidthPx = MAJOR_LINE_WIDTH_PX;
		push.minorColor = EditorTheme::GetColor32(ColorToken::GridLine).ToU32();
		push.majorColor = EditorTheme::GetColor32(ColorToken::GridMajor).ToU32();
		push.axisAlongXColor = EditorTheme::GetColor32(AxisToken(basis.alongX)).ToU32();
		push.axisAlongZColor = EditorTheme::GetColor32(AxisToken(basis.alongZ)).ToU32();

		u8 bytes[sizeof(GridPush)];
		std::memcpy(bytes, &push, sizeof(GridPush));
		context.DrawProcedural(_shader, {bytes, GRID_PUSH_BYTES});
	}

} // namespace Shalltear
