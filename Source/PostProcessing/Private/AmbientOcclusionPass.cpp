#include "Shalltear/PostProcessing/AmbientOcclusionPass.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Math/Matrix.h"

#include "AmbientOcclusionShader.h"

#include <iterator>

namespace Shalltear {

	namespace {

		// must match ShalltearAmbientOcclusionPush in AmbientOcclusion.hlsl
		struct AmbientOcclusionPush {
			f32 viewRows[3][4];
			f32 unproject[4];
			f32 projHalfAndLinearize[4];
			f32 radiusFalloffIntensity[4];
			// xy = step extent px, z = step kind, w = slice count
			f32 extentStepSlices[4];
		};
		static_assert(sizeof(AmbientOcclusionPush) == 112, "the push block is 7 float4s (under the 128 B budget)");

		enum class AmbientOcclusionStep : u32 {
			Estimate = 0,
			BlurX = 1,
			BlurY = 2,
			Composite = 3,
			// shader branches on these
			Upsample = 4,
			SmoothX = 5,
			SmoothY = 6
		};

		constexpr AmbientOcclusionStep FILTER_CHAIN[] = {AmbientOcclusionStep::Estimate, AmbientOcclusionStep::BlurX,
														 AmbientOcclusionStep::BlurY, AmbientOcclusionStep::SmoothX,
														 AmbientOcclusionStep::SmoothY};
		constexpr u32 FILTER_CHAIN_STEPS = static_cast<u32>(std::size(FILTER_CHAIN));

		constexpr i32 MIN_DIRECTIONS = 1;
		constexpr i32 MAX_DIRECTIONS = 16;

	} // namespace

	PassRequests AmbientOcclusionPass::GetRequests() const {
		const PassRequests base = PassRequests::SceneDepth | PassRequests::SceneNormals;
		return afterOpaque ? (base | PassRequests::SceneColor) : base;
	}

	AmbientOcclusionPass::AmbientOcclusionPass() {
		point = PassPoint::AfterPrepass;
	}
	AmbientOcclusionPass::~AmbientOcclusionPass() = default;

	void AmbientOcclusionPass::DeclarePlan(PassPlan& plan) {
		if (intensity <= 0.0f || radius <= 0.0f || falloffDistance <= 0.0f) {
			return;
		}

		const u16 divisor = downsample ? 2 : 1;

		const u8 estimate = plan.AddTarget(RHI::Format::R8Unorm, divisor);
		const u8 blurX = plan.AddTarget(RHI::Format::R8Unorm, divisor);
		const u8 blurY = plan.AddTarget(RHI::Format::R8Unorm, divisor);
		if (estimate == PASS_NO_READ || blurX == PASS_NO_READ || blurY == PASS_NO_READ) {
			return;
		}

		plan.AddStep(estimate);
		plan.AddStep(blurX, estimate);
		plan.AddStep(blurY, blurX);
		plan.AddStep(blurX, blurY);
		plan.AddStep(estimate, blurX);

		u8 result = estimate;
		if (downsample) {
			const u8 upsampled = plan.AddTarget(RHI::Format::R8Unorm, 1);
			if (upsampled == PASS_NO_READ) {
				return;
			}
			plan.AddStep(upsampled, estimate);
			result = upsampled;
		}

		if (afterOpaque && point < PassPoint::Opaque) {
			if (!_warnedPointDisagrees) {
				_warnedPointDisagrees = true;
				SLT_LOG_WARN("AmbientOcclusion",
							 "'{}' has afterOpaque set but runs at {} (opaque pass overwrites the composite). "
							 "Move the row to AfterOpaque (or clear afterOpaque to feed the shading)",
							 virtualPath, GetPassPointName(point));
			}
			plan.Produce(PassProduces::AmbientOcclusion, result);
			return;
		}

		if (afterOpaque) {
			plan.AddStep(PASS_SCENE_TARGET, result);
			return;
		}

		plan.Produce(PassProduces::AmbientOcclusion, result);
	}

