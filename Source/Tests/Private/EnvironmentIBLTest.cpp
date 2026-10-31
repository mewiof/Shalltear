#include "TestsPCH.h"

#include "Graphics/EnvironmentIBL.h"

#include <cmath>
#include <vector>

using namespace Shalltear;
using Shalltear::GraphicsInternal::BuildCubeFromEquirect;
using Shalltear::GraphicsInternal::BuildCubeFromGradient;
using Shalltear::GraphicsInternal::BuildCubeFromStrip;
using Shalltear::GraphicsInternal::CUBE_FACE_COUNT;
using Shalltear::GraphicsInternal::CubeTexelDirection;
using Shalltear::GraphicsInternal::EvaluateIrradianceSH;
using Shalltear::GraphicsInternal::EvaluateSkyGradient;
using Shalltear::GraphicsInternal::IBL_MIP_COUNT;
using Shalltear::GraphicsInternal::IrradianceSH;
using Shalltear::GraphicsInternal::PrefilterRadianceCube;
using Shalltear::GraphicsInternal::RadianceCube;
using Shalltear::GraphicsInternal::RadianceCubeBytes;
using Shalltear::GraphicsInternal::RadianceCubeFaceOffset;
using Shalltear::GraphicsInternal::RadianceCubeMipCount;
using Shalltear::GraphicsInternal::SkyGradient;
using Shalltear::GraphicsInternal::StripFaceSize;

namespace {

	f32 ReadTexel(const RadianceCube& cube, u32 mip, u32 face, u32 x, u32 y, u32 channel) {
		const u32 side = std::max(cube.faceSize >> mip, 1u);
		const u8* bytes = cube.pixels.data() + RadianceCubeFaceOffset(cube.faceSize, cube.mipCount, mip, face);
		const u16* texels = reinterpret_cast<const u16*>(bytes);
		return Shalltear::Math::HalfToFloat(texels[(static_cast<size_t>(y) * side + x) * 4 + channel]);
	}

	SkyGradient FlatSky(const Vec3& color) {
		SkyGradient gradient;
		gradient.sky = color;
		gradient.equator = color;
		gradient.ground = color;
		gradient.flat = true;
		return gradient;
	}

} // anonymous namespace

TEST(EnvironmentIBLTest, FaceOffsetsTileChain) {
	constexpr u32 faceSize = 32;
	const u32 mipCount = RadianceCubeMipCount(faceSize);
	const u64 total = RadianceCubeBytes(faceSize, mipCount);

	u64 expected = 0;
	for (u32 mip = 0; mip < mipCount; ++mip) {
		const u32 side = std::max(faceSize >> mip, 1u);
		for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
			EXPECT_EQ(RadianceCubeFaceOffset(faceSize, mipCount, mip, face), expected)
				<< "mip " << mip << " face " << face;
			expected += static_cast<u64>(side) * side * 8;
		}
	}
	EXPECT_EQ(expected, total);
}

TEST(EnvironmentIBLTest, MipCountStopsAtStepsOrFace) {
	EXPECT_EQ(RadianceCubeMipCount(256u), IBL_MIP_COUNT);
	EXPECT_EQ(RadianceCubeMipCount(64u), 4u);
	EXPECT_EQ(RadianceCubeMipCount(32u), 3u);
	EXPECT_EQ(RadianceCubeMipCount(8u), 1u);
}

TEST(EnvironmentIBLTest, FaceCentersLookAlongTheSixAxes) {
	const Vec3 expected[CUBE_FACE_COUNT] = {
		Vec3(1.0f, 0.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f),
		Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f),
		Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, -1.0f)};
	for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
		const Vec3 direction = CubeTexelDirection(face, 0.5f, 0.5f);
		EXPECT_NEAR(Vec3::Dot(direction, expected[face]), 1.0f, 1e-5f) << "face " << face;
	}
}

