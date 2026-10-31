#include "TestsPCH.h"

#include "Shalltear/Core/BuildInfo.h"
#include "Shalltear/Core/Module/EngineContext.h"

using namespace Shalltear;

TEST(EngineContextTest, CaptureFillsEveryField) {
	EngineContext context{};
	CaptureEngineContext(context);

	EXPECT_EQ(context.version, SLT_ENGINE_CONTEXT_VERSION);
	EXPECT_EQ(context.size, sizeof(EngineContext));
	EXPECT_EQ(context.engineHash, BuildInfo::GetEngineHash());
	EXPECT_NE(context.stateTable, nullptr);
}

TEST(EngineContextTest, CaptureIsStableAcrossCalls) {
	EngineContext first{};
	EngineContext second{};
	CaptureEngineContext(first);
	CaptureEngineContext(second);

	EXPECT_EQ(first.stateTable, second.stateTable);
	EXPECT_EQ(first.engineHash, second.engineHash);
}

TEST(EngineContextTest, SelfBindSucceedsAndIsHarmless) {
	EngineContext context{};
	CaptureEngineContext(context);

	u32 typeCountBefore = TypeRegistry::GetTypeCount();
	size_t dispatchBefore = VariantDispatch::GetEntryCount();
	size_t scriptingBytesBefore = MemoryManager::GetBytesForTag(MemTag::Scripting);

	ASSERT_TRUE(BindEngineContext(&context));

	EXPECT_EQ(TypeRegistry::GetTypeCount(), typeCountBefore);
	EXPECT_EQ(VariantDispatch::GetEntryCount(), dispatchBefore);
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scripting), scriptingBytesBefore);

	void* block = Allocate(256, MemTag::Scripting);
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scripting), scriptingBytesBefore + 256);
	Free(block);
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scripting), scriptingBytesBefore);
}

TEST(EngineContextTest, BindRejectsNullAndMismatches) {
	EXPECT_FALSE(BindEngineContext(nullptr));

	EngineContext context{};
	CaptureEngineContext(context);

	{
		EngineContext bad = context;
		bad.version = context.version + 1;
		EXPECT_FALSE(BindEngineContext(&bad));
	}
	{
		EngineContext bad = context;
		bad.size = context.size + 8;
		EXPECT_FALSE(BindEngineContext(&bad));
	}
	{
		EngineContext bad = context;
		bad.engineHash = context.engineHash ^ 0xDEADBEEFULL;
		EXPECT_FALSE(BindEngineContext(&bad));
	}
	{
		EngineContext bad = context;
		bad.stateTable = nullptr;
		EXPECT_FALSE(BindEngineContext(&bad));
	}

	EXPECT_TRUE(BindEngineContext(&context));
}
