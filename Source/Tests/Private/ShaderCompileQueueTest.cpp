#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/ShaderCompileQueue.h"

#include <chrono>
#include <thread>

using namespace Shalltear;

namespace {

	using namespace ShadingTest;

	constexpr const char* SURFACE_SHADER = "#pragma shalltear_template unlit\n"
										   "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
										   "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
										   "\tsurface.baseColor = tint.rgb;\n"
										   "\tsurface.alpha = 1.0;\n"
										   "}\n";

	class ShaderCompileQueueFixture : public MaterialShadingFixture {
	protected:
		void TearDown() override {
			ShaderCompileQueue::Terminate();
			MaterialShadingFixture::TearDown();
		}

		ShaderCompileStatus Await(ShaderCompileTicket ticket, ShaderAsset*& out) {
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
			for (;;) {
				const ShaderCompileStatus status = ShaderCompileQueue::Collect(ticket, out);
				if (status != ShaderCompileStatus::Pending || std::chrono::steady_clock::now() > deadline) {
					return status;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
		}

		static u32 LiveShaders() {
			u32 count = 0;
			ObjectRegistry::ForEachLiveObject(
				[](Object* object, u64, void* user) {
					if (object && object->GetType() == ShaderAsset::GetReflectType()) {
						++*static_cast<u32*>(user);
					}
				},
				&count);
			return count;
		}
	};

} // anonymous namespace

TEST_F(ShaderCompileQueueFixture, QueuedImportOffCallerThread) {
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Surface.hlsl", SURFACE_SHADER);
	const ShaderCompileStats before = ShaderCompileQueue::GetStats();

	const ShaderCompileTicket ticket = ShaderCompileQueue::Submit(Path("project://Surface.hlsl"));
	ASSERT_NE(ticket, 0u);
	ShaderAsset* compiled = nullptr;
	ASSERT_EQ(Await(ticket, compiled), ShaderCompileStatus::Succeeded);
	ASSERT_NE(compiled, nullptr);

	const ShaderCompileStats after = ShaderCompileQueue::GetStats();
	EXPECT_EQ(after.compilesElsewhere, before.compilesElsewhere);
	EXPECT_GT(after.compilesOnCompileThreads, before.compilesOnCompileThreads);
	EXPECT_EQ(after.submitted, before.submitted + 1);
	EXPECT_EQ(after.finished, before.finished + 1);

	ShaderAsset reference;
	ASSERT_TRUE(reference.Import(FileSystem::Resolve(Path("project://Surface.hlsl"))));
	ASSERT_NE(compiled->GetMaterialParamLayout(), nullptr);
	EXPECT_EQ(compiled->GetMaterialParamLayout()->sizeBytes, reference.GetMaterialParamLayout()->sizeBytes);
	EXPECT_EQ(compiled->bindings.size(), reference.bindings.size());
	EXPECT_EQ(compiled->integrator, ShaderTemplate::Unlit);
	ASSERT_NE(compiled->GetVariant(SHADER_BASE_VARIANT), nullptr);
	EXPECT_TRUE(compiled->GetVariant(SHADER_BASE_VARIANT)->HasProgram(ShaderProgramKind::Forward));
	EXPECT_TRUE(compiled->GetVariant(SHADER_BASE_VARIANT)->HasProgram(ShaderProgramKind::Depth));
	Delete(compiled);
}

TEST_F(ShaderCompileQueueFixture, FailedImportHandsNothing) {
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Broken.hlsl", "#pragma shalltear_template unlit\nthis is not hlsl\n");
	const u32 liveBefore = LiveShaders();

	const ShaderCompileTicket ticket = ShaderCompileQueue::Submit(Path("project://Broken.hlsl"));
	ShaderAsset* compiled = nullptr;
	EXPECT_EQ(Await(ticket, compiled), ShaderCompileStatus::Failed);
	EXPECT_EQ(compiled, nullptr);
	EXPECT_EQ(LiveShaders(), liveBefore);

	EXPECT_EQ(ShaderCompileQueue::Collect(ticket, compiled), ShaderCompileStatus::Unknown);
}

TEST_F(ShaderCompileQueueFixture, FailedImportHandsBackWhatCompilerSaid) {
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Broken.hlsl", "#pragma shalltear_template unlit\nthis is not hlsl\n");
	const ShaderCompileTicket ticket = ShaderCompileQueue::Submit(Path("project://Broken.hlsl"));
	ShaderAsset* compiled = nullptr;
	str messages;
	ShaderCompileStatus status = ShaderCompileStatus::Pending;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
	while ((status = ShaderCompileQueue::Collect(ticket, compiled, &messages)) == ShaderCompileStatus::Pending &&
		   std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	ASSERT_EQ(status, ShaderCompileStatus::Failed);
	EXPECT_NE(messages.find("Broken.hlsl:2:"), str::npos) << messages;
	EXPECT_NE(messages.find("error"), str::npos) << messages;
}

TEST_F(ShaderCompileQueueFixture, CancelledTicketFreesShader) {
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Surface.hlsl", SURFACE_SHADER);
	const u32 liveBefore = LiveShaders();
	const ShaderCompileStats before = ShaderCompileQueue::GetStats();

	ShaderCompileTicket tickets[4] = {};
	for (ShaderCompileTicket& ticket : tickets) {
		ticket = ShaderCompileQueue::Submit(Path("project://Surface.hlsl"));
	}
	for (ShaderCompileTicket ticket : tickets) {
		ShaderCompileQueue::Cancel(ticket);
		ShaderAsset* compiled = nullptr;
		EXPECT_EQ(ShaderCompileQueue::Collect(ticket, compiled), ShaderCompileStatus::Unknown);
		EXPECT_EQ(compiled, nullptr);
	}

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
	while (LiveShaders() != liveBefore && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		(void)ShaderCompileQueue::GetStats();
	}
	EXPECT_EQ(LiveShaders(), liveBefore);
	const ShaderCompileStats after = ShaderCompileQueue::GetStats();
	EXPECT_LT(after.finished - before.finished, 4u);
}

TEST_F(ShaderCompileQueueFixture, TerminateFreesWhatNobodyCollected) {
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Surface.hlsl", SURFACE_SHADER);
	const u32 liveBefore = LiveShaders();
	ShaderCompileQueue::Submit(Path("project://Surface.hlsl"));
	ShaderCompileQueue::Submit(Path("project://Surface.hlsl"));
	ShaderCompileQueue::Submit(Path("project://Surface.hlsl"));
	EXPECT_GT(LiveShaders(), liveBefore);

	ShaderCompileQueue::Terminate();
	EXPECT_EQ(LiveShaders(), liveBefore);

	const ShaderCompileTicket ticket = ShaderCompileQueue::Submit(Path("project://Surface.hlsl"));
	ShaderAsset* compiled = nullptr;
	EXPECT_EQ(Await(ticket, compiled), ShaderCompileStatus::Succeeded);
	if (compiled) {
		Delete(compiled);
	}
}

TEST_F(ShaderCompileQueueFixture, QueuedKeywordSetAdopted) {
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Keyed.hlsl", "#pragma shalltear_template unlit\n"
								   "#pragma shalltear_keyword MOSS\n"
								   "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
								   "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
								   "#ifdef MOSS\n"
								   "\tsurface.baseColor = tint.gbr;\n"
								   "#else\n"
								   "\tsurface.baseColor = tint.rgb;\n"
								   "#endif\n"
								   "}\n");
	const Path path = FileSystem::Resolve(Path("project://Keyed.hlsl"));
	ShaderAsset live;
	ASSERT_TRUE(live.Import(path));
	const str moss[] = {str("MOSS")};
	const u64 hash = ShaderAsset::KeywordSetHash(moss);
	ASSERT_EQ(live.FindVariant(hash), SHADER_BASE_VARIANT);

	const ShaderCompileStats before = ShaderCompileQueue::GetStats();
	ShaderAsset* compiled = nullptr;
	ASSERT_EQ(Await(ShaderCompileQueue::Submit(path, moss), compiled), ShaderCompileStatus::Succeeded);
	ASSERT_NE(compiled, nullptr);
	EXPECT_EQ(ShaderCompileQueue::GetStats().compilesElsewhere, before.compilesElsewhere);
	EXPECT_NE(compiled->FindVariant(hash), SHADER_BASE_VARIANT);

	EXPECT_TRUE(live.AdoptVariant(*compiled, hash));
	EXPECT_NE(live.FindVariant(hash), SHADER_BASE_VARIANT);
	EXPECT_FALSE(live.AdoptVariant(*compiled, hash));
	Delete(compiled);
}

TEST_F(ShaderCompileQueueFixture, MismatchedVariantNotAdopted) {
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Before.hlsl", "#pragma shalltear_template unlit\n"
									"#pragma shalltear_keyword MOSS\n"
									"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
									"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
									"\tsurface.baseColor = tint.rgb;\n"
									"}\n");
	WriteProjectFile("After.hlsl", "#pragma shalltear_template unlit\n"
								   "#pragma shalltear_keyword MOSS\n"
								   "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; float4 extra; };\n"
								   "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
								   "\tsurface.baseColor = tint.rgb + extra.rgb;\n"
								   "}\n");
	ShaderAsset live;
	ASSERT_TRUE(live.Import(FileSystem::Resolve(Path("project://Before.hlsl"))));
	const str moss[] = {str("MOSS")};
	const u64 hash = ShaderAsset::KeywordSetHash(moss);
	ShaderAsset* compiled = nullptr;
	ASSERT_EQ(Await(ShaderCompileQueue::Submit(FileSystem::Resolve(Path("project://After.hlsl")), moss), compiled),
			  ShaderCompileStatus::Succeeded);
	ASSERT_NE(compiled, nullptr);
	ASSERT_NE(compiled->FindVariant(hash), SHADER_BASE_VARIANT);
	EXPECT_FALSE(live.AdoptVariant(*compiled, hash));
	EXPECT_EQ(live.FindVariant(hash), SHADER_BASE_VARIANT);
	Delete(compiled);
}
