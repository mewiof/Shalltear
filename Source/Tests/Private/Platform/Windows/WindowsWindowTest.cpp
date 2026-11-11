#include "TestsPCH.h"

#include "Shalltear/Core/StringUtil.h"
#include "Shalltear/Core/Window.h"

#include "TestHelperProcess.h"
#include "WindowTestSupport.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace Shalltear;
using WindowTestSupport::HiddenDesc;

TEST_F(WindowTest, DpiChangeUsesSuggestedRect) {
	WindowDesc desc = HiddenDesc({560, 420});
	desc.position = Vec2I{180, 140};
	desc.flags = WindowFlags::Resizable | WindowFlags::Borderless;
	Handle<Window> window = WindowSystem::Create(desc);
	ASSERT_TRUE(window.IsValid());
	window->Show();
	WindowSystem::PumpEvents();

	HWND hwnd = static_cast<HWND>(window->GetNativeHandle().window);
	ASSERT_NE(hwnd, nullptr);

	RECT suggested{200, 160, 200 + 840, 160 + 630};
	::SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&suggested));
	WindowSystem::PumpEvents();

	EXPECT_EQ(window->GetPosition(), Vec2I(200, 160));
	EXPECT_FLOAT_EQ(window->GetContentScale(), 1.5f);

	RECT windowRect{};
	RECT clientRect{};
	::GetWindowRect(hwnd, &windowRect);
	::GetClientRect(hwnd, &clientRect);
	EXPECT_EQ(clientRect.right - clientRect.left, windowRect.right - windowRect.left);
	EXPECT_EQ(clientRect.bottom - clientRect.top, windowRect.bottom - windowRect.top);
	EXPECT_EQ(window->GetClientSize(), Vec2I(840, 630));
}

TEST_F(WindowTest, MinimizedLiveRectIsParked) {
	WindowDesc desc = HiddenDesc({640, 480});
	desc.position = Vec2I{200, 180};
	Handle<Window> window = WindowSystem::Create(desc);
	ASSERT_TRUE(window.IsValid());

	window->Show();
	window->Minimize();
	WindowSystem::PumpEvents();
	ASSERT_TRUE(window->IsMinimized());

	EXPECT_EQ(window->GetClientSize(), Vec2I(0, 0));
	EXPECT_LT(window->GetPosition().x, 0);
}

namespace Shalltear::TestHelpers {

	int RunWindowHost() {
		WindowSystem::Initialize();
		WindowDesc desc = HiddenDesc({200, 150});
		desc.title = WINDOW_HOST_TITLE;
		if (!WindowSystem::Create(desc).IsValid()) {
			WindowSystem::Terminate();
			return 1;
		}
		for (int tick = 0; tick < 200; ++tick) {
			WindowSystem::PumpEvents();
			::Sleep(50);
		}
		WindowSystem::Terminate();
		return 0;
	}

	int RunShowStateProbe() {
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		::GetStartupInfoW(&startup);
		if ((startup.dwFlags & STARTF_USESHOWWINDOW) == 0 || startup.wShowWindow != SW_HIDE) {
			return ShowStateNoSubstitution;
		}

		WindowSystem::Initialize();
		int result = ShowStateOk;
		{
			WindowDesc desc;
			desc.title = "ShalltearShowStateProbe";
			desc.clientSize = {200, 150};
			desc.position = Vec2I{-20000, -20000};
			desc.flags = WindowFlags::Resizable;

			WindowDesc hiddenDesc = desc;
			hiddenDesc.flags = desc.flags | WindowFlags::Hidden;

			Handle<Window> created = WindowSystem::Create(desc);
			Handle<Window> shown = WindowSystem::Create(hiddenDesc);
			Handle<Window> stayHidden = WindowSystem::Create(hiddenDesc);
			if (!created.IsValid() || !shown.IsValid() || !stayHidden.IsValid()) {
				result = ShowStateCreateFailed;
			} else if (!created->IsVisible()) {
				result = ShowStateCreateNotVisible;
			} else {
				shown->Show();
				WindowSystem::PumpEvents();
				if (!shown->IsVisible()) {
					result = ShowStateShowDidNotShow;
				} else if (stayHidden->IsVisible()) {
					result = ShowStateHiddenWasShown;
				} else {
					WindowPlacement placement;
					placement.position = {-20000, -20000};
					placement.clientSize = {400, 300};
					placement.state = WindowState::Maximized;
					shown->SetPlacement(placement);
					WindowSystem::PumpEvents();
					if (!shown->IsMaximized()) {
						result = ShowStateNotMaximized;
					}
				}
			}
		}
		WindowSystem::Terminate();
		return result;
	}

} // namespace Shalltear::TestHelpers