TEST(EnvironmentIBLTest, UniformRadianceGivesUniformIrradiance) {
	const Vec3 radiance(0.5f, 0.25f, 0.125f);
	RadianceCube cube;
	BuildCubeFromGradient(FlatSky(radiance), 32, cube);
	ASSERT_TRUE(cube.IsValid());

	IrradianceSH sh;
	PrefilterRadianceCube(cube, sh);

	const Vec3 directions[] = {Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f),
							   Vec3(0.577f, 0.577f, 0.577f), Vec3(-0.577f, 0.577f, -0.577f)};
	for (const Vec3& direction : directions) {
		const Vec3 irradiance = EvaluateIrradianceSH(sh, direction.Normalized());
		EXPECT_NEAR(irradiance.x, radiance.x, 0.01f);
		EXPECT_NEAR(irradiance.y, radiance.y, 0.01f);
		EXPECT_NEAR(irradiance.z, radiance.z, 0.01f);
	}
}

TEST(EnvironmentIBLTest, DirectionalSkyLightsUpAndDownDifferently) {
	SkyGradient gradient;
	gradient.sky = Vec3(1.0f, 1.0f, 1.0f);
	gradient.equator = Vec3(0.5f, 0.5f, 0.5f);
	gradient.ground = Vec3(0.0f, 0.0f, 0.0f);

	RadianceCube cube;
	BuildCubeFromGradient(gradient, 32, cube);
	IrradianceSH sh;
	PrefilterRadianceCube(cube, sh);

	const Vec3 up = EvaluateIrradianceSH(sh, Vec3(0.0f, 1.0f, 0.0f));
	const Vec3 down = EvaluateIrradianceSH(sh, Vec3(0.0f, -1.0f, 0.0f));
	EXPECT_GT(up.x, down.x + 0.2f);
	EXPECT_GE(down.x, 0.0f);
}

TEST(EnvironmentIBLTest, GradientHitsThreeStops) {
	SkyGradient gradient;
	gradient.sky = Vec3(1.0f, 0.0f, 0.0f);
	gradient.equator = Vec3(0.0f, 1.0f, 0.0f);
	gradient.ground = Vec3(0.0f, 0.0f, 1.0f);

	EXPECT_NEAR(EvaluateSkyGradient(gradient, Vec3(0.0f, 1.0f, 0.0f)).x, 1.0f, 1e-5f);
	EXPECT_NEAR(EvaluateSkyGradient(gradient, Vec3(1.0f, 0.0f, 0.0f)).y, 1.0f, 1e-5f);
	EXPECT_NEAR(EvaluateSkyGradient(gradient, Vec3(0.0f, -1.0f, 0.0f)).z, 1.0f, 1e-5f);
	gradient.flat = true;
	EXPECT_NEAR(EvaluateSkyGradient(gradient, Vec3(0.0f, -1.0f, 0.0f)).x, 1.0f, 1e-5f);
}

TEST(EnvironmentIBLTest, UniformSkySameAtEveryRoughness) {
	const Vec3 radiance(0.75f, 0.5f, 0.25f);
	RadianceCube cube;
	BuildCubeFromGradient(FlatSky(radiance), 64, cube);
	IrradianceSH sh;
	PrefilterRadianceCube(cube, sh);

	ASSERT_GT(cube.mipCount, 1u);
	for (u32 mip = 0; mip < cube.mipCount; ++mip) {
		for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
			EXPECT_NEAR(ReadTexel(cube, mip, face, 0, 0, 0), radiance.x, 0.005f) << "mip " << mip << " face " << face;
			EXPECT_NEAR(ReadTexel(cube, mip, face, 0, 0, 1), radiance.y, 0.005f) << "mip " << mip << " face " << face;
			EXPECT_NEAR(ReadTexel(cube, mip, face, 0, 0, 2), radiance.z, 0.005f) << "mip " << mip << " face " << face;
		}
	}
}

TEST(EnvironmentIBLTest, MipZeroKeepsTheSharpRadiance) {
	SkyGradient gradient;
	gradient.sky = Vec3(1.0f, 1.0f, 1.0f);
	gradient.equator = Vec3(0.0f, 0.0f, 0.0f);
	gradient.ground = Vec3(0.0f, 0.0f, 0.0f);

	RadianceCube cube;
	BuildCubeFromGradient(gradient, 32, cube);
	IrradianceSH sh;
	PrefilterRadianceCube(cube, sh);

	EXPECT_NEAR(ReadTexel(cube, 0, 2, 16, 16, 0), 1.0f, 0.02f);
	EXPECT_LT(ReadTexel(cube, cube.mipCount - 1, 2, 0, 0, 0), 0.95f);
}

