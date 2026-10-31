#include "TestsPCH.h"

#include "Shalltear/Editor/EditorDocuments.h"
#include "Shalltear/Editor/EditorExtensions.h"
#include "Shalltear/Editor/GraphDocument.h"
#include "Shalltear/Editor/UndoSystem.h"

using namespace Shalltear;

namespace {

	const int _imageA = 0;
	const int _imageB = 0;

	constexpr std::string_view Pair = R"({"_type":"NodeShader","version":1,"nodes":[
		{"id":1,"def":"Input/Time","x":0,"y":0},
		{"id":2,"def":"Math/Sine","x":200,"y":0}]})";

	struct Window {
		GraphDocument document;
		u32 captures = 0;
		u32 restores = 0;

		Window() {
			str error;
			EXPECT_TRUE(GraphDocumentOps::Read(Pair, document, error)) << error;
		}

		EditorDocumentDesc Desc(const Guid& asset, const void* owner) {
			EditorDocumentDesc desc;
			desc.asset = asset;
			desc.owner = owner;
			desc.capture = [this](str& out) {
				++captures;
				out = GraphDocumentOps::Write(document);
			};
			desc.restore = [this](std::string_view bytes) {
				++restores;
				str error;
				return GraphDocumentOps::Read(bytes, document, error);
			};
			return desc;
		}

		str Bytes() const { return GraphDocumentOps::Write(document); }
	};

	Guid MakeAsset(u64 low) {
		Guid guid;
		guid.high = 0x5eed0000;
		guid.low = low;
		return guid;
	}

	class EditorDocumentsTest : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			UndoSystem::Initialize();
		}
		void TearDown() override {
			EditorDocuments::Terminate();
			UndoSystem::Terminate();
		}
	};

} // anonymous namespace

TEST_F(EditorDocumentsTest, GestureIsOneUndoStep) {
	Window window;
	const Guid asset = MakeAsset(1);
	ASSERT_TRUE(EditorDocuments::Open(window.Desc(asset, &_imageA)));
	const str before = window.Bytes();
	const u64 opened = EditorDocuments::GetRevision(asset);
	EXPECT_NE(opened, 0u);

	EditorDocuments::BeginEdit(asset, "Connect");
	ASSERT_TRUE(GraphDocumentOps::Connect(window.document, 1, "Out", 2, "In"));
	EditorDocuments::EndEdit(asset);
	const str after = window.Bytes();
	ASSERT_NE(after, before);
	ASSERT_EQ(UndoSystem::GetUndoDepth(), 1u);
	EXPECT_EQ(UndoSystem::GetUndoLabel(), "Connect");
	const u64 recorded = EditorDocuments::GetRevision(asset);
	EXPECT_NE(recorded, opened);

	ASSERT_TRUE(UndoSystem::Undo());
	EXPECT_EQ(window.Bytes(), before);
	const u64 undone = EditorDocuments::GetRevision(asset);
	EXPECT_NE(undone, recorded);

	ASSERT_TRUE(UndoSystem::Redo());
	EXPECT_EQ(window.Bytes(), after);
	EXPECT_NE(EditorDocuments::GetRevision(asset), undone);
	EditorDocuments::Close(asset);
	EXPECT_FALSE(EditorDocuments::IsOpen(asset));
	EXPECT_EQ(EditorDocuments::GetRevision(asset), 0u);
}

TEST_F(EditorDocumentsTest, NestedBracketsAreOneStep) {
	Window window;
	const Guid asset = MakeAsset(2);
	ASSERT_TRUE(EditorDocuments::Open(window.Desc(asset, &_imageA)));
	const str before = window.Bytes();

	EditorDocuments::BeginEdit(asset, "Move");
	for (i32 frame = 1; frame <= 5; ++frame) {
		EditorDocuments::BeginEdit(asset, "Move");
		GraphDocumentOps::MoveNode(window.document, 1, Vec2(static_cast<f32>(frame * 10), 0.0f));
		GraphDocumentOps::MoveNode(window.document, 2, Vec2(200.0f + static_cast<f32>(frame * 10), 0.0f));
		EditorDocuments::EndEdit(asset);
		EXPECT_EQ(UndoSystem::GetUndoDepth(), 0u);
	}
	EXPECT_TRUE(UndoSystem::IsGroupOpen());
	EXPECT_FALSE(UndoSystem::Undo());
	EditorDocuments::EndEdit(asset);
	EXPECT_FALSE(UndoSystem::IsGroupOpen());
	ASSERT_EQ(UndoSystem::GetUndoDepth(), 1u);
	ASSERT_TRUE(UndoSystem::Undo());
	EXPECT_EQ(window.Bytes(), before);

	const u32 depth = UndoSystem::GetUndoDepth();
	EditorDocuments::BeginEdit(asset, "Nothing");
	EditorDocuments::EndEdit(asset);
	EXPECT_EQ(UndoSystem::GetUndoDepth(), depth);
	EXPECT_FALSE(UndoSystem::IsGroupOpen());

	UndoSystem::BeginGroup("Someone Else");
	EditorDocuments::EndEdit(asset);
	EXPECT_TRUE(UndoSystem::IsGroupOpen());
	UndoSystem::EndGroup();
}

