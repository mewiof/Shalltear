#include "TestsPCH.h"

#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Window.h"

using namespace Shalltear;

namespace {

	struct PresentingWindow {
		RHI::Device* device = nullptr;
		Handle<Window> window;
		RHI::SwapChain* swapChain = nullptr;
		u32 framesPresented = 0;
		u32 framesSkipped = 0;

		bool Open() {
			RHI::DeviceDesc deviceDesc;
			device = RHI::Device::Create(deviceDesc);
			if (!device) {
				return false;
			}
			WindowSystem::Initialize();
			WindowDesc windowDesc;
			windowDesc.title = "SwapChainResizeCostTest";
			windowDesc.clientSize = {320, 240};
			windowDesc.flags = WindowFlags::Resizable | WindowFlags::Hidden;
			window = WindowSystem::Create(windowDesc);
			if (!window.IsValid()) {
				return false;
			}
			NativeWindowHandle native = window.Get()->GetNativeHandle();
			RHI::NativeSwapChainDesc swapDesc;
			swapDesc.hwnd = native.hwnd;
			swapDesc.hinstance = native.hinstance;
			swapDesc.sizePx = window.Get()->GetClientSize();
			swapChain = device->CreateSwapChainForWindow(swapDesc);
			return swapChain != nullptr;
		}

		~PresentingWindow() {
			if (device && swapChain) {
				device->DestroySwapChain(swapChain);
			}
			if (window.IsValid()) {
				WindowSystem::RequestDestroy(window);
				WindowSystem::PumpEvents();
			}
			WindowSystem::Terminate();
			if (device) {
				RHI::Device::Destroy(device);
			}
		}

		RHI::TextureHandle Frame() {
			RHI::CommandList* cmd = device->BeginFrame();
			RHI::TextureHandle backbuffer = swapChain->AcquireNextImage();
			if (backbuffer.IsValid()) {
				cmd->TransitionTexture(backbuffer, RHI::ResourceState::RenderTarget);
				RHI::RenderPassDesc::Attachment attachment;
				attachment.texture = backbuffer;
				attachment.clearColor = Color::Blue();
				RHI::RenderPassDesc pass;
				pass.colorAttachments = {&attachment, 1};
				cmd->BeginRenderPass(pass);
				cmd->EndRenderPass();
				cmd->TransitionTexture(backbuffer, RHI::ResourceState::Present);
			}
			device->EndFrame();
			if (backbuffer.IsValid()) {
				swapChain->Present();
				++framesPresented;
			} else {
				++framesSkipped;
			}
			return backbuffer;
		}

		RHI::TextureHandle ResizeTick(i32 width, i32 height) {
			window.Get()->SetClientSize({width, height});
			WindowSystem::PumpEvents();
			return Frame();
		}
	};

} // anonymous namespace

TEST(SwapChainResizeCostTest, LiveResizeKeepsPresenting) {
	PresentingWindow rig;
	if (!rig.Open()) {
		GTEST_SKIP() << "no Vulkan device that can present";
	}
	rig.Frame();

	constexpr u32 TICKS = 16;
	for (u32 tick = 0; tick < TICKS; ++tick) {
		ASSERT_TRUE(rig.ResizeTick(320 + static_cast<i32>(tick) * 8, 240).IsValid());
	}

	EXPECT_EQ(rig.framesSkipped, 0u) << rig.framesSkipped << " frames";
	EXPECT_EQ(rig.framesPresented, TICKS + 1);

	const Vec2I released = rig.window.Get()->GetClientSize();
	rig.swapChain->Recreate(released, true);
	EXPECT_EQ(rig.swapChain->GetSize(), released);
	ASSERT_TRUE(rig.Frame().IsValid());
}

TEST(SwapChainResizeCostTest, RebuildInvalidatesHandles) {
	PresentingWindow rig;
	if (!rig.Open()) {
		GTEST_SKIP() << "no Vulkan device that can present";
	}
	rig.Frame();

	Vector<RHI::TextureHandle> before{GetMemoryResource(MemTag::Debug)};
	for (u32 i = 0; i < 4; ++i) {
		RHI::TextureHandle handle = rig.Frame();
		ASSERT_TRUE(handle.IsValid());
		before.push_back(handle);
	}

	const Vec2I sizeBefore = rig.swapChain->GetSize();
	for (u32 tick = 0; tick < 8 && rig.swapChain->GetSize() == sizeBefore; ++tick) {
		rig.ResizeTick(640 + static_cast<i32>(tick) * 8, 480);
	}
	ASSERT_NE(rig.swapChain->GetSize(), sizeBefore);

	for (RHI::TextureHandle handle : before) {
		EXPECT_EQ(rig.device->GetTextureSize(handle), Vec2I{});
	}
}

TEST(SwapChainResizeCostTest, ExplicitRecreateTakesSizeItIsGiven) {
	PresentingWindow rig;
	if (!rig.Open()) {
		GTEST_SKIP() << "no Vulkan device that can present";
	}
	rig.Frame();

	rig.window.Get()->SetClientSize({512, 384});
	WindowSystem::PumpEvents();
	rig.swapChain->Recreate({512, 384}, true);

	EXPECT_EQ(rig.swapChain->GetSize(), (Vec2I{512, 384}));
	ASSERT_TRUE(rig.Frame().IsValid());
	EXPECT_EQ(rig.framesSkipped, 0u);
}
