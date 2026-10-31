#include "Shalltear/Core/Graphics/ObjectPass.h"

#include "Shalltear/Core/Math/MathUtil.h"

#include <cmath>

SLT_REGISTER_TYPE(Shalltear::ObjectPass)

namespace Shalltear {

	ObjectPass::~ObjectPass() = default;

	namespace {

		Mat4 RowProjection(Mat4 projection, f32 fieldOfViewDeg, f32 depthScale) {
			const bool perspective = projection(3, 2) != 0.0f;
			if (perspective && projection(1, 1) != 0.0f) {
				const f32 half = Math::Clamp(fieldOfViewDeg, 1.0f, 179.0f) * Math::DEG_TO_RAD * 0.5f;
				const f32 zoom = (std::cos(half) / std::sin(half)) / projection(1, 1);
				for (size_t col = 0; col < 4; ++col) {
					projection(0, col) *= zoom;
					projection(1, col) *= zoom;
				}
			}
			const f32 share = Math::Clamp(depthScale, 0.001f, 1.0f);
			for (size_t col = 0; col < 4; ++col) {
				projection(2, col) = share * projection(2, col) + (1.0f - share) * projection(3, col);
			}
			return projection;
		}

	} // anonymous namespace

	void ObjectPass::Record(PassContext& context) {
		if (!overrideCamera) {
			context.DrawObjects(renderingLayerMask, overrideMaterial);
			return;
		}
		context.DrawObjects(renderingLayerMask, overrideMaterial, RowProjection(context.GetProjection(), fieldOfViewDeg, depthScale));
	}

} // namespace Shalltear