TEST_F(WindowTest, ShowStateSurvivesLauncherSuppliedShowCommand) {
	wchar_t exePath[MAX_PATH] = {};
	ASSERT_GT(::GetModuleFileNameW(nullptr, exePath, MAX_PATH), 0u);
	std::wstring commandLine = L"\"";
	commandLine += exePath;
	commandLine += L"\" ";
	commandLine += StringUtil::UTF8ToWide(TestHelpers::SHOW_STATE_FLAG);

	HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
	ASSERT_NE(job, nullptr);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESHOWWINDOW;
	startup.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION process{};
	if (!::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
						  CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &process)) {
		::CloseHandle(job);
		GTEST_SKIP() << "couldn't spawn helper process";
	}
	::AssignProcessToJobObject(job, process.hProcess);
	::ResumeThread(process.hThread);

	DWORD wait = ::WaitForSingleObject(process.hProcess, 30000);
	DWORD exitCode = static_cast<DWORD>(-1);
	if (wait == WAIT_OBJECT_0) {
		::GetExitCodeProcess(process.hProcess, &exitCode);
	} else {
		::TerminateProcess(process.hProcess, 1);
	}
	::CloseHandle(process.hThread);
	::CloseHandle(process.hProcess);
	::CloseHandle(job);

	ASSERT_EQ(wait, static_cast<DWORD>(WAIT_OBJECT_0));
	if (exitCode == TestHelpers::ShowStateNoSubstitution) {
		GTEST_SKIP() << "child inherited no STARTUPINFO show command";
	}
	EXPECT_EQ(exitCode, static_cast<DWORD>(TestHelpers::ShowStateOk));
}

TEST_F(WindowTest, ForeignWindowNeverResolved) {
	wchar_t exePath[MAX_PATH] = {};
	ASSERT_GT(::GetModuleFileNameW(nullptr, exePath, MAX_PATH), 0u);
	std::wstring commandLine = L"\"";
	commandLine += exePath;
	commandLine += L"\" ";
	commandLine += StringUtil::UTF8ToWide(TestHelpers::WINDOW_HOST_FLAG);

	HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
	ASSERT_NE(job, nullptr);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	if (!::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
						  CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &process)) {
		::CloseHandle(job);
		GTEST_SKIP() << "couldn't spawn helper process";
	}
	::AssignProcessToJobObject(job, process.hProcess);
	::ResumeThread(process.hThread);

	HWND foreign = nullptr;
	for (int attempt = 0; attempt < 200 && !foreign; ++attempt) {
		foreign = ::FindWindowW(nullptr, StringUtil::UTF8ToWide(TestHelpers::WINDOW_HOST_TITLE).c_str());
		if (!foreign) {
			::Sleep(25);
		}
	}

	if (foreign) {
		Handle<Window> mine = WindowSystem::Create(HiddenDesc());
		ASSERT_TRUE(mine.IsValid());
		RECT rect{};
		::GetWindowRect(foreign, &rect);
		Vec2I foreignCenter{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
		EXPECT_FALSE(WindowSystem::GetWindowAtPoint(foreignCenter).IsValid());
		EXPECT_FALSE(WindowSystem::GetWindowAtPoint({rect.left, rect.top}).IsValid());
	}

	::TerminateProcess(process.hProcess, 0);
	::WaitForSingleObject(process.hProcess, 5000);
	::CloseHandle(process.hThread);
	::CloseHandle(process.hProcess);
	::CloseHandle(job);

	if (!foreign) {
		GTEST_SKIP() << "helper process never published its window";
	}
}

