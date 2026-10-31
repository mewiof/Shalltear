#include "TestsPCH.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/EditorCommands.h"
#include "Shalltear/Editor/UndoSystem.h"

using namespace Shalltear;

namespace {

	struct StateEntry {
		str name;
		i32 count = 0;
		Handle<Object> target;

		SLT_REFLECT_VALUE(StateEntry)
		SLT_REFLECT_FIELD(name)
		SLT_REFLECT_FIELD(count)
		SLT_REFLECT_FIELD(target)
		SLT_END_REFLECT()
	};

	class StateTarget : public Object {
	public:
		SLT_REFLECT_CLASS(StateTarget)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_ARRAY(entries)
		SLT_REFLECT_FIELD(other)
		SLT_END_REFLECT()

		Vector<StateEntry> entries{GetMemoryResource(MemTag::Core)};
		i32 other = 0;
	};

	class FieldStateCommandTest : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			UndoSystem::Initialize();
		}
		void TearDown() override {
			UndoSystem::Terminate();
		}
	};

} // anonymous namespace

TEST_F(FieldStateCommandTest, CaptureRestoresOneField) {
	Object referenced;
	StateTarget target;
	target.entries.resize(1);
	target.entries[0].name = "first";
	target.entries[0].target = Handle<Object>(&referenced);

	str before = CaptureFieldState(target, "entries"_hash);
	ASSERT_FALSE(before.empty());
	EXPECT_EQ(before.find("other"), str::npos) << before;

	target.entries.resize(2);
	target.entries[1].name = "second";
	target.entries[0].target = {};
	str after = CaptureFieldState(target, "entries"_hash);
	UndoSystem::Push(MakeUnique<SetFieldStateCommand>(Handle<Object>(&target), "entries"_hash, before, after, "Edit"));

	target.other = 5;
	ASSERT_TRUE(UndoSystem::Undo());
	ASSERT_EQ(target.entries.size(), 1u);
	EXPECT_EQ(target.entries[0].name, "first");
	EXPECT_EQ(target.other, 5);

	EXPECT_EQ(target.entries[0].target.GetInstanceId(), referenced.GetInstanceId());

	ASSERT_TRUE(UndoSystem::Redo());
	ASSERT_EQ(target.entries.size(), 2u);
	EXPECT_EQ(target.entries[1].name, "second");
	EXPECT_EQ(target.entries[0].target.GetInstanceId(), 0u);
}

TEST_F(FieldStateCommandTest, GestureCoalescesToOneStep) {
	StateTarget target;
	target.entries.resize(1);
	target.entries[0].count = 1;

	UndoSystem::BeginGroup("Drag");
	for (i32 frame = 2; frame <= 4; ++frame) {
		str before = CaptureFieldState(target, "entries"_hash);
		target.entries[0].count = frame;
		str after = CaptureFieldState(target, "entries"_hash);
		UndoSystem::Push(MakeUnique<SetFieldStateCommand>(Handle<Object>(&target), "entries"_hash, before, after, "Drag"));
	}
	UndoSystem::EndGroup();

	ASSERT_EQ(UndoSystem::GetUndoDepth(), 1u);
	ASSERT_TRUE(UndoSystem::Undo());
	EXPECT_EQ(target.entries[0].count, 1);
	ASSERT_TRUE(UndoSystem::Redo());
	EXPECT_EQ(target.entries[0].count, 4);
}

TEST_F(FieldStateCommandTest, FieldObjectDoesNotHaveCapturesNothing) {
	StateTarget target;
	EXPECT_TRUE(CaptureFieldState(target, "missing"_hash).empty());
}