TEST(EnvironmentIBLTest, TheEquirectCenterColumnFacesForward) {
	constexpr i32 width = 64;
	constexpr i32 height = 32;
	std::vector<u16> source(static_cast<size_t>(width) * height * 4, 0);
	const Vec3 bands[4] = {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f), Vec3(1.0f, 1.0f, 0.0f)};
	for (i32 y = 0; y < height; ++y) {
		for (i32 x = 0; x < width; ++x) {
			const Vec3& band = bands[(x * 4) / width];
			const size_t index = (static_cast<size_t>(y) * width + x) * 4;
			source[index + 0] = Shalltear::Math::FloatToHalf(band.x);
			source[index + 1] = Shalltear::Math::FloatToHalf(band.y);
			source[index + 2] = Shalltear::Math::FloatToHalf(band.z);
			source[index + 3] = Shalltear::Math::FloatToHalf(1.0f);
		}
	}

	RadianceCube cube;
	ASSERT_TRUE(BuildCubeFromEquirect({reinterpret_cast<const u8*>(source.data()), source.size() * sizeof(u16)},
									  Vec2I(width, height), RHI::Format::RGBA16Float, 16, cube));

	EXPECT_NEAR(ReadTexel(cube, 0, 4, 8, 8, 2), 1.0f, 0.05f);
	EXPECT_LT(ReadTexel(cube, 0, 4, 8, 8, 0), 0.05f);
}

TEST(EnvironmentIBLTest, EquirectRefusesOnlyWhatItCannotRead) {
	RadianceCube cube;
	std::vector<u8> tiny(4 * 4, 0);
	EXPECT_FALSE(BuildCubeFromEquirect({tiny.data(), tiny.size()}, Vec2I(0, 0), RHI::Format::RGBA8Unorm, 8, cube));
	EXPECT_FALSE(BuildCubeFromEquirect({tiny.data(), tiny.size()}, Vec2I(4, 4), RHI::Format::D32Float, 8, cube));
	EXPECT_TRUE(BuildCubeFromEquirect({tiny.data(), tiny.size()}, Vec2I(2, 2), RHI::Format::RGBA8Unorm, 8, cube));
}

namespace {

	std::vector<u16> NumberedStrip(i32 side, bool horizontal) {
		const i32 width = horizontal ? side * 6 : side;
		const i32 height = horizontal ? side : side * 6;
		std::vector<u16> source(static_cast<size_t>(width) * height * 4, 0);
		for (i32 face = 0; face < 6; ++face) {
			for (i32 y = 0; y < side; ++y) {
				for (i32 x = 0; x < side; ++x) {
					const i32 px = horizontal ? face * side + x : x;
					const i32 py = horizontal ? y : face * side + y;
					const size_t index = (static_cast<size_t>(py) * width + px) * 4;
					source[index + 0] = Shalltear::Math::FloatToHalf(static_cast<f32>(face + 1));
					source[index + 1] = Shalltear::Math::FloatToHalf(static_cast<f32>(x));
					source[index + 2] = Shalltear::Math::FloatToHalf(static_cast<f32>(y));
					source[index + 3] = Shalltear::Math::FloatToHalf(1.0f);
				}
			}
		}
		return source;
	}

	void ExpectNumberedFaces(const RadianceCube& cube, i32 side, const char* what) {
		for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
			for (u32 y = 0; y < static_cast<u32>(side); ++y) {
				for (u32 x = 0; x < static_cast<u32>(side); ++x) {
					ASSERT_EQ(ReadTexel(cube, 0, face, x, y, 0), static_cast<f32>(face + 1)) << what << ": face " << face;
					ASSERT_EQ(ReadTexel(cube, 0, face, x, y, 1), static_cast<f32>(x)) << what << ": column, face " << face;
					ASSERT_EQ(ReadTexel(cube, 0, face, x, y, 2), static_cast<f32>(y)) << what << ": row, face " << face;
				}
			}
		}
	}

} // anonymous namespace

