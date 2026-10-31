#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED
#include "Graphics/ShaderCache.h"
#include "Graphics/ShaderCompiler.h"
#endif

#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/ShaderReflection.h"
#include "Shalltear/Core/Jobs/JobSystem.h"

#include "ShadingHarness.h"

#include <algorithm>
#include <array>
#include <filesystem>

using namespace Shalltear;

namespace {

	std::filesystem::path FixtureDir() {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / "Source" / "Tests" / "Fixtures" / "Graphics";
			if (std::filesystem::is_directory(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

	Vector<u8> ReadFixture(const char* name) {
		Vector<u8> bytes{GetMemoryResource(MemTag::Asset)};
		std::filesystem::path dir = FixtureDir();
		if (dir.empty()) {
			return bytes;
		}
		Buffer buffer = FileSystem::ReadFile(Path((dir / name).generic_string()), MemTag::Asset);
		if (buffer) {
			bytes.assign(buffer.data, buffer.data + buffer.size);
		}
		return bytes;
	}

	RHI::ShaderRenderState ParseState(std::string_view source, RHI::ShaderRenderState seed = {}) {
		ShaderPragmas pragmas;
		pragmas.renderState = seed;
		ShaderAsset::ParsePragmas(source, Path("test.hlsl"), pragmas);
		return pragmas.renderState;
	}

	Vector<ShaderMemberDefault> ParseDefaults(std::string_view source) {
		ShaderPragmas pragmas;
		ShaderAsset::ParsePragmas(source, Path("test.hlsl"), pragmas);
		return std::move(pragmas.memberDefaults);
	}

} // anonymous namespace

TEST(ShaderPragmaTest, ParsesAllRenderStates) {
	RHI::ShaderRenderState state = ParseState(
		"// header\n"
		"#pragma shalltear_blend alpha\n"
		"\t#pragma shalltear_depth test_only\n"
		"#pragma shalltear_cull off\n"
		"#pragma shalltear_topology line\n"
		"float4 PSMain() : SV_Target { return 0; }\n");
	EXPECT_EQ(state.blend, RHI::BlendMode::Alpha);
	EXPECT_EQ(state.depth, RHI::DepthMode::TestOnly);
	EXPECT_EQ(state.cull, RHI::CullMode::Off);
	EXPECT_EQ(state.topology, RHI::PrimitiveTopology::LineList);
	EXPECT_EQ(ParseState("#pragma shalltear_topology point\n").topology, RHI::PrimitiveTopology::PointList);
}

TEST(ShaderPragmaTest, UnknownValuesKeepDefaults) {
	RHI::ShaderRenderState state = ParseState(
		"#pragma shalltear_blend translucent-ish\n"
		"#pragma shalltear_topology hexagons\n"
		"#pragma shalltear_frobnicate on\n");
	EXPECT_EQ(state.blend, RHI::BlendMode::Opaque);
	EXPECT_EQ(state.depth, RHI::DepthMode::TestWrite);
	EXPECT_EQ(state.cull, RHI::CullMode::Back);
	EXPECT_EQ(state.topology, RHI::PrimitiveTopology::TriangleList);
}

TEST(ShaderPragmaTest, DepthCompareParsed) {
	EXPECT_EQ(ParseState("").depthCompare, RHI::DepthCompare::NearerOrEqual);
	for (u8 i = 0; i < static_cast<u8>(RHI::DepthCompare::Count); ++i) {
		const RHI::DepthCompare compare = static_cast<RHI::DepthCompare>(i);
		const str source = "#pragma shalltear_depth_compare " + str(RHI::DepthCompareName(compare)) + "\n";
		EXPECT_EQ(ParseState(source).depthCompare, compare) << RHI::DepthCompareName(compare);
	}
	EXPECT_EQ(ParseState("#pragma shalltear_depth_compare less\n").depthCompare, RHI::DepthCompare::NearerOrEqual);
}

TEST(ShaderPragmaTest, ParsesFillAndDepthBias) {
	RHI::ShaderRenderState state = ParseState(
		"#pragma shalltear_fill wireframe\n"
		"#pragma shalltear_depth_bias 1.5 -2\n");
	EXPECT_EQ(state.fill, RHI::FillMode::Wireframe);
	EXPECT_FLOAT_EQ(state.depthBiasConstant, 1.5f);
	EXPECT_FLOAT_EQ(state.depthBiasSlope, -2.0f);
}

TEST(ShaderPragmaTest, AMalformedDepthBiasKeepsBothDefaults) {
	for (std::string_view bad : {"1.0", "1.0 2.0 3.0", "1.0 wide", "nope 2.0", ""}) {
		str source = str("#pragma shalltear_depth_bias ") + str(bad) + "\n";
		RHI::ShaderRenderState state = ParseState(source);
		EXPECT_FLOAT_EQ(state.depthBiasConstant, 0.0f) << "value '" << bad << "'";
		EXPECT_FLOAT_EQ(state.depthBiasSlope, 0.0f) << "value '" << bad << "'";
	}
}

TEST(ShaderPragmaTest, AdditiveAndFrontCull) {
	RHI::ShaderRenderState seed;
	seed.topology = RHI::PrimitiveTopology::LineList;
	RHI::ShaderRenderState state = ParseState(
		"#pragma shalltear_blend additive\n#pragma shalltear_depth off\n#pragma shalltear_cull front\n"
		"#pragma shalltear_topology triangle\n",
		seed);
	EXPECT_EQ(state.topology, RHI::PrimitiveTopology::TriangleList);
	EXPECT_EQ(state.blend, RHI::BlendMode::Additive);
	EXPECT_EQ(state.depth, RHI::DepthMode::Off);
	EXPECT_EQ(state.cull, RHI::CullMode::Front);
}

TEST(ShaderPragmaTest, ParsesEveryBlendMode) {
	const struct {
		const char* spelling;
		RHI::BlendMode mode;
	} rows[] = {
		{"opaque", RHI::BlendMode::Opaque},
		{"alpha", RHI::BlendMode::Alpha},
		{"additive", RHI::BlendMode::Additive},
		{"premultiplied", RHI::BlendMode::Premultiplied},
		{"multiply", RHI::BlendMode::Multiply},
	};
	static_assert(std::size(rows) == static_cast<size_t>(RHI::BlendMode::Count));
	for (const auto& row : rows) {
		RHI::ShaderRenderState seed;
		seed.blend = RHI::BlendMode::Multiply;
		if (row.mode == RHI::BlendMode::Multiply) {
			seed.blend = RHI::BlendMode::Opaque;
		}
		str source = str("#pragma shalltear_blend ") + row.spelling + "\n";
		EXPECT_EQ(ParseState(source, seed).blend, row.mode) << "spelling '" << row.spelling << "'";
	}
}

TEST(ShaderPragmaTest, ParsesOneToFourNumbersPerDefault) {
	Vector<ShaderMemberDefault> defaults = ParseDefaults(
		"#pragma shalltear_default baseColor 1 0.5 0 1\n"
		"#pragma shalltear_default shininess 32\n"
		"#pragma shalltear_default tiling  2   3 \n");
	ASSERT_EQ(defaults.size(), 3u);
	EXPECT_EQ(defaults[0].name, "baseColor");
	EXPECT_EQ(defaults[0].count, 4u);
	EXPECT_FLOAT_EQ(defaults[0].numbers[1], 0.5f);
	EXPECT_EQ(defaults[1].name, "shininess");
	EXPECT_EQ(defaults[1].count, 1u);
	EXPECT_FLOAT_EQ(defaults[1].numbers[0], 32.0f);
	EXPECT_EQ(defaults[2].count, 2u);
	EXPECT_FLOAT_EQ(defaults[2].numbers[1], 3.0f);
}

TEST(ShaderPragmaTest, AMalformedDefaultIsDroppedWhole) {
	for (std::string_view bad : {"baseColor", "baseColor 1 2 3 4 5", "baseColor 1 red", "", "   "}) {
		str source = str("#pragma shalltear_default ") + str(bad) + "\n";
		EXPECT_TRUE(ParseDefaults(source).empty()) << "value '" << bad << "'";
	}
}

TEST(ShaderPragmaTest, DefaultsAndRenderStateIndependent) {
	ShaderPragmas pragmas;
	ShaderAsset::ParsePragmas(
		"#pragma shalltear_default baseColor 1 1 1 1\n"
		"#pragma shalltear_cull front\n"
		"#pragma shalltear_default shininess 8\n",
		Path("test.hlsl"), pragmas);
	EXPECT_EQ(pragmas.renderState.cull, RHI::CullMode::Front);
	EXPECT_EQ(pragmas.memberDefaults.size(), 2u);
}

TEST(SpirvReflectTest, VertexFixtureReflects) {
	Vector<u8> blob = ReadFixture("fixture_vs.spv");
	ASSERT_FALSE(blob.empty());

	SpirvReflection reflection = ReflectSpirv({blob.data(), blob.size()});
	ASSERT_TRUE(reflection.valid) << reflection.error;
	EXPECT_EQ(reflection.stage, RHI::ShaderStage::Vertex);
	EXPECT_EQ(reflection.pushConstantSize, 120u);
	ASSERT_EQ(reflection.bindings.size(), 1u);
	EXPECT_EQ(reflection.bindings[0].set, 0u);
	EXPECT_EQ(reflection.bindings[0].binding, 0u);
	EXPECT_EQ(reflection.bindings[0].type, RHI::DescriptorType::UniformBuffer);
}

TEST(SpirvReflectTest, PixelFixtureReflects) {
	Vector<u8> blob = ReadFixture("fixture_ps.spv");
	ASSERT_FALSE(blob.empty());

	SpirvReflection reflection = ReflectSpirv({blob.data(), blob.size()});
	ASSERT_TRUE(reflection.valid) << reflection.error;
	EXPECT_EQ(reflection.stage, RHI::ShaderStage::Pixel);
	ASSERT_EQ(reflection.bindings.size(), 3u);
	EXPECT_EQ(reflection.bindings[0].set, 1u);
	EXPECT_EQ(reflection.bindings[0].type, RHI::DescriptorType::UniformBuffer);
	EXPECT_EQ(reflection.bindings[1].type, RHI::DescriptorType::SampledTexture);
	EXPECT_EQ(reflection.bindings[2].type, RHI::DescriptorType::Sampler);
}

TEST(SpirvReflectTest, MergeCombinesStagesAndValidatesConvention) {
	Vector<u8> vsBlob = ReadFixture("fixture_vs.spv");
	Vector<u8> psBlob = ReadFixture("fixture_ps.spv");
	ASSERT_FALSE(vsBlob.empty() || psBlob.empty());

	SpirvReflection vs = ReflectSpirv({vsBlob.data(), vsBlob.size()});
	SpirvReflection ps = ReflectSpirv({psBlob.data(), psBlob.size()});
	Vector<RHI::DescriptorBindingDesc> merged{GetMemoryResource(MemTag::Asset)};
	u32 pushSize = 0;
	str error;
	ASSERT_TRUE(MergeReflections(vs, ps, merged, pushSize, error)) << error;
	EXPECT_EQ(merged.size(), 4u);
	EXPECT_EQ(pushSize, 120u);
}

TEST(SpirvReflectTest, MergeRejectsConflictingTypes) {
	SpirvReflection vs;
	vs.valid = true;
	vs.stage = RHI::ShaderStage::Vertex;
	vs.bindings.push_back({0, 0, RHI::DescriptorType::UniformBuffer, RHI::ShaderStage::Vertex});
	SpirvReflection ps;
	ps.valid = true;
	ps.stage = RHI::ShaderStage::Pixel;
	ps.bindings.push_back({0, 0, RHI::DescriptorType::SampledTexture, RHI::ShaderStage::Pixel});

	Vector<RHI::DescriptorBindingDesc> merged{GetMemoryResource(MemTag::Asset)};
	u32 pushSize = 0;
	str error;
	EXPECT_FALSE(MergeReflections(vs, ps, merged, pushSize, error));
	EXPECT_FALSE(error.empty());
}

TEST(SpirvReflectTest, MergeRejectsExtraSet) {
	auto merge = [](u32 set, str& outError) {
		SpirvReflection vs;
		vs.valid = true;
		vs.stage = RHI::ShaderStage::Vertex;
		SpirvReflection ps;
		ps.valid = true;
		ps.stage = RHI::ShaderStage::Pixel;
		ps.bindings.push_back({set, 0, RHI::DescriptorType::UniformBuffer, RHI::ShaderStage::Pixel});

		Vector<RHI::DescriptorBindingDesc> merged{GetMemoryResource(MemTag::Asset)};
		u32 pushSize = 0;
		return MergeReflections(vs, ps, merged, pushSize, outError);
	};

	str accepted;
	EXPECT_TRUE(merge(2, accepted)) << accepted;
	str refused;
	EXPECT_FALSE(merge(3, refused));
	EXPECT_NE(refused.find("set 3"), str::npos) << refused;
}

TEST(SpirvReflectTest, RejectsMalformedModules) {
	std::array<u8, 32> badMagic{};
	EXPECT_FALSE(ReflectSpirv(badMagic).valid);

	std::array<u8, 8> tooShort{0x03, 0x02, 0x23, 0x07};
	EXPECT_FALSE(ReflectSpirv(tooShort).valid);

	std::array<u32, 7> truncated{0x07230203, 0x00010300, 0, 100, 0, (0xFFFFu << 16) | 15, 0};
	EXPECT_FALSE(ReflectSpirv({reinterpret_cast<const u8*>(truncated.data()), truncated.size() * 4}).valid);

	std::array<u8, 21> odd{0x03, 0x02, 0x23, 0x07};
	EXPECT_FALSE(ReflectSpirv(odd).valid);
}

namespace {

	class ShaderCompileFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_shaders_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
		}

		void TearDown() override {
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Path Write(const char* name, const str& content) {
			std::filesystem::path file = _root / name;
			FileSystem::WriteTextFile(Path(file.generic_string()), content);
			return Path(file.generic_string());
		}
	};

	constexpr const char* MINIMAL_SHADER = R"(
#pragma shalltear_blend alpha
#pragma shalltear_cull off
[[vk::binding(0, 0)]] cbuffer ViewData { float4x4 viewProjection; };
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	output.positionCS = mul(viewProjection, float4(float(vertexId), 0, 0, 1));
	return output;
}
float4 PSMain(VSOutput input) : SV_Target { return float4(1, 0, 1, 1); }
)";

} // anonymous namespace

