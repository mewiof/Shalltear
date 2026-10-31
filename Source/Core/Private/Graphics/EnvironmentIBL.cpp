#include "Graphics/EnvironmentIBL.h"

#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"

#include <cmath>
#include <cstring>

namespace Shalltear::GraphicsInternal {

	namespace {

		constexpr f32 PI = 3.14159265358979323846f;

		constexpr u32 PREFILTER_SAMPLES = 64;

		constexpr u32 SH_SOURCE_FACE_SIZE = 32;

		u32 MipFaceSize(u32 faceSize, u32 mip) {
			return Math::Max(faceSize >> mip, 1u);
		}

		struct WorkingChain {
			Vector<Vec3> texels{GetMemoryResource(MemTag::Graphics)};
			u32 faceSize = 0;
			u32 mipCount = 0;

			u64 FaceOffset(u32 mip, u32 face) const {
				u64 offset = 0;
				for (u32 m = 0; m < mip; ++m) {
					u64 side = MipFaceSize(faceSize, m);
					offset += side * side * CUBE_FACE_COUNT;
				}
				u64 side = MipFaceSize(faceSize, mip);
				return offset + side * side * face;
			}
		};

		u32 FullMipCount(u32 faceSize) {
			u32 count = 1;
			while ((faceSize >> count) >= 1 && (faceSize >> (count - 1)) > 1) {
				++count;
			}
			return count;
		}

		void DirectionToFace(const Vec3& direction, u32& outFace, f32& outU, f32& outV) {
			f32 absX = std::fabs(direction.x);
			f32 absY = std::fabs(direction.y);
			f32 absZ = std::fabs(direction.z);
			f32 sc = 0.0f;
			f32 tc = 0.0f;
			f32 ma = 0.0f;
			if (absX >= absY && absX >= absZ) {
				outFace = direction.x > 0.0f ? 0u : 1u;
				sc = direction.x > 0.0f ? -direction.z : direction.z;
				tc = -direction.y;
				ma = absX;
			} else if (absY >= absZ) {
				outFace = direction.y > 0.0f ? 2u : 3u;
				sc = direction.x;
				tc = direction.y > 0.0f ? direction.z : -direction.z;
				ma = absY;
			} else {
				outFace = direction.z > 0.0f ? 4u : 5u;
				sc = direction.z > 0.0f ? direction.x : -direction.x;
				tc = -direction.y;
				ma = absZ;
			}
			ma = Math::Max(ma, 1e-8f);
			outU = 0.5f * (sc / ma + 1.0f);
			outV = 0.5f * (tc / ma + 1.0f);
		}

		Vec3 SampleChain(const WorkingChain& chain, u32 mip, const Vec3& direction) {
			u32 face = 0;
			f32 u = 0.0f;
			f32 v = 0.0f;
			DirectionToFace(direction, face, u, v);

			const u32 side = MipFaceSize(chain.faceSize, mip);
			const Vec3* data = chain.texels.data() + chain.FaceOffset(mip, face);
			f32 x = Math::Clamp(u * static_cast<f32>(side) - 0.5f, 0.0f, static_cast<f32>(side) - 1.0f);
			f32 y = Math::Clamp(v * static_cast<f32>(side) - 0.5f, 0.0f, static_cast<f32>(side) - 1.0f);
			u32 x0 = static_cast<u32>(x);
			u32 y0 = static_cast<u32>(y);
			u32 x1 = Math::Min(x0 + 1, side - 1);
			u32 y1 = Math::Min(y0 + 1, side - 1);
			f32 fx = x - static_cast<f32>(x0);
			f32 fy = y - static_cast<f32>(y0);

			Vec3 top = data[y0 * side + x0] * (1.0f - fx) + data[y0 * side + x1] * fx;
			Vec3 bottom = data[y1 * side + x0] * (1.0f - fx) + data[y1 * side + x1] * fx;
			return top * (1.0f - fy) + bottom * fy;
		}

		void BuildWorkingChain(const RadianceCube& cube, WorkingChain& out) {
			out.faceSize = cube.faceSize;
			out.mipCount = FullMipCount(cube.faceSize);
			u64 total = 0;
			for (u32 mip = 0; mip < out.mipCount; ++mip) {
				u64 side = MipFaceSize(out.faceSize, mip);
				total += side * side * CUBE_FACE_COUNT;
			}
			out.texels.resize(static_cast<size_t>(total));

			for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
				const u16* source = reinterpret_cast<const u16*>(
					cube.pixels.data() + RadianceCubeFaceOffset(cube.faceSize, cube.mipCount, 0, face));
				Vec3* destination = out.texels.data() + out.FaceOffset(0, face);
				const u32 count = out.faceSize * out.faceSize;
				for (u32 i = 0; i < count; ++i) {
					destination[i] = Vec3(Math::HalfToFloat(source[i * 4 + 0]), Math::HalfToFloat(source[i * 4 + 1]),
										  Math::HalfToFloat(source[i * 4 + 2]));
				}
			}