TEST(EnvironmentIBLTest, StripFacesMapTexelExact) {
	constexpr i32 side = 8;
	const std::vector<u16> source = NumberedStrip(side, true);
	RadianceCube cube;
	ASSERT_TRUE(BuildCubeFromStrip({reinterpret_cast<const u8*>(source.data()), source.size() * sizeof(u16)},
								   Vec2I(side * 6, side), RHI::Format::RGBA16Float, side, cube));
	ExpectNumberedFaces(cube, side, "a horizontal strip");
}

TEST(EnvironmentIBLTest, VerticalStripReadsFacesTopToBottom) {
	constexpr i32 side = 8;
	const std::vector<u16> source = NumberedStrip(side, false);
	RadianceCube cube;
	ASSERT_TRUE(BuildCubeFromStrip({reinterpret_cast<const u8*>(source.data()), source.size() * sizeof(u16)},
								   Vec2I(side, side * 6), RHI::Format::RGBA16Float, side, cube));
	ExpectNumberedFaces(cube, side, "a vertical strip");
}

TEST(EnvironmentIBLTest, OnlySixToOneIsStrip) {
	EXPECT_EQ(StripFaceSize(Vec2I(96, 16)), 16u);
	EXPECT_EQ(StripFaceSize(Vec2I(16, 96)), 16u);
	EXPECT_EQ(StripFaceSize(Vec2I(64, 32)), 0u);
	EXPECT_EQ(StripFaceSize(Vec2I(80, 16)), 0u);
	EXPECT_EQ(StripFaceSize(Vec2I(16, 16)), 0u);
	EXPECT_EQ(StripFaceSize(Vec2I(0, 0)), 0u);
}

TEST(EnvironmentIBLTest, ResampledStripStaysInFace) {
	constexpr i32 side = 16;
	std::vector<u16> source(static_cast<size_t>(side) * 6 * side * 4, 0);
	for (i32 y = 0; y < side; ++y) {
		for (i32 x = 0; x < side * 6; ++x) {
			const f32 face = static_cast<f32>(x / side + 1);
			const size_t index = (static_cast<size_t>(y) * side * 6 + x) * 4;
			source[index + 0] = Shalltear::Math::FloatToHalf(face);
			source[index + 3] = Shalltear::Math::FloatToHalf(1.0f);
		}
	}
	for (const u32 faceSize : {8u, 32u}) {
		RadianceCube cube;
		ASSERT_TRUE(BuildCubeFromStrip({reinterpret_cast<const u8*>(source.data()), source.size() * sizeof(u16)},
									   Vec2I(side * 6, side), RHI::Format::RGBA16Float, faceSize, cube));
		ASSERT_EQ(cube.faceSize, faceSize);
		for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
			EXPECT_EQ(ReadTexel(cube, 0, face, 0, faceSize / 2, 0), static_cast<f32>(face + 1)) << faceSize << ": face " << face << " left edge";
			EXPECT_EQ(ReadTexel(cube, 0, face, faceSize - 1, faceSize / 2, 0), static_cast<f32>(face + 1)) << faceSize << ": face " << face << " right edge";
		}
	}
}

TEST(EnvironmentIBLTest, FaceViewProjectsTexelDirections) {
	const Vec3 eye(1.0f, -2.0f, 3.0f);
	const Mat4 projection = Mat4::PerspectiveReverseZ(90.0f, 1.0f, 0.1f, 100.0f);
	for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
		const Mat4 viewProjection = projection * CubeFaceView(face, eye);
		for (const f32 u : {0.1f, 0.5f, 0.8f}) {
			for (const f32 v : {0.2f, 0.5f, 0.9f}) {
				const Vec3 point = eye + CubeTexelDirection(face, u, v) * 5.0f;
				const Vec4 clip = viewProjection * Vec4(point.x, point.y, point.z, 1.0f);
				const f32 ndcX = clip.x / clip.w;
				const f32 ndcY = clip.y / clip.w;
				EXPECT_NEAR(ndcX, 2.0f * u - 1.0f, 1e-4f) << "face " << face << " at (" << u << ", " << v << "): u";
				EXPECT_NEAR(ndcY, 1.0f - 2.0f * v, 1e-4f) << "face " << face << " at (" << u << ", " << v << "): v";
			}
		}
	}
}