TEST_F(ShaderCompileFixture, ImportCompilesReflectsAndParsesState) {
	Path source = Write("minimal.hlsl", MINIMAL_SHADER);

	ShaderAsset asset;
	if (!asset.Import(source)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}
	ASSERT_FALSE(asset.variants.empty());
	EXPECT_FALSE(asset.variants[SHADER_BASE_VARIANT].vertexSpirv.empty());
	EXPECT_FALSE(asset.variants[SHADER_BASE_VARIANT].pixelSpirv.empty());
	EXPECT_EQ(asset.renderState.blend, RHI::BlendMode::Alpha);
	EXPECT_EQ(asset.renderState.cull, RHI::CullMode::Off);
	ASSERT_EQ(asset.bindings.size(), 1u);
	EXPECT_EQ(asset.bindings[0].set, 0u);
	ASSERT_FALSE(asset.dependencies.empty());
	EXPECT_EQ(asset.dependencies[0].physicalPath, source.ToString());
	EXPECT_EQ(asset.version, 1u);
}

namespace {

	constexpr const char* KERNEL_SHADER = R"(
#pragma shalltear_kernel
[[vk::binding(0, 0)]] RWByteAddressBuffer values;
struct KernelPush { uint count; uint base; };
[[vk::push_constant]] KernelPush push;
[numthreads(32, 2, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
	if (id.x < push.count) {
		values.Store(id.x * 4, push.base + id.x);
	}
}
)";

} // anonymous namespace