	bool AmbientOcclusionPass::Cook(CookWriter& descriptor, CookWriter& payload) const {
		if (!RenderPass::Cook(descriptor, payload)) {
			return false;
		}
		descriptor.NoteAssetRef(AmbientOcclusionImpl::AmbientOcclusionShader);
		return true;
	}

	void AmbientOcclusionPass::Record(PassContext& context) {
		const u32 stepCount = context.GetStepCount();
		if (stepCount < FILTER_CHAIN_STEPS) {
			return;
		}

		if (!_shaderResolved) {
			_shaderResolved = true;
			if (Asset* asset = AssetDatabase::LoadAssetByGuid(AmbientOcclusionImpl::AmbientOcclusionShader)) {
				if (asset->GetAssetTypeId() == StringHash("ShaderAsset")) {
					_shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(asset));
				} else {
					AssetDatabase::UnloadAsset(asset);
				}
			}
		}
		if (!_shader.Get()) {
			if (!_warnedNoShader) {
				_warnedNoShader = true;
				SLT_LOG_WARN("AmbientOcclusion", "'{}' can't resolve engine://Shaders/AmbientOcclusion.hlsl (row draws nothing)",
							 virtualPath);
			}
			return;
		}

		AmbientOcclusionPush push{};

		const Mat4& view = context.GetView();
		for (u32 row = 0; row < 3; ++row) {
			for (u32 column = 0; column < 3; ++column) {
				push.viewRows[row][column] = view(row, column);
			}
		}

		const Mat4& projection = context.GetProjection();
		push.unproject[0] = 1.0f / projection(0, 0);
		push.unproject[1] = 1.0f / projection(1, 1);
		push.unproject[2] = -projection(0, 2) / projection(0, 0);
		push.unproject[3] = -projection(1, 2) / projection(1, 1);
		push.projHalfAndLinearize[0] = projection(0, 0) * 0.5f;
		push.projHalfAndLinearize[1] = projection(1, 1) * 0.5f;
		push.projHalfAndLinearize[2] = projection(2, 3);
		push.projHalfAndLinearize[3] = -projection(2, 2);

		const f32 clampedRadius = Math::Max(radius, 1.0e-4f);
		push.radiusFalloffIntensity[0] = clampedRadius;
		push.radiusFalloffIntensity[1] = Math::Max(falloffDistance, 1.0e-4f);
		const bool perspective = projection(3, 2) != 0.0f;
		if (!perspective && !_warnedOrthographic) {
			_warnedOrthographic = true;
			SLT_LOG_WARN("AmbientOcclusion", "'{}' is on an orthographic camera (not modeled, no occlusion)",
						 virtualPath);
		}
		push.radiusFalloffIntensity[2] = perspective ? Math::Max(intensity, 0.0f) : 0.0f;

		const u32 step = context.GetStep();
		AmbientOcclusionStep kind = AmbientOcclusionStep::Composite;
		if (step < FILTER_CHAIN_STEPS) {
			kind = FILTER_CHAIN[step];
		} else if (step == FILTER_CHAIN_STEPS && downsample) {
			kind = AmbientOcclusionStep::Upsample;
		}

		const Vec2I extent = context.GetExtent();
		push.extentStepSlices[0] = static_cast<f32>(extent.x);
		push.extentStepSlices[1] = static_cast<f32>(extent.y);
		push.extentStepSlices[2] = static_cast<f32>(static_cast<u32>(kind));
		push.extentStepSlices[3] = static_cast<f32>((Math::Clamp(directions, MIN_DIRECTIONS, MAX_DIRECTIONS) + 1) / 2);

		context.DrawProcedural(_shader,
							   std::span<const u8>(reinterpret_cast<const u8*>(&push), sizeof(push)),
							   3);
	}

} // namespace Shalltear