namespace {

	struct HitTestProbe {
		HitTestResult verdict = HitTestResult::Client;
		u32 calls = 0;
	};

	HitTestResult ProbeHitTest(Window&, Vec2I, void* user) {
		auto& probe = *static_cast<HitTestProbe*>(user);
		++probe.calls;
		return probe.verdict;
	}

	Handle<Window> MakeBorderless() {
		WindowDesc desc = HiddenDesc({560, 420});
		desc.position = Vec2I{180, 140};
		desc.flags = WindowFlags::Resizable | WindowFlags::Borderless | WindowFlags::Hidden;
		return WindowSystem::Create(desc);
	}

	LPARAM ScreenPoint(i32 x, i32 y) {
		return MAKELPARAM(static_cast<WORD>(x), static_cast<WORD>(y));
	}

} // anonymous namespace

TEST_F(WindowTest, ContentWinsEdgePixel) {
	Handle<Window> window = MakeBorderless();
	ASSERT_TRUE(window.IsValid());
	HWND hwnd = static_cast<HWND>(window->GetNativeHandle().window);
	ASSERT_NE(hwnd, nullptr);

	HitTestProbe probe;
	window->SetHitTestCallback(&ProbeHitTest, &probe);

	RECT rect{};
	::GetWindowRect(hwnd, &rect);
	const LPARAM edge = ScreenPoint(rect.right - 1, (rect.top + rect.bottom) / 2);

	probe.verdict = HitTestResult::Client;
	EXPECT_EQ(::SendMessageW(hwnd, WM_NCHITTEST, 0, edge), HTRIGHT);
	EXPECT_GT(probe.calls, 0u);

	probe.verdict = HitTestResult::Content;
	EXPECT_EQ(::SendMessageW(hwnd, WM_NCHITTEST, 0, edge), HTCLIENT);
}

TEST_F(WindowTest, ResizeEdgeBeatsCaption) {
	Handle<Window> window = MakeBorderless();
	ASSERT_TRUE(window.IsValid());
	HWND hwnd = static_cast<HWND>(window->GetNativeHandle().window);
	ASSERT_NE(hwnd, nullptr);

	HitTestProbe probe;
	probe.verdict = HitTestResult::Caption;
	window->SetHitTestCallback(&ProbeHitTest, &probe);

	RECT rect{};
	::GetWindowRect(hwnd, &rect);

	EXPECT_EQ(::SendMessageW(hwnd, WM_NCHITTEST, 0, ScreenPoint(rect.left, rect.top)), HTTOPLEFT);
	EXPECT_EQ(::SendMessageW(hwnd, WM_NCHITTEST, 0,
							 ScreenPoint((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2)),
			  HTCAPTION);
}

TEST_F(WindowTest, SetIconFreesReplacedIcons) {
	const auto userObjects = [] { return ::GetGuiResources(::GetCurrentProcess(), GR_USEROBJECTS); };
	const std::vector<u8> rgba(32 * 32 * 4, 200);
	const WindowIconImage image{{32, 32}, rgba};

	{
		Handle<Window> warmup = WindowSystem::Create(HiddenDesc());
		ASSERT_TRUE(warmup.IsValid());
		WindowSystem::RequestDestroy(warmup);
		WindowSystem::PumpEvents();
	}
	const DWORD before = userObjects();
	for (int round = 0; round < 4; ++round) {
		Handle<Window> window = WindowSystem::Create(HiddenDesc());
		ASSERT_TRUE(window.IsValid());
		const DWORD bare = userObjects();
		window->SetIcon({&image, 1});
		const DWORD set = userObjects();
		EXPECT_GT(set, bare);
		window->SetIcon({&image, 1});
		EXPECT_EQ(userObjects(), set);
		window->SetIcon({});
		EXPECT_EQ(userObjects(), bare);
		window->SetIcon({&image, 1});
		WindowSystem::RequestDestroy(window);
		WindowSystem::PumpEvents();
	}
	EXPECT_EQ(userObjects(), before);
}