			for (u32 mip = 1; mip < out.mipCount; ++mip) {
				const u32 side = MipFaceSize(out.faceSize, mip);
				const u32 parentSide = MipFaceSize(out.faceSize, mip - 1);
				for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
					const Vec3* parent = out.texels.data() + out.FaceOffset(mip - 1, face);
					Vec3* destination = out.texels.data() + out.FaceOffset(mip, face);
					for (u32 y = 0; y < side; ++y) {
						for (u32 x = 0; x < side; ++x) {
							u32 px = Math::Min(x * 2, parentSide - 1);
							u32 py = Math::Min(y * 2, parentSide - 1);
							u32 px1 = Math::Min(px + 1, parentSide - 1);
							u32 py1 = Math::Min(py + 1, parentSide - 1);
							destination[y * side + x] = (parent[py * parentSide + px] + parent[py * parentSide + px1] +
														 parent[py1 * parentSide + px] + parent[py1 * parentSide + px1]) *
														0.25f;
						}
					}
				}
			}
		}

		Vec2 Hammersley(u32 i, u32 count) {
			u32 bits = i;
			bits = (bits << 16u) | (bits >> 16u);
			bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
			bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
			bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
			bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
			return Vec2(static_cast<f32>(i) / static_cast<f32>(count), static_cast<f32>(bits) * 2.3283064365386963e-10f);
		}

		Vec3 ImportanceSampleGGX(const Vec2& xi, f32 linearRoughness, const Vec3& normal) {
			const f32 a = linearRoughness;
			const f32 phi = 2.0f * PI * xi.x;
			const f32 cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
			const f32 sinTheta = std::sqrt(Math::Max(0.0f, 1.0f - cosTheta * cosTheta));

			const Vec3 halfTangent(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);
			const Vec3 up = std::fabs(normal.z) < 0.999f ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(1.0f, 0.0f, 0.0f);
			const Vec3 tangentX = Vec3::Cross(up, normal).Normalized();
			const Vec3 tangentY = Vec3::Cross(normal, tangentX);
			return (tangentX * halfTangent.x + tangentY * halfTangent.y + normal * halfTangent.z).Normalized();
		}

		f32 DistributionGGX(f32 NoH, f32 linearRoughness) {
			const f32 a2 = linearRoughness * linearRoughness;
			const f32 denominator = NoH * NoH * (a2 - 1.0f) + 1.0f;
			return a2 / Math::Max(PI * denominator * denominator, 1e-8f);
		}

		void PrefilterFace(const WorkingChain& chain, RadianceCube& cube, u32 mip, u32 face, f32 roughness) {
			const u32 side = MipFaceSize(cube.faceSize, mip);
			const f32 linearRoughness = Math::Max(roughness * roughness, 1e-3f);
			const f32 texelSolidAngle = 4.0f * PI / (CUBE_FACE_COUNT * static_cast<f32>(chain.faceSize) * static_cast<f32>(chain.faceSize));
			u16* destination = reinterpret_cast<u16*>(cube.pixels.data() + RadianceCubeFaceOffset(cube.faceSize, cube.mipCount, mip, face));

			for (u32 y = 0; y < side; ++y) {
				for (u32 x = 0; x < side; ++x) {
					const Vec3 normal = CubeTexelDirection(face, (static_cast<f32>(x) + 0.5f) / static_cast<f32>(side),
														   (static_cast<f32>(y) + 0.5f) / static_cast<f32>(side));
					Vec3 sum(0.0f, 0.0f, 0.0f);
					f32 weight = 0.0f;
					for (u32 s = 0; s < PREFILTER_SAMPLES; ++s) {
						const Vec3 half = ImportanceSampleGGX(Hammersley(s, PREFILTER_SAMPLES), linearRoughness, normal);
						const f32 VoH = Vec3::Dot(normal, half);
						const Vec3 light = half * (2.0f * VoH) - normal;
						const f32 NoL = Vec3::Dot(normal, light);
						if (NoL <= 0.0f) {
							continue;
						}
						const f32 NoH = Math::Max(VoH, 0.0f);
						const f32 pdf = DistributionGGX(NoH, linearRoughness) * 0.25f;
						const f32 sampleSolidAngle = 1.0f / (static_cast<f32>(PREFILTER_SAMPLES) * pdf + 1e-4f);
						const f32 sampleMip = 0.5f * std::log2(sampleSolidAngle / texelSolidAngle);
						const u32 readMip = static_cast<u32>(Math::Clamp(sampleMip, 0.0f, static_cast<f32>(chain.mipCount - 1)));
						sum += SampleChain(chain, readMip, light) * NoL;
						weight += NoL;
					}
					const Vec3 color = weight > 0.0f ? sum * (1.0f / weight) : SampleChain(chain, 0, normal);
					u16* texel = destination + (static_cast<u64>(y) * side + x) * 4;
					texel[0] = Math::FloatToHalf(color.x);
					texel[1] = Math::FloatToHalf(color.y);
					texel[2] = Math::FloatToHalf(color.z);
					texel[3] = Math::FloatToHalf(1.0f);
				}
			}
		}

		void EvaluateBasis(const Vec3& d, f32 basis[9]) {
			basis[0] = 0.282095f;
			basis[1] = 0.488603f * d.y;
			basis[2] = 0.488603f * d.z;
			basis[3] = 0.488603f * d.x;
			basis[4] = 1.092548f * d.x * d.y;
			basis[5] = 1.092548f * d.y * d.z;
			basis[6] = 0.315392f * (3.0f * d.z * d.z - 1.0f);
			basis[7] = 1.092548f * d.x * d.z;
			basis[8] = 0.546274f * (d.x * d.x - d.y * d.y);
		}

		u32 SourceMipOf(u32 faceSize, u32 mipCount) {
			u32 mip = 0;
			while (mip + 1 < mipCount && MipFaceSize(faceSize, mip + 1) >= SH_SOURCE_FACE_SIZE) {
				++mip;
			}
			return mip;
		}

		template <typename TexelFn>
		void ProjectFaces(u32 side, const TexelFn& texel, IrradianceSH& out) {
			const f32 inverseSide = 1.0f / static_cast<f32>(side);

			Vec3 raw[9]{};
			f32 totalSolidAngle = 0.0f;
			for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
				for (u32 y = 0; y < side; ++y) {
					for (u32 x = 0; x < side; ++x) {
						const f32 u = (static_cast<f32>(x) + 0.5f) * inverseSide;
						const f32 v = (static_cast<f32>(y) + 0.5f) * inverseSide;
						const Vec3 direction = CubeTexelDirection(face, u, v);
						const f32 s = 2.0f * u - 1.0f;
						const f32 t = 2.0f * v - 1.0f;
						const f32 temp = 1.0f + s * s + t * t;
						const f32 solidAngle = 4.0f * inverseSide * inverseSide / (temp * std::sqrt(temp));
						totalSolidAngle += solidAngle;

						f32 basis[9];
						EvaluateBasis(direction, basis);
						const Vec3 radiance = texel(face, x, y);
						for (u32 i = 0; i < 9; ++i) {
							raw[i] += radiance * (basis[i] * solidAngle);
						}
					}
				}
			}
			(void)totalSolidAngle;

			const f32 a0 = 3.141593f;
			const f32 a1 = 2.094395f;
			const f32 a2 = 0.785398f;
			const f32 convolution[9] = {a0, a1, a1, a1, a2, a2, a2, a2, a2};
			for (u32 i = 0; i < 9; ++i) {
				out.coefficients[i] = raw[i] * (convolution[i] / PI);
			}
		}

		void ProjectIrradiance(const WorkingChain& chain, IrradianceSH& out) {
			const u32 mip = SourceMipOf(chain.faceSize, chain.mipCount);
			const u32 side = MipFaceSize(chain.faceSize, mip);
			ProjectFaces(
				side, [&](u32 face, u32 x, u32 y) { return chain.texels[chain.FaceOffset(mip, face) + static_cast<u64>(y) * side + x]; },
				out);
		}

		struct SourceImage {
			std::span<const u8> bytes;
			i32 width = 0;
			i32 height = 0;
			bool isHalf = false;
			bool decodeSRGB = false;

			bool Open(std::span<const u8> source, Vec2I size, RHI::Format format) {
				if (size.x <= 0 || size.y <= 0) {
					return false;
				}
				isHalf = format == RHI::Format::RGBA16Float;
				const bool isByte = format == RHI::Format::RGBA8Unorm || format == RHI::Format::RGBA8SRGB;
				if (!isHalf && !isByte) {
					return false;
				}
				const u64 texelBytes = isHalf ? 8u : 4u;
				if (source.size() < static_cast<u64>(size.x) * size.y * texelBytes) {
					return false;
				}
				bytes = source;
				width = size.x;
				height = size.y;
				decodeSRGB = format == RHI::Format::RGBA8SRGB;
				return true;
			}

			Vec3 Texel(i32 x, i32 y) const {
				const u64 index = static_cast<u64>(y) * width + x;
				if (isHalf) {
					const u16* texels = reinterpret_cast<const u16*>(bytes.data());
					return Vec3(Math::HalfToFloat(texels[index * 4 + 0]), Math::HalfToFloat(texels[index * 4 + 1]),
								Math::HalfToFloat(texels[index * 4 + 2]));
				}
				const u8* texels = bytes.data();
				auto channel = [&](u32 offset) {
					const f32 value = static_cast<f32>(texels[index * 4 + offset]) / 255.0f;
					if (!decodeSRGB) {
						return value;
					}
					return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
				};
				return Vec3(channel(0), channel(1), channel(2));
			}
		};

		void WriteRadianceTexel(u16* texel, const Vec3& color) {
			texel[0] = Math::FloatToHalf(color.x);
			texel[1] = Math::FloatToHalf(color.y);
			texel[2] = Math::FloatToHalf(color.z);
			texel[3] = Math::FloatToHalf(1.0f);
		}

	} // anonymous namespace

	u32 RadianceCubeMipCount(u32 faceSize) {
		u32 count = 1;
		while (count < IBL_MIP_COUNT && MipFaceSize(faceSize, count) >= IBL_MIN_FACE_SIZE) {
			++count;
		}
		return count;
	}

	u64 RadianceCubeBytes(u32 faceSize, u32 mipCount) {
		u64 total = 0;
		for (u32 mip = 0; mip < mipCount; ++mip) {
			const u64 side = MipFaceSize(faceSize, mip);
			total += side * side * CUBE_FACE_COUNT;
		}
		return total * 8;
	}

	u64 RadianceCubeFaceOffset(u32 faceSize, u32 mipCount, u32 mip, u32 face) {
		u64 offset = 0;
		for (u32 m = 0; m < mip && m < mipCount; ++m) {
			const u64 side = MipFaceSize(faceSize, m);
			offset += side * side * CUBE_FACE_COUNT;
		}
		const u64 side = MipFaceSize(faceSize, mip);
		return (offset + side * side * face) * 8;
	}

	Vec3 CubeTexelDirection(u32 face, f32 u, f32 v) {
		const f32 s = 2.0f * u - 1.0f;
		const f32 t = 2.0f * v - 1.0f;
		Vec3 direction;
		switch (face) {
		case 0:
			direction = Vec3(1.0f, -t, -s);
			break;
		case 1:
			direction = Vec3(-1.0f, -t, s);
			break;
		case 2:
			direction = Vec3(s, 1.0f, t);
			break;
		case 3:
			direction = Vec3(s, -1.0f, -t);
			break;
		case 4:
			direction = Vec3(s, -t, 1.0f);
			break;
		default:
			direction = Vec3(-s, -t, -1.0f);
			break;
		}
		return direction.Normalized();
	}

	Vec3 EvaluateSkyGradient(const SkyGradient& gradient, const Vec3& direction) {
		if (gradient.flat) {
			return gradient.sky;
		}
		const f32 up = Math::Clamp(direction.y, -1.0f, 1.0f);
		if (up >= 0.0f) {
			return gradient.equator + (gradient.sky - gradient.equator) * up;
		}
		return gradient.equator + (gradient.ground - gradient.equator) * (-up);
	}

	void BuildCubeFromGradient(const SkyGradient& gradient, u32 faceSize, RadianceCube& out) {
		if (faceSize == 0) {
			return;
		}
		out.faceSize = faceSize;
		out.mipCount = RadianceCubeMipCount(faceSize);
		out.pixels.resize(static_cast<size_t>(RadianceCubeBytes(faceSize, out.mipCount)));
		for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
			u16* destination = reinterpret_cast<u16*>(out.pixels.data() + RadianceCubeFaceOffset(faceSize, out.mipCount, 0, face));
			for (u32 y = 0; y < faceSize; ++y) {
				for (u32 x = 0; x < faceSize; ++x) {
					const Vec3 direction = CubeTexelDirection(face, (static_cast<f32>(x) + 0.5f) / static_cast<f32>(faceSize),
															  (static_cast<f32>(y) + 0.5f) / static_cast<f32>(faceSize));
					const Vec3 color = EvaluateSkyGradient(gradient, direction);
					u16* texel = destination + (static_cast<u64>(y) * faceSize + x) * 4;
					texel[0] = Math::FloatToHalf(color.x);
					texel[1] = Math::FloatToHalf(color.y);
					texel[2] = Math::FloatToHalf(color.z);
					texel[3] = Math::FloatToHalf(1.0f);
				}
			}
		}
	}

	bool BuildCubeFromEquirect(std::span<const u8> source, Vec2I sourceSize, RHI::Format sourceFormat,
							   u32 faceSize, RadianceCube& out) {
		SourceImage image;
		if (faceSize == 0 || !image.Open(source, sourceSize, sourceFormat)) {
			return false;
		}

		out.faceSize = faceSize;
		out.mipCount = RadianceCubeMipCount(faceSize);
		out.pixels.resize(static_cast<size_t>(RadianceCubeBytes(faceSize, out.mipCount)));

		const i32 width = sourceSize.x;
		const i32 height = sourceSize.y;
		auto fetch = [&](i32 x, i32 y) -> Vec3 {
			x = ((x % width) + width) % width;
			y = Math::Clamp(y, 0, height - 1);
			return image.Texel(x, y);
		};

		for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
			u16* destination = reinterpret_cast<u16*>(out.pixels.data() + RadianceCubeFaceOffset(faceSize, out.mipCount, 0, face));
			for (u32 y = 0; y < faceSize; ++y) {
				for (u32 x = 0; x < faceSize; ++x) {
					const Vec3 direction = CubeTexelDirection(face, (static_cast<f32>(x) + 0.5f) / static_cast<f32>(faceSize),
															  (static_cast<f32>(y) + 0.5f) / static_cast<f32>(faceSize));
					// image center faces +Z
					const f32 u = 0.5f + std::atan2(direction.x, direction.z) / (2.0f * PI);
					const f32 v = std::acos(Math::Clamp(direction.y, -1.0f, 1.0f)) / PI;

					f32 fx = u * static_cast<f32>(width) - 0.5f;
					f32 fy = v * static_cast<f32>(height) - 0.5f;
					i32 x0 = static_cast<i32>(std::floor(fx));
					i32 y0 = static_cast<i32>(std::floor(fy));
					f32 tx = fx - static_cast<f32>(x0);
					f32 ty = fy - static_cast<f32>(y0);
					Vec3 top = fetch(x0, y0) * (1.0f - tx) + fetch(x0 + 1, y0) * tx;
					Vec3 bottom = fetch(x0, y0 + 1) * (1.0f - tx) + fetch(x0 + 1, y0 + 1) * tx;
					Vec3 color = top * (1.0f - ty) + bottom * ty;
					WriteRadianceTexel(destination + (static_cast<u64>(y) * faceSize + x) * 4, color);
				}
			}
		}
		return true;
	}

	u32 StripFaceSize(Vec2I sourceSize) {
		if (sourceSize.x <= 0 || sourceSize.y <= 0) {
			return 0;
		}
		if (sourceSize.x == sourceSize.y * static_cast<i32>(CUBE_FACE_COUNT)) {
			return static_cast<u32>(sourceSize.y);
		}
		if (sourceSize.y == sourceSize.x * static_cast<i32>(CUBE_FACE_COUNT)) {
			return static_cast<u32>(sourceSize.x);
		}
		return 0;
	}

	bool BuildCubeFromStrip(std::span<const u8> source, Vec2I sourceSize, RHI::Format sourceFormat,
							u32 faceSize, RadianceCube& out) {
		const u32 sourceFace = StripFaceSize(sourceSize);
		SourceImage image;
		if (faceSize == 0 || sourceFace == 0 || !image.Open(source, sourceSize, sourceFormat)) {
			return false;
		}
		const bool horizontal = sourceSize.x > sourceSize.y;
		const i32 side = static_cast<i32>(sourceFace);

		out.faceSize = faceSize;
		out.mipCount = RadianceCubeMipCount(faceSize);
		out.pixels.resize(static_cast<size_t>(RadianceCubeBytes(faceSize, out.mipCount)));

		for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
			const i32 originX = horizontal ? static_cast<i32>(face) * side : 0;
			const i32 originY = horizontal ? 0 : static_cast<i32>(face) * side;
			u16* destination = reinterpret_cast<u16*>(out.pixels.data() + RadianceCubeFaceOffset(faceSize, out.mipCount, 0, face));
			for (u32 y = 0; y < faceSize; ++y) {
				for (u32 x = 0; x < faceSize; ++x) {
					Vec3 color;
					if (faceSize == sourceFace) {
						color = image.Texel(originX + static_cast<i32>(x), originY + static_cast<i32>(y));
					} else {
						const f32 scale = static_cast<f32>(side) / static_cast<f32>(faceSize);
						const f32 fx = Math::Clamp((static_cast<f32>(x) + 0.5f) * scale - 0.5f, 0.0f, static_cast<f32>(side - 1));
						const f32 fy = Math::Clamp((static_cast<f32>(y) + 0.5f) * scale - 0.5f, 0.0f, static_cast<f32>(side - 1));
						const i32 x0 = static_cast<i32>(fx);
						const i32 y0 = static_cast<i32>(fy);
						const i32 x1 = Math::Min(x0 + 1, side - 1);
						const i32 y1 = Math::Min(y0 + 1, side - 1);
						const f32 tx = fx - static_cast<f32>(x0);
						const f32 ty = fy - static_cast<f32>(y0);
						const Vec3 top = image.Texel(originX + x0, originY + y0) * (1.0f - tx) + image.Texel(originX + x1, originY + y0) * tx;
						const Vec3 bottom = image.Texel(originX + x0, originY + y1) * (1.0f - tx) + image.Texel(originX + x1, originY + y1) * tx;
						color = top * (1.0f - ty) + bottom * ty;
					}
					WriteRadianceTexel(destination + (static_cast<u64>(y) * faceSize + x) * 4, color);
				}
			}
		}
		return true;
	}

	void PrefilterRadianceCube(RadianceCube& cube, IrradianceSH& outSH) {
		if (!cube.IsValid()) {
			outSH = IrradianceSH{};
			return;
		}
		WorkingChain chain;
		BuildWorkingChain(cube, chain);
		ProjectIrradiance(chain, outSH);

		const bool parallel = JobSystem::GetWorkerCount() > 0;
		for (u32 mip = 1; mip < cube.mipCount; ++mip) {
			const f32 roughness = static_cast<f32>(mip) / static_cast<f32>(cube.mipCount - 1);
			if (!parallel) {
				for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
					PrefilterFace(chain, cube, mip, face, roughness);
				}
				continue;
			}
			JobHandle handles[CUBE_FACE_COUNT];
			const WorkingChain* chainPtr = &chain;
			RadianceCube* cubePtr = &cube;
			for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
				handles[face] = JobSystem::Submit([chainPtr, cubePtr, mip, face, roughness]() {
					PrefilterFace(*chainPtr, *cubePtr, mip, face, roughness);
				});
			}
			JobSystem::WaitForAll(std::span<const JobHandle>(handles, CUBE_FACE_COUNT));
		}
	}

	u32 BoxChainMipCount(u32 faceSize) {
		return FullMipCount(faceSize);
	}

	u32 IrradianceSourceMip(u32 faceSize) {
		return SourceMipOf(faceSize, FullMipCount(faceSize));
	}

	void ProjectIrradianceFromFaces(std::span<const u8> faces, u32 side, IrradianceSH& outSH) {
		const u64 faceTexels = static_cast<u64>(side) * side;
		if (side == 0 || faces.size() < faceTexels * CUBE_FACE_COUNT * 8) {
			outSH = IrradianceSH{};
			return;
		}
		const u16* halves = reinterpret_cast<const u16*>(faces.data());
		ProjectFaces(
			side,
			[&](u32 face, u32 x, u32 y) {
				const u16* texel = halves + (face * faceTexels + static_cast<u64>(y) * side + x) * 4;
				return Vec3(Math::HalfToFloat(texel[0]), Math::HalfToFloat(texel[1]), Math::HalfToFloat(texel[2]));
			},
			outSH);
	}

	Vec3 EvaluateIrradianceSH(const IrradianceSH& sh, const Vec3& normal) {
		f32 basis[9];
		EvaluateBasis(normal, basis);
		Vec3 result(0.0f, 0.0f, 0.0f);
		for (u32 i = 0; i < 9; ++i) {
			result += sh.coefficients[i] * basis[i];
		}
		return Vec3(Math::Max(result.x, 0.0f), Math::Max(result.y, 0.0f), Math::Max(result.z, 0.0f));
	}

} // namespace Shalltear::GraphicsInternal
