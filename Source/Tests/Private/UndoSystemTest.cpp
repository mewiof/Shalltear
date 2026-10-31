#include "TestsPCH.h"

#include "Shalltear/Core/Math.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/UndoSystem.h"

using namespace Shalltear;

namespace {

	class UndoTarget : public Object {
	public:
		SLT_REFLECT_CLASS(UndoTarget)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(number)
		SLT_REFLECT_FIELD(title)
		SLT_END_REFLECT()

		i32 number = 0;
		str title;
	};

	class NumberProbe {
	public:
		explicit NumberProbe(UndoTarget& watched) : _watched(watched) {}

		i32 revertSaw = -1;

		class Command final : public IEditorCommand {
		public:
			explicit Command(NumberProbe& probe) : _probe(probe) {}
			void Apply() override {}
			void Revert() override { _probe.revertSaw = _probe._watched.number; }
			str GetLabel() const override { return str("Probe"); }
		private:
			NumberProbe& _probe;
		};
	private:
		UndoTarget& _watched;
	};

	class UndoSystemTest : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			UndoSystem::Initialize();
		}
		void TearDown() override {
			UndoSystem::Terminate();
		}

		static UniquePtr<IEditorCommand> SetNumber(Handle<Object> target, i32 before, i32 after) {
			return MakeUnique<PropertySetCommand>(target, "number"_hash, Variant(before), Variant(after), "Number");
		}
	};

} // anonymous namespace

TEST_F(UndoSystemTest, PushExecutesTheCommand) {
	UndoTarget target;
	UndoSystem::Push(SetNumber(Handle<Object>(&target), 0, 5));
	EXPECT_EQ(target.number, 5);
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 1u);
	EXPECT_TRUE(UndoSystem::CanUndo());
	EXPECT_EQ(UndoSystem::GetUndoLabel(), "Number");
}

TEST_F(UndoSystemTest, UndoRevertsRedoReapplies) {
	UndoTarget target;
	UndoSystem::Push(SetNumber(Handle<Object>(&target), 0, 5));

	EXPECT_TRUE(UndoSystem::Undo());
	EXPECT_EQ(target.number, 0);
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 0u);
	EXPECT_EQ(UndoSystem::GetRedoDepth(), 1u);

	EXPECT_TRUE(UndoSystem::Redo());
	EXPECT_EQ(target.number, 5);
	EXPECT_EQ(UndoSystem::GetRedoDepth(), 0u);

	EXPECT_FALSE(UndoSystem::Redo());
}

TEST_F(UndoSystemTest, NewPushClearsTheRedoStack) {
	UndoTarget target;
	UndoSystem::Push(SetNumber(Handle<Object>(&target), 0, 5));
	UndoSystem::Undo();
	ASSERT_EQ(UndoSystem::GetRedoDepth(), 1u);

	UndoSystem::Push(SetNumber(Handle<Object>(&target), 0, 9));
	EXPECT_EQ(UndoSystem::GetRedoDepth(), 0u);
	EXPECT_EQ(target.number, 9);
}

TEST_F(UndoSystemTest, GroupCoalescesSameTargetAndField) {
	UndoTarget target;

	UndoSystem::BeginGroup("Scrub Number");
	UndoSystem::Push(SetNumber(Handle<Object>(&target), 0, 1));
	UndoSystem::Push(SetNumber(Handle<Object>(&target), 1, 2));
	UndoSystem::Push(SetNumber(Handle<Object>(&target), 2, 3));
	UndoSystem::EndGroup();

	EXPECT_EQ(target.number, 3);
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 1u);
	EXPECT_EQ(UndoSystem::GetUndoLabel(), "Scrub Number");

	UndoSystem::Undo();
	EXPECT_EQ(target.number, 0);
	UndoSystem::Redo();
	EXPECT_EQ(target.number, 3);
}

TEST_F(UndoSystemTest, GroupKeepsDistinctFieldsAsSeparateCommands) {
	UndoTarget target;
	target.title = "before";

	UndoSystem::BeginGroup("Edit Both");
	UndoSystem::Push(SetNumber(Handle<Object>(&target), 0, 7));
	UndoSystem::Push(MakeUnique<PropertySetCommand>(
		Handle<Object>(&target), "title"_hash, Variant(str("before")), Variant(str("after")), "Title"));
	UndoSystem::EndGroup();

	EXPECT_EQ(UndoSystem::GetUndoDepth(), 1u);
	UndoSystem::Undo();
	EXPECT_EQ(target.number, 0);
	EXPECT_EQ(target.title, "before");
}

TEST_F(UndoSystemTest, GroupCoalescesInterleavedTargets) {
	UndoTarget first;
	UndoTarget second;
	UndoTarget third;

	const size_t baselineBytes = MemoryManager::GetBytesForTag(MemTag::Editor);

	UndoSystem::BeginGroup("Move");
	for (i32 frame = 1; frame <= 512; ++frame) {
		UndoSystem::Push(SetNumber(Handle<Object>(&first), 0, frame));
		UndoSystem::Push(SetNumber(Handle<Object>(&second), 0, frame * 2));
		UndoSystem::Push(SetNumber(Handle<Object>(&third), 0, frame * 3));
	}
	UndoSystem::EndGroup();

	EXPECT_LT(MemoryManager::GetBytesForTag(MemTag::Editor) - baselineBytes, 1024u);

	EXPECT_EQ(first.number, 512);
	EXPECT_EQ(second.number, 1024);
	EXPECT_EQ(third.number, 1536);

	ASSERT_EQ(UndoSystem::GetUndoDepth(), 1u);
	UndoSystem::Undo();
	EXPECT_EQ(first.number, 0);
	EXPECT_EQ(second.number, 0);
	EXPECT_EQ(third.number, 0);

	UndoSystem::Redo();
	EXPECT_EQ(first.number, 512);
	EXPECT_EQ(second.number, 1024);
	EXPECT_EQ(third.number, 1536);
}

