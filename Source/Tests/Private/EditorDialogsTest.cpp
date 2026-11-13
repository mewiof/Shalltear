#include "TestsPCH.h"

#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Editor/EditorDialogs.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace Shalltear;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {640, 480};
		return desc;
	}

	bool DrainUntilSettled() {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (EditorDialogs::GetPendingPickCount() > 0) {
			if (std::chrono::steady_clock::now() > deadline) {
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			EditorDialogs::Drain();
		}
		return true;
	}

	struct StandInDialog {
		explicit StandInDialog(EditorDialogs::FileDialogFn dialog) {
			EditorDialogs::SetFileDialog(std::move(dialog));
		}
		~StandInDialog() {
			EditorDialogs::SetFileDialog({});
		}
	};

} // anonymous namespace

TEST(EditorDialogsTest, PickedFilesArriveWhereDrainRuns) {
	std::atomic<bool> ranOnCaller{true};
	const std::thread::id caller = std::this_thread::get_id();
	StandInDialog dialog([&ranOnCaller, caller](const EditorDialogs::FileDialogDesc& desc, Vector<str>& out, str&) {
		ranOnCaller = std::this_thread::get_id() == caller;
		out.push_back(desc.physicalFolder + "/a.png");
		out.push_back(desc.physicalFolder + "/b.png");
		return true;
	});

	UI::Context ctx(Desc(), nullptr);
	Vector<str> picked{GetMemoryResource(MemTag::Debug)};
	bool deliveredOnCaller = false;
	EditorDialogs::FileDialogDesc desc;
	desc.title = "Pick Art";
	desc.physicalFolder = "C:/Art";
	desc.multiple = true;
	EditorDialogs::PickFiles(ctx, desc, [&](std::span<const str> paths) {
		picked.assign(paths.begin(), paths.end());
		deliveredOnCaller = std::this_thread::get_id() == caller;
	});
	EXPECT_EQ(EditorDialogs::GetPendingPickCount(), 1u);
	EXPECT_TRUE(picked.empty());

	ASSERT_TRUE(DrainUntilSettled());
	ASSERT_EQ(picked.size(), 2u);
	EXPECT_EQ(picked[0], "C:/Art/a.png");
	EXPECT_EQ(picked[1], "C:/Art/b.png");
	EXPECT_TRUE(deliveredOnCaller);
	EXPECT_FALSE(ranOnCaller.load());
}

TEST(EditorDialogsTest, CancelledOrUnavailableDialogCallsNothing) {
	UI::Context ctx(Desc(), nullptr);
	u32 calls = 0;
	{
		StandInDialog cancelled([](const EditorDialogs::FileDialogDesc&, Vector<str>&, str&) { return true; });
		EditorDialogs::PickFiles(ctx, {}, [&calls](std::span<const str>) { ++calls; });
		ASSERT_TRUE(DrainUntilSettled());
	}
	{
		StandInDialog unavailable([](const EditorDialogs::FileDialogDesc&, Vector<str>& out, str& error) {
			out.push_back("C:/never.png");
			error = "no dialog on this desktop";
			return false;
		});
		EditorDialogs::PickFiles(ctx, {}, [&calls](std::span<const str>) { ++calls; });
		ASSERT_TRUE(DrainUntilSettled());
	}
	EXPECT_EQ(calls, 0u);
}