TEST_F(ShaderCompileFixture, KernelImportsWithGroupSize) {
	ShaderAsset kernel;
	if (!kernel.Import(Write("kernel.hlsl", KERNEL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_TRUE(kernel.IsKernel());
	const ShaderVariant& base = kernel.variants[SHADER_BASE_VARIANT];
	EXPECT_FALSE(base.HasProgram(ShaderProgramKind::Forward));
	EXPECT_FALSE(base.HasProgram(ShaderProgramKind::Depth));
	EXPECT_TRUE(base.pixelSpirv.empty());
	EXPECT_EQ(kernel.threadGroupSize, Vec3I(32, 2, 1));
	ASSERT_EQ(kernel.bindings.size(), 1u);
	EXPECT_EQ(kernel.bindings[0].type, RHI::DescriptorType::StorageBuffer);
	EXPECT_EQ(kernel.bindings[0].stages, RHI::ShaderStage::Compute);
	EXPECT_EQ(kernel.pushConstantSize, 8u);

	ASSERT_TRUE(kernel.Import(Write("kernel.hlsl", MINIMAL_SHADER)));
	EXPECT_FALSE(kernel.IsKernel());
	EXPECT_EQ(kernel.threadGroupSize, Vec3I(0, 0, 0));
}

TEST_F(ShaderCompileFixture, KernelRefusesTemplateAndDropsKeywords) {
	ShaderAsset kernel;
	if (!kernel.Import(Write("kernel.hlsl", "#pragma shalltear_keyword UNUSED\n" + str(KERNEL_SHADER)))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	EXPECT_TRUE(kernel.keywords.empty());

	ShaderAsset templated;
	EXPECT_FALSE(templated.Import(Write("templated.hlsl", "#pragma shalltear_template lit\n" + str(KERNEL_SHADER))));
}

namespace {

	constexpr const char* DEPTH_PROGRAM_SHADER = R"(
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
#ifdef SHALLTEAR_DEPTH_PROGRAM
	output.positionCS = float4(float(vertexId), 1.0, 0.0, 1.0);
#else
	output.positionCS = float4(float(vertexId), 0.0, 0.0, 1.0);
#endif
	return output;
}
#if defined(SHALLTEAR_DEPTH_NO_COLOR)
void PSMain(VSOutput input) { clip(input.positionCS.x - 0.5); }
#elif defined(SHALLTEAR_DEPTH_PROGRAM)
float4 PSMain(VSOutput input) : SV_Target0 { return float4(0.5, 0.5, 1.0, 1.0); }
#else
float4 PSMain(VSOutput input) : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }
#endif
)";

} // anonymous namespace

TEST_F(ShaderCompileFixture, RawShaderDepthOnRequest) {
	ShaderPragmas declared;
	ShaderAsset::ParsePragmas("#pragma shalltear_depth_program\n", Path("test.hlsl"), declared);
	EXPECT_EQ(declared.depthProgram, ShaderDepthProgram::Declared);
	ShaderPragmas clipping;
	ShaderAsset::ParsePragmas("#pragma shalltear_depth_program clip\n", Path("test.hlsl"), clipping);
	EXPECT_EQ(clipping.depthProgram, ShaderDepthProgram::Clipping);

	ShaderAsset none;
	if (!none.Import(Write("none.hlsl", DEPTH_PROGRAM_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	EXPECT_FALSE(none.variants[SHADER_BASE_VARIANT].HasProgram(ShaderProgramKind::Depth));

	ShaderAsset normals;
	ASSERT_TRUE(normals.Import(Write("normals.hlsl", "#pragma shalltear_depth_program\n" + str(DEPTH_PROGRAM_SHADER))));
	const ShaderVariant& declaredBase = normals.variants[SHADER_BASE_VARIANT];
	EXPECT_EQ(normals.depthProgram, ShaderDepthProgram::Declared);
	ASSERT_TRUE(declaredBase.HasProgram(ShaderProgramKind::Depth));
	EXPECT_FALSE(declaredBase.depthPixelSpirv.empty());
	EXPECT_TRUE(declaredBase.depthClipPixelSpirv.empty());
	EXPECT_NE(declaredBase.depthVertexSpirv, declaredBase.vertexSpirv);

	ShaderAsset clipped;
	ASSERT_TRUE(clipped.Import(Write("clipped.hlsl", "#pragma shalltear_depth_program clip\n" + str(DEPTH_PROGRAM_SHADER))));
	const ShaderVariant& clippedBase = clipped.variants[SHADER_BASE_VARIANT];
	EXPECT_EQ(clipped.depthProgram, ShaderDepthProgram::Clipping);
	EXPECT_FALSE(clippedBase.depthClipPixelSpirv.empty());
	EXPECT_NE(clippedBase.depthClipPixelSpirv, clippedBase.depthPixelSpirv);

	ShaderAsset kernel;
	ASSERT_TRUE(kernel.Import(Write("kernel.hlsl", "#pragma shalltear_depth_program\n" + str(KERNEL_SHADER))));
	EXPECT_EQ(kernel.depthProgram, ShaderDepthProgram::None);
	EXPECT_FALSE(kernel.variants[SHADER_BASE_VARIANT].HasProgram(ShaderProgramKind::Depth));
}

namespace {

	constexpr const char* VALUE_PROGRAM_SHADER = R"(
struct ValuePush { uint value; uint pad; };
[[vk::push_constant]] ValuePush gPush;
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	output.positionCS = float4(float(vertexId), float(gPush.pad), 0.0, 1.0);
	return output;
}
#if defined(SHALLTEAR_DEPTH_VALUE)
uint PSMain(VSOutput input) : SV_Target0 { return gPush.value; }
#elif defined(SHALLTEAR_DEPTH_NO_COLOR)
void PSMain(VSOutput input) { clip(input.positionCS.x - 0.5); }
#elif defined(SHALLTEAR_DEPTH_PROGRAM)
float4 PSMain(VSOutput input) : SV_Target0 { return float4(0.5, 0.5, 1.0, 1.0); }
#else
float4 PSMain(VSOutput input) : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }
#endif
)";

} // anonymous namespace

TEST_F(ShaderCompileFixture, RawDepthValueOnRequest) {
	ShaderPragmas both;
	ShaderAsset::ParsePragmas("#pragma shalltear_depth_program value clip\n", Path("test.hlsl"), both);
	EXPECT_EQ(both.depthProgram, ShaderDepthProgram::Clipping);
	EXPECT_TRUE(both.depthValue);

	ShaderAsset valued;
	if (!valued.Import(Write("valued.hlsl", "#pragma shalltear_depth_program clip value\n" + str(VALUE_PROGRAM_SHADER)))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const ShaderVariant& base = valued.variants[SHADER_BASE_VARIANT];
	EXPECT_TRUE(valued.depthValue);
	EXPECT_FALSE(base.depthValuePixelSpirv.empty());
	EXPECT_NE(base.depthValuePixelSpirv, base.depthPixelSpirv);
	EXPECT_NE(base.depthValuePixelSpirv, base.depthClipPixelSpirv);

	ShaderAsset plain;
	ASSERT_TRUE(plain.Import(Write("plain.hlsl", "#pragma shalltear_depth_program\n" + str(VALUE_PROGRAM_SHADER))));
	EXPECT_FALSE(plain.depthValue);
	EXPECT_TRUE(plain.variants[SHADER_BASE_VARIANT].depthValuePixelSpirv.empty());
}

namespace {

	constexpr const char* UNREAD_BINDINGS_SHADER = R"(
[[vk::binding(0, 1)]] Texture2D measured;
[[vk::binding(1, 1)]] Texture2D unread;
[[vk::binding(2, 1)]] SamplerState measuredSampler;
[[vk::binding(3, 1)]] SamplerState unreadSampler;
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	output.positionCS = float4(float(vertexId), 0, 0, 1);
	return output;
}
float4 PSMain(VSOutput input) : SV_Target {
	uint width, height;
	measured.GetDimensions(width, height);
	return float4(float(width), float(height), 0, 1);
}
)";

	u32 CountSetOne(const Vector<ShaderResourceInfo>& resources, RHI::DescriptorType type) {
		return static_cast<u32>(std::count_if(resources.begin(), resources.end(), [type](const ShaderResourceInfo& resource) {
			return resource.set == 1 && resource.type == type;
		}));
	}

} // anonymous namespace

