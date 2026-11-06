#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/ProceduralRenderer.h"
#include "Shalltear/Core/Graphics/ShadowCasting.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Gradient.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/VisualEffects/LineRenderer.h"

#include <span>

namespace Shalltear {

	class MaterialAsset;

	namespace VisualEffectsImpl {

		struct RibbonPoint {
			Vec3 position{};
			f32 width = 0.0f;
			Color color = Color::White();
			f32 u = 0.0f;
		};

		struct RibbonFacing {
			Vec3 eye{};
			Vec3 axis{0.0f, 0.0f, 1.0f};
			bool fromEye = false;
			f32 shadowBias = 0.0f;
		};

		struct RibbonShape {
			u32 cornerVertices = 0;
			u32 endCapVertices = 0;
			bool loop = false;
			Vec2 textureScale{1.0f, 1.0f};
		};

		struct RibbonStyle {
			const AnimationCurve* width = nullptr;
			f32 widthMultiplier = 1.0f;
			const Gradient* color = nullptr;
			LineTextureMode textureMode = LineTextureMode::Stretch;
		};

		struct RibbonState {
			Vector<RibbonPoint> points{GetMemoryResource(MemTag::Graphics)};
			Vector<Vec3> positions{GetMemoryResource(MemTag::Graphics)};
			Vector<f32> t{GetMemoryResource(MemTag::Graphics)};
			Vec3 transformZ{0.0f, 0.0f, 1.0f};
		};

		// sampled by length when t is empty
		void FillRibbonPoints(std::span<const Vec3> positions, std::span<const f32> t, const RibbonStyle& style, bool loop, Vector<RibbonPoint>& out);

		RibbonFacing FacingFor(LineAlignment alignment, const Vec3& transformZ, const Mat4& view, const Mat4& projection, const Vec3& eye);

		void BuildRibbon(std::span<const RibbonPoint> points, const RibbonShape& shape, const RibbonFacing& facing, Vector<MeshVertex>& out);

		AABB RibbonBounds(std::span<const RibbonPoint> points);

		void DeclareRibbon(ProceduralPlan& plan, std::span<const RibbonPoint> points, std::span<const Handle<MaterialAsset>> materials,
						   ShadowCasting castShadows);

		void RecordRibbon(ProceduralContext& context, std::span<const RibbonPoint> points, const RibbonShape& shape, LineAlignment alignment,
						  const Vec3& transformZ, f32 shadowBias, bool receiveShadows);

		void ShutdownRibbons();

		void AdvanceTrails(f32 deltaTime);
		void ShutdownTrails();

	} // namespace VisualEffectsImpl

} // namespace Shalltear