TEST_F(EditorDocumentsTest, UndoWaitsForSweptImage) {
	Window window;
	const Guid asset = MakeAsset(3);
	ASSERT_TRUE(EditorDocuments::Open(window.Desc(asset, &_imageA)));
	const str original = window.Bytes();

	EditorDocuments::BeginEdit(asset, "First");
	GraphDocumentOps::MoveNode(window.document, 1, Vec2(50.0f, 50.0f));
	EditorDocuments::EndEdit(asset);
	EditorDocuments::BeginEdit(asset, "Second");
	GraphDocumentOps::Connect(window.document, 1, "Out", 2, "In");
	EditorDocuments::EndEdit(asset);
	const str edited = window.Bytes();
	ASSERT_EQ(UndoSystem::GetUndoDepth(), 2u);

	EXPECT_GE(EditorExtensions::Unload(&_imageA), 1u);
	EXPECT_FALSE(EditorDocuments::IsOpen(asset));

	const u32 restoresBefore = window.restores;
	const u32 capturesBefore = window.captures;
	EXPECT_TRUE(UndoSystem::Undo());
	EXPECT_EQ(window.restores, restoresBefore);
	EXPECT_EQ(window.captures, capturesBefore);
	EXPECT_EQ(window.Bytes(), edited);

	Window returned;
	returned.document = window.document;
	ASSERT_TRUE(EditorDocuments::Open(returned.Desc(asset, &_imageA)));
	ASSERT_TRUE(UndoSystem::Undo());
	EXPECT_EQ(window.restores, restoresBefore);
	EXPECT_EQ(returned.restores, 1u);
	EXPECT_EQ(returned.Bytes(), original);
}

TEST_F(EditorDocumentsTest, WindowsShareOneHistory) {
	Window first;
	const Guid asset = MakeAsset(4);
	ASSERT_TRUE(EditorDocuments::Open(first.Desc(asset, &_imageA)));
	Window second;
	ASSERT_TRUE(EditorDocuments::Open(second.Desc(asset, &_imageA)));
	EXPECT_FALSE(EditorDocuments::Open(first.Desc(asset, &_imageB)));

	const str before = first.Bytes();
	EditorDocuments::BeginEdit(asset, "Connect");
	GraphDocumentOps::Connect(first.document, 1, "Out", 2, "In");
	EditorDocuments::EndEdit(asset);

	EditorDocuments::Close(asset);
	ASSERT_TRUE(EditorDocuments::IsOpen(asset));
	ASSERT_TRUE(UndoSystem::Undo());
	EXPECT_EQ(first.Bytes(), before);

	EditorDocuments::Close(asset);
	EXPECT_FALSE(EditorDocuments::IsOpen(asset));
	const u32 restores = first.restores;
	ASSERT_TRUE(UndoSystem::Redo());
	EXPECT_EQ(first.restores, restores);
}

TEST_F(EditorDocumentsTest, MidGestureCloseRecordsNothing) {
	Window window;
	const Guid asset = MakeAsset(5);
	ASSERT_TRUE(EditorDocuments::Open(window.Desc(asset, &_imageA)));
	EditorDocuments::BeginEdit(asset, "Move");
	GraphDocumentOps::MoveNode(window.document, 1, Vec2(70.0f, 0.0f));
	EditorDocuments::Close(asset);
	EXPECT_FALSE(UndoSystem::IsGroupOpen());
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 0u);
	EditorDocuments::EndEdit(asset);
	EXPECT_FALSE(UndoSystem::IsGroupOpen());

	ASSERT_TRUE(EditorDocuments::Open(window.Desc(asset, &_imageA)));
	EditorDocuments::BeginEdit(asset, "Move");
	EXPECT_EQ(EditorDocuments::UnregisterOwnedBy(&_imageA), 1u);
	EXPECT_FALSE(UndoSystem::IsGroupOpen());
	EXPECT_EQ(EditorDocuments::UnregisterOwnedBy(nullptr), 0u);
}