TEST_F(ShaderCompileFixture, UnreadTextureAndSamplerStayInInterface) {
	ShaderAsset probe;
	if (!probe.Import(Write("minimal.hlsl", MINIMAL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ShaderAsset asset;
	ASSERT_TRUE(asset.Import(Write("unread.hlsl", UNREAD_BINDINGS_SHADER)));
	EXPECT_EQ(CountSetOne(asset.resources, RHI::DescriptorType::SampledTexture), 2u);
	EXPECT_EQ(CountSetOne(asset.resources, RHI::DescriptorType::Sampler), 2u);
}

#ifndef SHALLTEAR_SHARED
TEST_F(ShaderCompileFixture, OptimizedCompileKeepsEveryBindingItDeclares) {
	if (!ShaderCompiler::IsAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Path source = Write("unread.hlsl", UNREAD_BINDINGS_SHADER);
	ShaderCompileResult compiled = ShaderCompiler::Compile(UNREAD_BINDINGS_SHADER, source, ShaderStageKind::Pixel, {}, ShaderCompileMode::Optimized);
	ASSERT_TRUE(compiled.success) << compiled.errors;
	SpirvReflection reflection = ReflectSpirv({compiled.spirv.data(), compiled.spirv.size()});
	ASSERT_TRUE(reflection.valid) << reflection.error;
	EXPECT_EQ(CountSetOne(reflection.resources, RHI::DescriptorType::SampledTexture), 2u);
	EXPECT_EQ(CountSetOne(reflection.resources, RHI::DescriptorType::Sampler), 2u);
}

TEST_F(ShaderCompileFixture, CompileModeReachesDxc) {
	if (!ShaderCompiler::IsAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Path source = Write("modes.hlsl", UNREAD_BINDINGS_SHADER);
	constexpr ShaderCompileMode MODES[] = {ShaderCompileMode::Optimized, ShaderCompileMode::Unoptimized, ShaderCompileMode::Debug};
	size_t sizes[3] = {};
	u64 keys[3] = {};
	for (size_t i = 0; i < 3; ++i) {
		ShaderCompileResult compiled = ShaderCompiler::Compile(UNREAD_BINDINGS_SHADER, source, ShaderStageKind::Pixel, {}, MODES[i]);
		ASSERT_TRUE(compiled.success) << compiled.errors;
		sizes[i] = compiled.spirv.size();
		SpirvReflection reflection = ReflectSpirv({compiled.spirv.data(), compiled.spirv.size()});
		ASSERT_TRUE(reflection.valid) << reflection.error;
		EXPECT_EQ(CountSetOne(reflection.resources, RHI::DescriptorType::SampledTexture), 2u) << "mode " << i;
		keys[i] = ShaderCache::ComputeEntryKey(1, 0, "dir", {}, ShaderStageKind::Pixel, MODES[i]);
	}
	EXPECT_LT(sizes[0], sizes[1]);
	EXPECT_LT(sizes[1], sizes[2]);
	EXPECT_NE(keys[0], keys[1]);
	EXPECT_NE(keys[1], keys[2]);
	EXPECT_NE(keys[0], keys[2]);
}
#endif

TEST_F(ShaderCompileFixture, IncludesAreRecordedAsDependencies) {
	Write("common.hlsli", "float4 Tint() { return float4(0.5, 0.5, 0.5, 1.0); }\n");
	Path source = Write("including.hlsl", R"(
#include "common.hlsli"
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	output.positionCS = float4(float(vertexId), 0, 0, 1);
	return output;
}
float4 PSMain(VSOutput input) : SV_Target { return Tint(); }
)");

	ShaderAsset asset;
	if (!asset.Import(source)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	bool includeRecorded = false;
	for (const ShaderAsset::Dependency& dependency : asset.dependencies) {
		if (dependency.physicalPath.find("common.hlsli") != str::npos) {
			includeRecorded = true;
			EXPECT_NE(dependency.contentHash, 0u);
		}
	}
	EXPECT_TRUE(includeRecorded);
}

TEST_F(ShaderCompileFixture, NestedIncludeRecordedByPath) {
	std::filesystem::create_directories(_root / "shared");
	std::filesystem::create_directories(_root / "shaders");
	Write("shared/common.hlsli", "float4 Tint() { return float4(0.5, 0.5, 0.5, 1.0); }\n");
	Path source = Write("shaders/including.hlsl", R"(
#include "../shared/common.hlsli"
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	output.positionCS = float4(float(vertexId), 0, 0, 1);
	return output;
}
float4 PSMain(VSOutput input) : SV_Target { return Tint(); }
)");

	ShaderAsset asset;
	if (!asset.Import(source)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const str expected = (_root / "shared" / "common.hlsli").lexically_normal().generic_string();
	bool recorded = false;
	for (const ShaderAsset::Dependency& dependency : asset.dependencies) {
		if (dependency.physicalPath.find("common.hlsli") != str::npos) {
			recorded = true;
			EXPECT_EQ(dependency.physicalPath, expected);
		}
	}
	EXPECT_TRUE(recorded);
}

TEST_F(ShaderCompileFixture, VertexInputAttributesAreRejected) {
	Path source = Write("attributes.hlsl", R"(
struct VSInput { float3 position : POSITION; };
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(VSInput input) {
	VSOutput output;
	output.positionCS = float4(input.position, 1.0);
	return output;
}
float4 PSMain(VSOutput input) : SV_Target { return 1; }
)");

	ShaderAsset probe;
	if (!probe.Import(Write("probe.hlsl", MINIMAL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}

	ShaderAsset asset;
	EXPECT_FALSE(asset.Import(source));
}

TEST_F(ShaderCompileFixture, CompileErrorFailsImport) {
	Path source = Write("broken.hlsl", "this is not hlsl at all {\n");
	ShaderAsset probe;
	if (!probe.Import(Write("probe.hlsl", MINIMAL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ShaderAsset asset;
	EXPECT_FALSE(asset.Import(source));
}

namespace {

	class EngineIncludeFixture : public ShaderCompileFixture {
	protected:
		std::filesystem::path _engineRoot;

		void SetUp() override {
			ShaderCompileFixture::SetUp();
			_engineRoot = _root / "EngineAssets";
			std::filesystem::create_directories(_engineRoot / "Shaders");
			FileSystem::Mount("engine", Path(_engineRoot.generic_string()));
		}

		void TearDown() override {
			FileSystem::Unmount("engine");
			ShaderCompileFixture::TearDown();
		}

		void WriteEngineHeader(const char* name, const str& content) {
			FileSystem::WriteTextFile(Path((_engineRoot / "Shaders" / name).generic_string()), content);
		}
	};

	constexpr const char* SHADER_INCLUDING_ENGINE_HEADER = R"(
#include "TestCommon.hlsli"
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	output.positionCS = float4(float(vertexId), 0, 0, 1);
	return output;
}
float4 PSMain(VSOutput input) : SV_Target { return EngineTint(); }
)";

} // anonymous namespace

TEST_F(EngineIncludeFixture, ProjectShaderReachesEnginesShaderHeaders) {
	WriteEngineHeader("TestCommon.hlsli", "float4 EngineTint() { return float4(0.25, 0.5, 0.75, 1.0); }\n");
	Path source = Write("uses_engine_header.hlsl", SHADER_INCLUDING_ENGINE_HEADER);

	ShaderAsset asset;
	if (!asset.Import(source)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}
	ASSERT_FALSE(asset.variants.empty());
	EXPECT_FALSE(asset.variants[SHADER_BASE_VARIANT].vertexSpirv.empty());

	bool recorded = false;
	for (const ShaderAsset::Dependency& dependency : asset.dependencies) {
		if (dependency.physicalPath.find("TestCommon.hlsli") != str::npos) {
			recorded = true;
			EXPECT_NE(dependency.contentHash, 0u);
		}
	}
	EXPECT_TRUE(recorded);
}

TEST_F(EngineIncludeFixture, MissingEngineHeaderFails) {
	ShaderAsset probe;
	if (!probe.Import(Write("probe.hlsl", MINIMAL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}
	Path source = Write("uses_engine_header.hlsl", SHADER_INCLUDING_ENGINE_HEADER);

	ShaderAsset asset;
	EXPECT_FALSE(asset.Import(source));
}

TEST_F(EngineIncludeFixture, AProjectHeaderShadowsTheEngineOne) {
	WriteEngineHeader("TestCommon.hlsli", "float4 EngineTint() { return float4(1, 0, 0, 1); }\n");
	Write("TestCommon.hlsli", "float4 EngineTint() { return float4(0, 1, 0, 1); }\n");
	Path source = Write("uses_engine_header.hlsl", SHADER_INCLUDING_ENGINE_HEADER);

	ShaderAsset asset;
	if (!asset.Import(source)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	bool engineHeaderUsed = false;
	bool projectHeaderUsed = false;
	for (const ShaderAsset::Dependency& dependency : asset.dependencies) {
		if (dependency.physicalPath.find("TestCommon.hlsli") == str::npos) {
			continue;
		}
		if (dependency.physicalPath.find("EngineAssets") != str::npos) {
			engineHeaderUsed = true;
		} else {
			projectHeaderUsed = true;
		}
	}
	EXPECT_TRUE(projectHeaderUsed);
	EXPECT_FALSE(engineHeaderUsed);
}

TEST_F(EngineIncludeFixture, MissingNestedHeaderNamed) {
	WriteEngineHeader("TestCommon.hlsli", "float4 EngineTint() { return float4(0.25, 0.5, 0.75, 1.0); }\n");
	std::filesystem::create_directories(_engineRoot / "Shaders" / "Nested");
	WriteEngineHeader("Nested/Tint.hlsli", "#include \"TestCommon.hlsli\"\nfloat4 NestedTint() { return EngineTint(); }\n");
	ShaderAsset control;
	if (!control.Import(Write("probe.hlsl", MINIMAL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}
	const str body = "struct VSOutput { float4 positionCS : SV_Position; };\n"
					 "VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
					 "\tVSOutput output;\n"
					 "\toutput.positionCS = float4(float(vertexId), 0, 0, 1);\n"
					 "\treturn output;\n"
					 "}\n";
	const Path nested = Write("uses_nested.hlsl", "#include \"Nested/Tint.hlsli\"\n" + body + "float4 PSMain(VSOutput input) : SV_Target { return NestedTint(); }\n");
	const Path missing = Write("uses_missing.hlsl", "#include \"Nowhere.hlsli\"\n" + body + "float4 PSMain(VSOutput input) : SV_Target { return 1; }\n");

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Info);
	auto sink = std::make_shared<ShadingTest::ShaderDiagnosticSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ShadingTest::ArmDiagnosticSink(sink));
	ShaderAsset quiet;
	const bool nestedImported = quiet.Import(nested);
	ShaderAsset broken;
	const bool missingImported = broken.Import(missing);
	LogManager::Terminate();
	const str captured = sink->Joined();
	sink.reset();
	JobSystem::Terminate();

	EXPECT_TRUE(nestedImported);
	EXPECT_EQ(captured.find("TestCommon.hlsli"), str::npos) << captured;
	EXPECT_FALSE(missingImported);
	EXPECT_NE(captured.find("Nowhere.hlsli"), str::npos) << captured;
}

TEST_F(EngineIncludeFixture, VirtualPathResolvedFirst) {
	WriteEngineHeader("TestCommon.hlsli", "float4 EngineTint() { return float4(0.25, 0.5, 0.75, 1.0); }\n");
	ShaderAsset control;
	if (!control.Import(Write("probe.hlsl", MINIMAL_SHADER))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}

	FileSystem::WriteTextFile(Path((_engineRoot / "Shaders" / "ViaScheme.hlsl").generic_string()),
							  SHADER_INCLUDING_ENGINE_HEADER);

	ShaderAsset asset;
	ASSERT_TRUE(asset.Import(Path("engine://Shaders/ViaScheme.hlsl")));

	ASSERT_FALSE(asset.dependencies.empty());
	EXPECT_EQ(asset.dependencies[0].physicalPath.find("://"), str::npos);
}

namespace {

	Vector<str> Keywords(std::initializer_list<const char*> names) {
		Vector<str> out{GetMemoryResource(MemTag::Asset)};
		for (const char* name : names) {
			out.push_back(str(name));
		}
		return out;
	}

	constexpr const char* KEYWORD_SHADER = R"(
#pragma shalltear_keyword FEATURE_A
#pragma shalltear_keyword FEATURE_B
[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 baseColor; };
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	output.positionCS = float4(float(vertexId), 0, 0, 1);
	return output;
}
float4 PSMain(VSOutput input) : SV_Target {
	float3 tint = float3(1, 0, 0);
#ifdef FEATURE_A
	tint = float3(0, 1, 0);
#endif
#ifdef FEATURE_B
	tint += float3(0, 0, 1);
#endif
	return float4(tint * baseColor.rgb, 1);
}
)";

} // anonymous namespace

TEST(ShaderKeywordHashTest, KeyIgnoresOrder) {
	Vector<str> ab = Keywords({"ALPHA", "BETA"});
	Vector<str> ba = Keywords({"BETA", "ALPHA"});
	Vector<str> abb = Keywords({"ALPHA", "BETA", "BETA"});
	Vector<str> a = Keywords({"ALPHA"});

	EXPECT_EQ(ShaderAsset::KeywordSetHash({ab.data(), ab.size()}), ShaderAsset::KeywordSetHash({ba.data(), ba.size()}));
	EXPECT_EQ(ShaderAsset::KeywordSetHash({ab.data(), ab.size()}), ShaderAsset::KeywordSetHash({abb.data(), abb.size()}));
	EXPECT_NE(ShaderAsset::KeywordSetHash({ab.data(), ab.size()}), ShaderAsset::KeywordSetHash({a.data(), a.size()}));
	EXPECT_NE(ShaderAsset::KeywordSetHash({a.data(), a.size()}), ShaderAsset::KeywordSetHash({}));

	Vector<str> joined = Keywords({"ALPHABETA"});
	Vector<str> split = Keywords({"ALPHA", "BETA"});
	EXPECT_NE(ShaderAsset::KeywordSetHash({joined.data(), joined.size()}),
			  ShaderAsset::KeywordSetHash({split.data(), split.size()}));
}

TEST_F(ShaderCompileFixture, ImportCompilesBaseOnly) {
	ShaderAsset asset;
	if (!asset.Import(Write("keywords.hlsl", str(KEYWORD_SHADER)))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_EQ(asset.keywords.size(), 2u);
	EXPECT_EQ(asset.keywords[0], "FEATURE_A");
	EXPECT_EQ(asset.keywords[1], "FEATURE_B");
	ASSERT_EQ(asset.variants.size(), 1u);
	EXPECT_EQ(asset.variants[SHADER_BASE_VARIANT].keywordHash, ShaderAsset::KeywordSetHash({}));
	EXPECT_TRUE(asset.variants[SHADER_BASE_VARIANT].keywords.empty());
}

TEST_F(ShaderCompileFixture, SkipOptimizationsForAllVariants) {
	ShaderAsset optimized;
	if (!optimized.Import(Write("optimized.hlsl", str(KEYWORD_SHADER)))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ShaderAsset skipped;
	ASSERT_TRUE(skipped.Import(Write("skipped.hlsl", "#pragma shalltear_skip_optimizations\n" + str(KEYWORD_SHADER))));
	EXPECT_FALSE(optimized.skipOptimizations);
	EXPECT_TRUE(skipped.skipOptimizations);
	Vector<str> a = Keywords({"FEATURE_A"});
	const u32 optimizedVariant = optimized.GetOrCompileVariant({a.data(), a.size()});
	const u32 skippedVariant = skipped.GetOrCompileVariant({a.data(), a.size()});
	ASSERT_NE(optimizedVariant, SHADER_BASE_VARIANT);
	ASSERT_NE(skippedVariant, SHADER_BASE_VARIANT);
#if defined(SHALLTEAR_CONFIG_RELEASE)
	EXPECT_GT(skipped.variants[SHADER_BASE_VARIANT].pixelSpirv.size(), optimized.variants[SHADER_BASE_VARIANT].pixelSpirv.size());
	EXPECT_GT(skipped.variants[skippedVariant].pixelSpirv.size(), optimized.variants[optimizedVariant].pixelSpirv.size());
#endif
	EXPECT_EQ(skipped.bindings.size(), optimized.bindings.size());
	EXPECT_EQ(skipped.uniformBlocks.size(), optimized.uniformBlocks.size());
}

TEST_F(ShaderCompileFixture, VariantCompiledOnce) {
	ShaderAsset asset;
	if (!asset.Import(Write("keywords.hlsl", str(KEYWORD_SHADER)))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Vector<str> a = Keywords({"FEATURE_A"});
	u32 first = asset.GetOrCompileVariant({a.data(), a.size()});
	EXPECT_EQ(first, 1u);
	EXPECT_EQ(asset.variants.size(), 2u);

	Vector<str> aAgain = Keywords({"FEATURE_A"});
	EXPECT_EQ(asset.GetOrCompileVariant({aAgain.data(), aAgain.size()}), first);
	EXPECT_EQ(asset.variants.size(), 2u);

	Vector<str> both = Keywords({"FEATURE_B", "FEATURE_A"});
	u32 pair = asset.GetOrCompileVariant({both.data(), both.size()});
	EXPECT_EQ(asset.variants.size(), 3u);
	Vector<str> bothReversed = Keywords({"FEATURE_A", "FEATURE_B"});
	EXPECT_EQ(asset.GetOrCompileVariant({bothReversed.data(), bothReversed.size()}), pair);
	EXPECT_EQ(asset.variants.size(), 3u);

	EXPECT_EQ(asset.FindVariant(ShaderAsset::KeywordSetHash({a.data(), a.size()})), first);
	EXPECT_EQ(asset.FindVariant(ShaderAsset::KeywordSetHash({})), SHADER_BASE_VARIANT);
	Vector<str> unknown = Keywords({"NOT_DECLARED"});
	EXPECT_EQ(asset.FindVariant(ShaderAsset::KeywordSetHash({unknown.data(), unknown.size()})), SHADER_BASE_VARIANT);
}

TEST_F(ShaderCompileFixture, UndeclaredKeywordIsDroppedNotCompiled) {
	ShaderAsset asset;
	if (!asset.Import(Write("keywords.hlsl", str(KEYWORD_SHADER)))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Vector<str> typo = Keywords({"FEATURE_TYPO"});
	EXPECT_EQ(asset.GetOrCompileVariant({typo.data(), typo.size()}), SHADER_BASE_VARIANT);
	EXPECT_EQ(asset.variants.size(), 1u);

	Vector<str> mixed = Keywords({"FEATURE_TYPO", "FEATURE_A"});
	EXPECT_EQ(asset.GetOrCompileVariant({mixed.data(), mixed.size()}), 1u);
	Vector<str> a = Keywords({"FEATURE_A"});
	EXPECT_EQ(asset.variants[1].keywordHash, ShaderAsset::KeywordSetHash({a.data(), a.size()}));
}

TEST_F(ShaderCompileFixture, InterfaceChangingVariantRefused) {
	constexpr const char* CONDITIONAL_DECLARATION = R"(
#pragma shalltear_keyword ADD_A_MEMBER
[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {
	float4 baseColor;
#ifdef ADD_A_MEMBER
	float extra;
#endif
};
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	output.positionCS = float4(float(vertexId), 0, 0, 1);
	return output;
}
float4 PSMain(VSOutput input) : SV_Target {
	float4 c = baseColor;
#ifdef ADD_A_MEMBER
	c *= extra;
#endif
	return c;
}
)";
	ShaderAsset asset;
	if (!asset.Import(Write("conditional.hlsl", str(CONDITIONAL_DECLARATION)))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const UniformBlockLayout* before = asset.GetMaterialParamLayout();
	ASSERT_NE(before, nullptr);
	ASSERT_EQ(before->members.size(), 1u);
	const u32 sizeBefore = before->sizeBytes;

	Vector<str> add = Keywords({"ADD_A_MEMBER"});
	EXPECT_EQ(asset.GetOrCompileVariant({add.data(), add.size()}), SHADER_BASE_VARIANT);
	EXPECT_EQ(asset.variants.size(), 1u);

	const UniformBlockLayout* after = asset.GetMaterialParamLayout();
	ASSERT_NE(after, nullptr);
	EXPECT_EQ(after->members.size(), 1u);
	EXPECT_EQ(after->sizeBytes, sizeBefore);

	constexpr const char* UNCONDITIONAL_DECLARATION = R"(
#pragma shalltear_keyword ADD_A_MEMBER
[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {
	float4 baseColor;
	float extra;
};
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	output.positionCS = float4(float(vertexId), 0, 0, 1);
	return output;
}
float4 PSMain(VSOutput input) : SV_Target {
	float4 c = baseColor;
#ifdef ADD_A_MEMBER
	c *= extra;
#endif
	return c;
}
)";
	ShaderAsset supported;
	ASSERT_TRUE(supported.Import(Write("unconditional.hlsl", str(UNCONDITIONAL_DECLARATION))));
	Vector<str> addAgain = Keywords({"ADD_A_MEMBER"});
	EXPECT_EQ(supported.GetOrCompileVariant({addAgain.data(), addAgain.size()}), 1u);
}

TEST_F(ShaderCompileFixture, VariantThatChangesSlotsDimensionIsRefused) {
	constexpr const char* CONDITIONAL_DIMENSION = R"(
#pragma shalltear_keyword DEEP
[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 baseColor; };
#ifdef DEEP
[[vk::binding(1, 1)]] Texture3D ramp;
#else
[[vk::binding(1, 1)]] Texture2D ramp;
#endif
[[vk::binding(2, 1)]] SamplerState rampSampler;
struct VSOutput { float4 positionCS : SV_Position; };
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	output.positionCS = float4(float(vertexId), 0, 0, 1);
	return output;
}
float4 PSMain(VSOutput input) : SV_Target {
#ifdef DEEP
	return ramp.SampleLevel(rampSampler, float3(0.5, 0.5, 0.5), 0) * baseColor;
#else
	return ramp.SampleLevel(rampSampler, float2(0.5, 0.5), 0) * baseColor;
#endif
}
)";
	ShaderAsset asset;
	if (!asset.Import(Write("dimension.hlsl", str(CONDITIONAL_DIMENSION)))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	EXPECT_EQ(asset.GetTextureSlotDimension("ramp"), RHI::TextureDimension::Texture2D);
	Vector<str> deep = Keywords({"DEEP"});
	EXPECT_EQ(asset.GetOrCompileVariant({deep.data(), deep.size()}), SHADER_BASE_VARIANT);
	EXPECT_EQ(asset.GetTextureSlotDimension("ramp"), RHI::TextureDimension::Texture2D);
}

TEST_F(ShaderCompileFixture, SourcelessShaderUsesBase) {
	ShaderAsset asset;
	if (!asset.Import(Write("keywords.hlsl", str(KEYWORD_SHADER)))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	asset.dependencies.clear();

	Vector<str> a = Keywords({"FEATURE_A"});
	EXPECT_EQ(asset.GetOrCompileVariant({a.data(), a.size()}), SHADER_BASE_VARIANT);
	EXPECT_EQ(asset.variants.size(), 1u);
}
