#include "Shalltear/Animation/AnimatorController.h"

#include "Shalltear/Core/Math/MathUtil.h"

#include <cmath>

namespace Shalltear {

	namespace AnimatorBlend {

		namespace {

			f32 Band(f32 ipX, f32 ipY, f32 ijX, f32 ijY) {
				const f32 denom = ijX * ijX + ijY * ijY;
				if (!(denom > 0.0f)) {
					return 1.0f;
				}
				return 1.0f - (ipX * ijX + ipY * ijY) / denom;
			}

			f32 SignedAngle(f32 ax, f32 ay, f32 bx, f32 by) {
				const f32 lenA = Math::Sqrt(ax * ax + ay * ay);
				const f32 lenB = Math::Sqrt(bx * bx + by * by);
				if (!(lenA > 0.0f) || !(lenB > 0.0f)) {
					return 0.0f;
				}
				return std::atan2(ax * by - ay * bx, ax * bx + ay * by);
			}

			f32 RadialDelta(f32 lenFrom, f32 lenTo) {
				const f32 mean = (lenFrom + lenTo) * 0.5f;
				return mean > 0.0f ? (lenTo - lenFrom) / mean : 0.0f;
			}

			constexpr f32 DIRECTIONAL_ANGLE_SCALE = 2.0f;

			u32 Normalize(f32* out, u32 count, u32 nearest) {
				f32 total = 0.0f;
				for (u32 index = 0; index < count; ++index) {
					out[index] = Math::Max(out[index], 0.0f);
					total += out[index];
				}
				if (!(total > 0.0f)) {
					for (u32 index = 0; index < count; ++index) {
						out[index] = index == nearest ? 1.0f : 0.0f;
					}
					return count;
				}
				for (u32 index = 0; index < count; ++index) {
					out[index] /= total;
				}
				return count;
			}

		} // anonymous namespace

		u32 Weights(const AnimatorController& controller, i32 node, Vec2 sample, const f32* values, f32* out, u32 capacity) {
			if (node < 0 || static_cast<size_t>(node) >= controller.nodes.size() || !out) {
				return 0;
			}
			const AnimatorController::MotionNode& tree = controller.nodes[static_cast<size_t>(node)];
			const u32 count = Math::Min(static_cast<u32>(tree.children.size()), capacity);
			if (count == 0 || tree.kind == AnimatorBlendKind::Clip) {
				return 0;
			}
			if (count == 1 && tree.kind != AnimatorBlendKind::Direct) {
				out[0] = 1.0f;
				return 1;
			}

			u32 nearest = 0;
			f32 nearestDistance = 0.0f;
			for (u32 index = 0; index < count; ++index) {
				const Vec2 at = tree.children[index].position;
				const f32 distance = (at.x - sample.x) * (at.x - sample.x) + (at.y - sample.y) * (at.y - sample.y);
				if (index == 0 || distance < nearestDistance) {
					nearestDistance = distance;
					nearest = index;
				}
			}

			switch (tree.kind) {
			case AnimatorBlendKind::Simple1D: {
				for (u32 index = 0; index < count; ++index) {
					out[index] = 0.0f;
				}
				i32 below = -1;
				i32 above = -1;
				for (u32 index = 0; index < count; ++index) {
					const f32 at = tree.children[index].position.x;
					if (at <= sample.x && (below < 0 || at > tree.children[static_cast<size_t>(below)].position.x)) {
						below = static_cast<i32>(index);
					}
					if (at >= sample.x && (above < 0 || at < tree.children[static_cast<size_t>(above)].position.x)) {
						above = static_cast<i32>(index);
					}
				}
				if (below < 0 && above < 0) {
					out[nearest] = 1.0f;
					return count;
				}
				if (below < 0) {
					out[above] = 1.0f;
					return count;
				}
				if (above < 0 || below == above) {
					out[below] = 1.0f;
					return count;
				}
				const f32 low = tree.children[static_cast<size_t>(below)].position.x;
				const f32 high = tree.children[static_cast<size_t>(above)].position.x;
				const f32 t = high > low ? (sample.x - low) / (high - low) : 0.0f;
				out[below] = 1.0f - t;
				out[above] = t;
				return count;
			}
			case AnimatorBlendKind::FreeformCartesian2D: {
				for (u32 index = 0; index < count; ++index) {
					const Vec2 own = tree.children[index].position;
					f32 weight = 1.0f;
					for (u32 other = 0; other < count; ++other) {
						if (other == index) {
							continue;
						}
						const Vec2 to = tree.children[other].position;
						weight = Math::Min(weight, Band(sample.x - own.x, sample.y - own.y, to.x - own.x, to.y - own.y));
					}
					out[index] = weight;
				}
				return Normalize(out, count, nearest);
			}
			case AnimatorBlendKind::FreeformDirectional2D: {
				const f32 lenSample = Math::Sqrt(sample.x * sample.x + sample.y * sample.y);
				for (u32 index = 0; index < count; ++index) {
					const Vec2 own = tree.children[index].position;
					const f32 lenOwn = Math::Sqrt(own.x * own.x + own.y * own.y);
					const f32 radialToSample = RadialDelta(lenOwn, lenSample);
					const f32 angleToSample = SignedAngle(own.x, own.y, sample.x, sample.y) * DIRECTIONAL_ANGLE_SCALE;
					f32 weight = 1.0f;
					for (u32 other = 0; other < count; ++other) {
						if (other == index) {
							continue;
						}
						const Vec2 to = tree.children[other].position;
						const f32 lenTo = Math::Sqrt(to.x * to.x + to.y * to.y);
						const f32 radialToOther = RadialDelta(lenOwn, lenTo);
						const f32 angleToOther = SignedAngle(own.x, own.y, to.x, to.y) * DIRECTIONAL_ANGLE_SCALE;
						weight = Math::Min(weight, Band(radialToSample, angleToSample, radialToOther, angleToOther));
					}
					out[index] = weight;
				}
				return Normalize(out, count, nearest);
			}
			case AnimatorBlendKind::Direct: {
				for (u32 index = 0; index < count; ++index) {
					out[index] = values ? Math::Max(values[index], 0.0f) : 0.0f;
				}
				f32 total = 0.0f;
				for (u32 index = 0; index < count; ++index) {
					total += out[index];
				}
				if (!(total > 0.0f)) {
					return count;
				}
				for (u32 index = 0; index < count; ++index) {
					out[index] /= total;
				}
				return count;
			}
			case AnimatorBlendKind::Clip:
			default:
				return 0;
			}
		}

	} // namespace AnimatorBlend

} // namespace Shalltear