TEST_F(UndoSystemTest, CoalesceWalkStopsAtNonPropertyCommand) {
	UndoTarget target;
	NumberProbe probe(target);

	UndoSystem::BeginGroup("Gesture");
	UndoSystem::Push(SetNumber(Handle<Object>(&target), 0, 1));
	UndoSystem::Push(MakeUnique<NumberProbe::Command>(probe));
	UndoSystem::Push(SetNumber(Handle<Object>(&target), 1, 2));
	UndoSystem::EndGroup();

	ASSERT_EQ(UndoSystem::GetUndoDepth(), 1u);
	UndoSystem::Undo();
	EXPECT_EQ(probe.revertSaw, 1);
	EXPECT_EQ(target.number, 0);
}

TEST_F(UndoSystemTest, EmptyGroupRecordsNothing) {
	UndoSystem::BeginGroup("Nothing");
	UndoSystem::EndGroup();
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 0u);
}

TEST_F(UndoSystemTest, UndoRefusedWhileGroupOpen) {
	UndoTarget target;
	UndoSystem::Push(SetNumber(Handle<Object>(&target), 0, 5));

	UndoSystem::BeginGroup("Gesture");
	EXPECT_FALSE(UndoSystem::CanUndo());
	EXPECT_FALSE(UndoSystem::Undo());
	UndoSystem::EndGroup();
	EXPECT_TRUE(UndoSystem::Undo());
}

TEST_F(UndoSystemTest, StackCapDropsOldestEntries) {
	UndoTarget target;
	for (i32 i = 0; i < 300; ++i) {
		UndoSystem::Push(SetNumber(Handle<Object>(&target), i, i + 1));
	}
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 256u);
	EXPECT_EQ(target.number, 300);
}

TEST_F(UndoSystemTest, DeadTargetIsToleratedNotFatal) {
	{
		UndoTarget dying;
		UndoSystem::Push(SetNumber(Handle<Object>(&dying), 0, 5));
	}
	EXPECT_TRUE(UndoSystem::Undo());
}

TEST_F(UndoSystemTest, ClearsOnWorldSwapsOnly) {
	UndoTarget target;
	EventBus& bus = EventBus::GetGlobal();

	UndoSystem::Push(SetNumber(Handle<Object>(&target), 0, 1));
	EditorEvents::PlayModeChanged.Publish(bus, {PlayModeState::Playing});
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 0u);

	UndoSystem::Push(SetNumber(Handle<Object>(&target), 1, 2));
	EditorEvents::PlayModeChanged.Publish(bus, {PlayModeState::Paused});
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 1u);
	EditorEvents::PlayModeChanged.Publish(bus, {PlayModeState::Playing});
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 1u);

	EditorEvents::PlayModeChanged.Publish(bus, {PlayModeState::Editing});
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 0u);
}

TEST_F(UndoSystemTest, ClearsOnModuleReloadBegan) {
	UndoTarget target;
	UndoSystem::Push(SetNumber(Handle<Object>(&target), 0, 1));
	UndoSystem::Undo();
	UndoSystem::Push(SetNumber(Handle<Object>(&target), 0, 2));
	EditorEvents::ModuleReloadBegan.Publish(EventBus::GetGlobal(), {});
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 0u);
	EXPECT_EQ(UndoSystem::GetRedoDepth(), 0u);
}

TEST_F(UndoSystemTest, TRSCommandsUpdateWorldMatrices) {
	SceneSystem::Initialize();
	{
		Handle<Scene> scene = SceneSystem::CreateScene("UndoTrs");
		Handle<SceneObject> object = SceneSystem::CreateObject("Mover", scene);

		Mat4 before = object.Get()->GetWorldMatrix();
		EXPECT_FLOAT_EQ(before.GetColumn(3).y, 0.0f);

		UndoSystem::Push(MakeUnique<PropertySetCommand>(
			Handle<Object>(object.Get()), "_localPosition"_hash,
			Variant(Vec3(0.0f, 0.0f, 0.0f)), Variant(Vec3(1.0f, 2.0f, 3.0f)), "Move"));

		Mat4 after = object.Get()->GetWorldMatrix();
		EXPECT_FLOAT_EQ(after.GetColumn(3).x, 1.0f);
		EXPECT_FLOAT_EQ(after.GetColumn(3).y, 2.0f);
		EXPECT_FLOAT_EQ(after.GetColumn(3).z, 3.0f);

		UndoSystem::Undo();
		Mat4 reverted = object.Get()->GetWorldMatrix();
		EXPECT_FLOAT_EQ(reverted.GetColumn(3).x, 0.0f);
		EXPECT_FLOAT_EQ(reverted.GetColumn(3).y, 0.0f);

		UndoSystem::Clear();
	}
	SceneSystem::Terminate();
}
