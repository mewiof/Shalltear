#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/UI.h"
#include "Shalltear/Core/Window.h"

#include "ZoneProbe.h"

#include <algorithm>
#include <filesystem>

using namespace Shalltear;

namespace {

	std::filesystem::path FindRepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
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

	class UISurfaceFixture : public ::testing::Test {
	protected:
		bool _mounted = false;

		void SetUp() override {
			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
		}

		void TearDown() override {
			UISystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
		}

		Handle<Window> MakeHiddenWindow() {
			WindowDesc desc;
			desc.title = "UISurfaceTest";
			desc.clientSize = {160, 120};
			desc.flags = WindowFlags::Hidden;
			return WindowSystem::Create(desc);
		}
	};

	struct RGBA {
		u8 r = 0, g = 0, b = 0, a = 0;
		bool operator==(const RGBA&) const = default;
	};

	RGBA TexelAt(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
		size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
		return {pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
	}

	bool ReadbackSurface(UISurfaceId surface, Vector<u8>& outRGBA8, Vec2I& outSize) {
		RHI::TextureHandle texture = UISystem::GetSurfaceTexture(surface);
		UI::Context* context = UISystem::GetSurfaceContext(surface);
		RHI::Device* device = RenderSystem::GetDevice();
		if (!texture.IsValid() || !context || !device) {
			return false;
		}
		outSize = context->GetSize();
		outRGBA8.resize(static_cast<size_t>(outSize.x) * outSize.y * 4);
		device->ReadbackTexture(texture, {outRGBA8.data(), outRGBA8.size()});
		return true;
	}

	UI::ElementHandle FillWith(UI::Context& context, UI::ElementHandle parent, Color color) {
		UI::ElementHandle element = context.CreateElement();
		context.AppendChild(parent, element);
		UI::Style style;
		style.width = UI::Length::Percent(1.0f);
		style.height = UI::Length::Percent(1.0f);
		style.background = UI::Brush::SolidColor(color);
		context.SetStyle(element, style);
		return element;
	}

	InputEvent MouseMove(i32 x, i32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseMove;
		event.mousePosPx = {x, y};
		return event;
	}
	InputEvent MouseAt(InputEventType type, i32 x, i32 y) {
		InputEvent event{};
		event.type = type;
		event.button = MouseButton::Left;
		event.mousePosPx = {x, y};
		return event;
	}

} // anonymous namespace

TEST_F(UISurfaceFixture, SurfaceIdNeverReused) {
	UISurfaceId first = UISystem::CreateSurface({64, 64});
	UISurfaceId second = UISystem::CreateSurface({32, 16});
	ASSERT_NE(first, INVALID_UI_SURFACE);
	ASSERT_NE(second, INVALID_UI_SURFACE);
	EXPECT_NE(first, second);

	UI::Context* firstContext = UISystem::GetSurfaceContext(first);
	UI::Context* secondContext = UISystem::GetSurfaceContext(second);
	ASSERT_NE(firstContext, nullptr);
	ASSERT_NE(secondContext, nullptr);
	EXPECT_NE(firstContext, secondContext);
	EXPECT_EQ(firstContext->GetSize(), (Vec2I{64, 64}));
	EXPECT_EQ(secondContext->GetSize(), (Vec2I{32, 16}));

	UISystem::DestroySurface(first);
	UISurfaceId third = UISystem::CreateSurface({8, 8});
	EXPECT_NE(third, first);
	EXPECT_NE(third, second);

	UISystem::DestroySurface(second);
	UISystem::DestroySurface(third);
	EXPECT_EQ(UISystem::CreateSurface({0, 32}), INVALID_UI_SURFACE);
	EXPECT_EQ(UISystem::CreateSurface({16, -1}), INVALID_UI_SURFACE);
}

TEST_F(UISurfaceFixture, DestroyingSurfaceKillsContextNotOrphaningIt) {
	UISurfaceId surface = UISystem::CreateSurface({64, 64});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);
	EXPECT_TRUE(UISystem::IsContextLive(context));

	UISystem::DestroySurface(surface);
	EXPECT_EQ(UISystem::GetSurfaceContext(surface), nullptr);
	EXPECT_FALSE(UISystem::GetSurfaceTexture(surface).IsValid());
	EXPECT_FALSE(UISystem::IsContextLive(context));
	UISystem::DestroySurface(surface);
}

TEST_F(UISurfaceFixture, AppContextInitializerDoesNotDressSurface) {
	int windowContexts = 0;
	int initialized = 0;
	UISystem::SetContextInitializer([&](UI::Context& context) {
		++initialized;
		context.AddClass(context.GetRoot(), "themed"_hash);
	});

	UISurfaceId surface = UISystem::CreateSurface({64, 64});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* surfaceContext = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(surfaceContext, nullptr);
	EXPECT_EQ(initialized, 0);
	EXPECT_FALSE(surfaceContext->HasClass(surfaceContext->GetRoot(), "themed"_hash));

	Handle<Window> window = MakeHiddenWindow();
	if (window.IsValid()) {
		UI::Context* windowContext = UISystem::CreateContext(window);
		ASSERT_NE(windowContext, nullptr);
		++windowContexts;
		EXPECT_EQ(initialized, 1);
		EXPECT_TRUE(windowContext->HasClass(windowContext->GetRoot(), "themed"_hash));
		WindowSystem::RequestDestroy(window);
		WindowSystem::PumpEvents();
	}
	ASSERT_EQ(windowContexts, 1);

	UISystem::SetContextInitializer(nullptr);
	UISystem::DestroySurface(surface);
}

TEST_F(UISurfaceFixture, SurfaceUsesGivenSizeAndScale) {
	UISurfaceId surface = UISystem::CreateSurface({200, 100}, 2.0f);
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);
	EXPECT_FLOAT_EQ(context->GetContentScale(), 2.0f);

	UI::ElementHandle box = context->CreateElement();
	context->AppendChild(context->GetRoot(), box);
	UI::Style style;
	style.width = UI::Length::Pixel(20.0f);
	style.height = UI::Length::Pixel(10.0f);
	context->SetStyle(box, style);
	UISystem::Update();

	EXPECT_FLOAT_EQ(context->GetLayout(box).borderBox.w, 40.0f);
	EXPECT_FLOAT_EQ(context->GetLayout(box).borderBox.h, 20.0f);

	UISystem::ResizeSurface(surface, {80, 40}, 1.0f);
	UISystem::Update();
	EXPECT_EQ(context->GetSize(), (Vec2I{80, 40}));
	EXPECT_FLOAT_EQ(context->GetContentScale(), 1.0f);
	EXPECT_FLOAT_EQ(context->GetLayout(box).borderBox.w, 20.0f);

	UISystem::DestroySurface(surface);
}

TEST_F(UISurfaceFixture, SurfaceWorksWithoutDevice) {
	UISurfaceId surface = UISystem::CreateSurface({64, 64});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	ASSERT_NE(UISystem::GetSurfaceContext(surface), nullptr);
	EXPECT_EQ(UISystem::GetSurfaceTexture(surface).IsValid(), RenderSystem::IsRenderingAvailable());
	UISystem::DestroySurface(surface);
}

TEST_F(UISurfaceFixture, SurfaceCountsAsContextInFrameStats) {
	EXPECT_EQ(UISystem::GetStats().contexts, 0u);
	UISurfaceId surface = UISystem::CreateSurface({64, 64});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	EXPECT_EQ(UISystem::GetStats().contexts, 1u);
	UISystem::DestroySurface(surface);
	EXPECT_EQ(UISystem::GetStats().contexts, 0u);
}

TEST_F(UISurfaceFixture, InjectedInputReachesSurface) {
	UISurfaceId surface = UISystem::CreateSurface({100, 100});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);

	UI::ElementHandle box = context->CreateElement();
	context->AppendChild(context->GetRoot(), box);
	UI::Style style;
	style.width = UI::Length::Pixel(50.0f);
	style.height = UI::Length::Pixel(50.0f);
	context->SetStyle(box, style);
	int clicks = 0;
	context->Events(box).onClick = [&] { ++clicks; };
	UISystem::Update();

	context->InjectInput(MouseMove(10, 10));
	context->InjectInput(MouseAt(InputEventType::MouseDown, 10, 10));
	context->InjectInput(MouseAt(InputEventType::MouseUp, 10, 10));
	UISystem::Update();
	EXPECT_EQ(clicks, 1);
	EXPECT_EQ(context->GetHovered(), box);

	UISystem::DestroySurface(surface);
}

TEST_F(UISurfaceFixture, DetachedSurfaceRenders) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	UISurfaceId surface = UISystem::CreateSurface({32, 32});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);
	FillWith(*context, context->GetRoot(), Color::Red());

	UISystem::Update();
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	ASSERT_EQ(size, (Vec2I{32, 32}));
	EXPECT_EQ(TexelAt(pixels, size, 16, 16), (RGBA{255, 0, 0, 255}));
	EXPECT_EQ(TexelAt(pixels, size, 0, 0), (RGBA{255, 0, 0, 255}));
	EXPECT_EQ(TexelAt(pixels, size, 31, 31), (RGBA{255, 0, 0, 255}));
	EXPECT_GT(UISystem::GetStats().quads, 0u);

	UISystem::DestroySurface(surface);
}

TEST_F(UISurfaceFixture, SurfaceIsClearedEachFrameNotAccumulating) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	UISurfaceId surface = UISystem::CreateSurface({16, 16});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);

	UI::ElementHandle fill = FillWith(*context, context->GetRoot(), Color::Red());
	UISystem::Update();
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	ASSERT_EQ(TexelAt(pixels, size, 8, 8), (RGBA{255, 0, 0, 255}));

	context->DestroyElement(fill);
	UISystem::Update();
	RenderSystem::RenderAll();

	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	EXPECT_EQ(TexelAt(pixels, size, 8, 8), (RGBA{0, 0, 0, 0}));

	UISystem::DestroySurface(surface);
}

TEST_F(UISurfaceFixture, SurfacesRenderSeparately) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	UISurfaceId red = UISystem::CreateSurface({16, 16});
	UISurfaceId blue = UISystem::CreateSurface({24, 8});
	ASSERT_NE(red, INVALID_UI_SURFACE);
	ASSERT_NE(blue, INVALID_UI_SURFACE);
	FillWith(*UISystem::GetSurfaceContext(red), UISystem::GetSurfaceContext(red)->GetRoot(), Color::Red());
	FillWith(*UISystem::GetSurfaceContext(blue), UISystem::GetSurfaceContext(blue)->GetRoot(), Color::Blue());

	UISystem::Update();
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(red, pixels, size));
	ASSERT_EQ(size, (Vec2I{16, 16}));
	EXPECT_EQ(TexelAt(pixels, size, 8, 8), (RGBA{255, 0, 0, 255}));
	ASSERT_TRUE(ReadbackSurface(blue, pixels, size));
	ASSERT_EQ(size, (Vec2I{24, 8}));
	EXPECT_EQ(TexelAt(pixels, size, 12, 4), (RGBA{0, 0, 255, 255}));
	EXPECT_EQ(UISystem::GetStats().contexts, 2u);

	UISystem::DestroySurface(red);
	UISystem::DestroySurface(blue);
}

TEST_F(UISurfaceFixture, WindowsUIDoesNotPaintIntoSurface) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Window> window = MakeHiddenWindow();
	ASSERT_TRUE(window.IsValid());
	UI::Context* windowContext = UISystem::CreateContext(window);
	ASSERT_NE(windowContext, nullptr);
	FillWith(*windowContext, windowContext->GetRoot(), Color::Green());

	UISurfaceId surface = UISystem::CreateSurface({16, 16});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	FillWith(*UISystem::GetSurfaceContext(surface), UISystem::GetSurfaceContext(surface)->GetRoot(), Color::Red());

	WindowSystem::PumpEvents();
	UISystem::Update();
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	EXPECT_EQ(TexelAt(pixels, size, 8, 8), (RGBA{255, 0, 0, 255}));

	UISystem::DestroySurface(surface);
	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

TEST_F(UISurfaceFixture, ASurfaceRastersAtItsStatedScale) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	UISurfaceId surface = UISystem::CreateSurface({32, 32}, 2.0f);
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);
	UI::Style rootStyle;
	rootStyle.alignItems = UI::Align::Start;
	context->SetStyle(context->GetRoot(), rootStyle);
	UI::ElementHandle box = context->CreateElement();
	context->AppendChild(context->GetRoot(), box);
	UI::Style style;
	style.width = UI::Length::Pixel(10.0f);
	style.height = UI::Length::Pixel(10.0f);
	style.background = UI::Brush::SolidColor(Color::Red());
	context->SetStyle(box, style);

	UISystem::Update();
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	EXPECT_EQ(TexelAt(pixels, size, 19, 19), (RGBA{255, 0, 0, 255}));
	EXPECT_EQ(TexelAt(pixels, size, 21, 21), (RGBA{0, 0, 0, 0}));

	UISystem::DestroySurface(surface);
}

TEST_F(UISurfaceFixture, SurfaceCompositesLayerScopeThroughRealRenderer) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	UISurfaceId surface = UISystem::CreateSurface({16, 16});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);

	UI::ElementHandle group = context->CreateElement();
	context->AppendChild(context->GetRoot(), group);
	UI::Style groupStyle;
	groupStyle.width = UI::Length::Percent(1.0f);
	groupStyle.height = UI::Length::Percent(1.0f);
	groupStyle.opacity = 0.5f;
	context->SetStyle(group, groupStyle);
	FillWith(*context, group, Color::White());

	UISystem::Update();
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	RGBA middle = TexelAt(pixels, size, 8, 8);
	EXPECT_NEAR(middle.r, 128, 4);
	EXPECT_NEAR(middle.a, 128, 4);

	UISystem::DestroySurface(surface);
}

TEST_F(UISurfaceFixture, SurfaceSampledElsewhere) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	UISurfaceId source = UISystem::CreateSurface({16, 16});
	UISurfaceId viewer = UISystem::CreateSurface({16, 16});
	ASSERT_NE(source, INVALID_UI_SURFACE);
	ASSERT_NE(viewer, INVALID_UI_SURFACE);
	FillWith(*UISystem::GetSurfaceContext(source), UISystem::GetSurfaceContext(source)->GetRoot(), Color::Red());

	UI::Context* viewerContext = UISystem::GetSurfaceContext(viewer);
	UI::ElementHandle host = viewerContext->CreateElement();
	viewerContext->AppendChild(viewerContext->GetRoot(), host);
	UI::Style hostStyle;
	hostStyle.width = UI::Length::Percent(1.0f);
	hostStyle.height = UI::Length::Percent(1.0f);
	viewerContext->SetStyle(host, hostStyle);
	viewerContext->Events(host).customDraw =
		[source](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
			RHI::TextureHandle texture = UISystem::GetSurfaceTexture(source);
			if (!texture.IsValid()) {
				return;
			}
			paint.EmitTexturedQuad(UI::SnapRectToPixels(c.GetLayout(element).content), texture,
								   {0.0f, 0.0f, 1.0f, 1.0f}, Color::White(), UI::SamplerKind::NearestClamp);
		};

	for (int frame = 0; frame < 2; ++frame) {
		UISystem::Update();
		RenderSystem::RenderAll();
	}

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(viewer, pixels, size));
	EXPECT_EQ(TexelAt(pixels, size, 8, 8), (RGBA{255, 0, 0, 255}));

	UISystem::DestroySurface(viewer);
	UISystem::DestroySurface(source);
}

TEST_F(UISurfaceFixture, ResizedSurfaceRendersAtNewSize) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	UISurfaceId surface = UISystem::CreateSurface({16, 16});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);
	FillWith(*context, context->GetRoot(), Color::Red());
	UISystem::Update();
	RenderSystem::RenderAll();

	UISystem::ResizeSurface(surface, {40, 24}, 1.0f);
	UISystem::Update();
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	ASSERT_EQ(size, (Vec2I{40, 24}));
	EXPECT_EQ(TexelAt(pixels, size, 39, 23), (RGBA{255, 0, 0, 255}));

	UISystem::DestroySurface(surface);
}

namespace {

	void RenderClipFixture(bool clipped, bool ring, f32 radius, Vector<u8>& pixels, Vec2I& size) {
		UISurfaceId surface = UISystem::CreateSurface({48, 48});
		ASSERT_NE(surface, INVALID_UI_SURFACE);
		UI::Context* context = UISystem::GetSurfaceContext(surface);
		ASSERT_NE(context, nullptr);

		UI::ElementHandle parent = context->GetRoot();
		if (clipped) {
			parent = context->CreateElement();
			context->AppendChild(context->GetRoot(), parent);
			UI::Style clipStyle;
			clipStyle.position = UI::Position::Absolute;
			clipStyle.left = UI::Length::Pixel(8.0f);
			clipStyle.top = UI::Length::Pixel(8.0f);
			clipStyle.width = UI::Length::Pixel(32.0f);
			clipStyle.height = UI::Length::Pixel(32.0f);
			clipStyle.borderRadius = UI::Radii{radius, radius, radius, radius};
			clipStyle.overflow = UI::Overflow::Hidden;
			context->SetStyle(parent, clipStyle);
		}

		UI::ElementHandle box = context->CreateElement();
		context->AppendChild(parent, box);
		UI::Style style;
		style.position = UI::Position::Absolute;
		style.left = UI::Length::Pixel(clipped ? 0.0f : 8.0f);
		style.top = UI::Length::Pixel(clipped ? 0.0f : 8.0f);
		style.width = UI::Length::Pixel(32.0f);
		style.height = UI::Length::Pixel(32.0f);
		style.borderRadius = UI::Radii{radius, radius, radius, radius};
		if (ring) {
			UI::BoxShadow band;
			band.y = 1.0f;
			band.inset = true;
			band.color = Color::FromHex("#0078d7");
			style.AddBoxShadow(band);
		} else {
			style.background = UI::Brush::SolidColor(Color::FromHex("#0078d7"));
		}
		context->SetStyle(box, style);

		UISystem::Update();
		RenderSystem::RenderAll();
		ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
		UISystem::DestroySurface(surface);
	}

} // namespace

TEST_F(UISurfaceFixture, IdenticalRoundedClipCostsShapeNothing) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	for (f32 radius : {4.0f, 8.0f}) {
		for (bool ring : {false, true}) {
			SCOPED_TRACE(std::string(ring ? "1 px inset band" : "solid fill") + " at radius " + std::to_string(static_cast<int>(radius)));
			Vector<u8> bare{GetMemoryResource(MemTag::Debug)};
			Vector<u8> clipped{GetMemoryResource(MemTag::Debug)};
			Vec2I size{};
			RenderClipFixture(false, ring, radius, bare, size);
			RenderClipFixture(true, ring, radius, clipped, size);

			f64 bareInk = 0.0;
			f64 clippedInk = 0.0;
			i32 worst = 0;
			for (i32 y = 0; y < size.y; ++y) {
				for (i32 x = 0; x < size.x; ++x) {
					const i32 a = TexelAt(bare, size, x, y).b;
					const i32 b = TexelAt(clipped, size, x, y).b;
					bareInk += a;
					clippedInk += b;
					worst = std::max(worst, std::abs(a - b));
				}
			}
			ASSERT_GT(bareInk, 0.0);
			const f64 drift = 100.0 * std::abs(bareInk - clippedInk) / bareInk;

			if (!ring) {
				EXPECT_EQ(worst, 0);
				EXPECT_EQ(bareInk, clippedInk);
			} else {
				EXPECT_LT(drift, 3.5) << "ink drifted " << drift;
			}
		}
	}
}

namespace {

	f64 StripeCoverage(const Vector<u8>& pixels, Vec2I size, i32 x, f64 backdropBlue) {
		const RGBA texel = TexelAt(pixels, size, x, 0);
		return std::clamp((static_cast<f64>(texel.b) - backdropBlue) / (0xd7 - backdropBlue), 0.0, 1.0);
	}

	f64 StripeRamp(const Vector<u8>& pixels, Vec2I size, i32 edge, i32 step, f64 backdropBlue) {
		f64 ramp = 0.0;
		for (i32 i = 0; i < 16; ++i) {
			const f64 coverage = StripeCoverage(pixels, size, edge + i * step, backdropBlue);
			if (coverage > 0.999) {
				break;
			}
			ramp += 1.0 - coverage;
		}
		return ramp;
	}

} // namespace

TEST_F(UISurfaceFixture, TabStripeFadesEvenly) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	for (f32 scale : {1.0f, 1.5f}) {
		SCOPED_TRACE("content scale " + std::to_string(scale));
		UISurfaceId surface = UISystem::CreateSurface({320, 64}, scale);
		ASSERT_NE(surface, INVALID_UI_SURFACE);
		UI::Context* context = UISystem::GetSurfaceContext(surface);
		ASSERT_NE(context, nullptr);

		UI::ElementHandle back = context->CreateElement();
		context->AppendChild(context->GetRoot(), back);
		UI::Style backStyle;
		backStyle.position = UI::Position::Absolute;
		backStyle.left = UI::Length::Pixel(0.0f);
		backStyle.top = UI::Length::Pixel(0.0f);
		backStyle.width = UI::Length::Percent(1.0f);
		backStyle.height = UI::Length::Percent(1.0f);
		backStyle.background = UI::Brush::SolidColor(Color::FromHex("#141516"));
		context->SetStyle(back, backStyle);

		UI::ElementHandle host = context->CreateElement();
		context->AppendChild(context->GetRoot(), host);
		UI::Style hostStyle;
		hostStyle.width = UI::Length::Percent(1.0f);
		hostStyle.height = UI::Length::Percent(1.0f);
		context->SetStyle(host, hostStyle);

		UI::DockManager dock(*context, UISystem::GetDefaultFont());
		dock.RegisterPanel("panel.a"_hash, "Alpha", [](UI::Context&, UI::ElementHandle) {});
		dock.RegisterPanel("panel.b"_hash, "Beta", [](UI::Context&, UI::ElementHandle) {});
		dock.DockPanel("panel.a"_hash, dock.GetRootNode(), UI::DockZone::Center);
		dock.DockPanel("panel.b"_hash, dock.GetRootNode(), UI::DockZone::Center);
		dock.ActivatePanel("panel.a"_hash);
		dock.SetFocusedPanel("panel.a"_hash);
		for (int frame = 0; frame < 3; ++frame) {
			dock.Build(host);
			UISystem::Update();
			RenderSystem::RenderAll();
		}

		Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
		Vec2I size{};
		ASSERT_TRUE(ReadbackSurface(surface, pixels, size));

		const i32 bodyRow = static_cast<i32>(5 * scale);
		auto isFill = [&](i32 x) {
			const RGBA texel = TexelAt(pixels, size, x, bodyRow);
			return texel.r == 0x2b && texel.g == 0x2c && texel.b == 0x2f;
		};
		i32 firstFill = 0;
		while (firstFill < size.x && !isFill(firstFill)) {
			++firstFill;
		}
		i32 lastFill = firstFill;
		while (lastFill + 1 < size.x && isFill(lastFill + 1)) {
			++lastFill;
		}
		const i32 boxLeft = firstFill - 1;
		const i32 boxRight = lastFill + 2;
		ASSERT_GT(boxRight - boxLeft, 8);

		const f64 outer = StripeRamp(pixels, size, boxLeft, +1, 0x1d);
		const f64 inner = StripeRamp(pixels, size, boxRight - 1, -1, 0x1d);
		ASSERT_GT(inner, 0.0);
		EXPECT_LT(std::abs(outer - inner), 0.35);

		UISystem::DestroySurface(surface);
	}
}

TEST_F(UISurfaceFixture, SrgbTexturePaintsBytesFileHolds) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RHI::Device* device = RenderSystem::GetDevice();
	ASSERT_NE(device, nullptr);

	const u8 file[16] = {
		128,
		128,
		128,
		255,
		138,
		21,
		21,
		255,
		220,
		40,
		40,
		255,
		10,
		200,
		90,
		255,
	};
	auto upload = [&](RHI::Format format) {
		RHI::TextureDesc desc;
		desc.size = {2, 2};
		desc.format = format;
		desc.usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;
		desc.debugName = "UISurfaceTestImage";
		RHI::TextureHandle texture = device->CreateTextureResource(desc);
		if (texture.IsValid()) {
			device->UploadTexture(texture, file);
		}
		return texture;
	};
	RHI::TextureHandle srgb = upload(RHI::Format::RGBA8SRGB);
	RHI::TextureHandle unorm = upload(RHI::Format::RGBA8Unorm);
	ASSERT_TRUE(srgb.IsValid());
	ASSERT_TRUE(unorm.IsValid());

	UISurfaceId surface = UISystem::CreateSurface({32, 16});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);
	UI::Style rootStyle;
	rootStyle.alignItems = UI::Align::Start;
	context->SetStyle(context->GetRoot(), rootStyle);
	for (RHI::TextureHandle texture : {srgb, unorm}) {
		UI::ElementHandle image = context->CreateElement();
		context->AppendChild(context->GetRoot(), image);
		UI::Style style;
		style.width = UI::Length::Pixel(16.0f);
		style.height = UI::Length::Pixel(16.0f);
		style.background = UI::Brush::ImageBrush(texture, UI::SamplerKind::NearestClamp);
		context->SetStyle(image, style);
	}

	UISystem::Update();
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	ASSERT_EQ(size, (Vec2I{32, 16}));

	const Vec2I quadrants[4] = {{4, 4}, {12, 4}, {4, 12}, {12, 12}};
	for (u32 texel = 0; texel < 4; ++texel) {
		const u8* expected = &file[texel * 4];
		const RGBA fromUnorm = TexelAt(pixels, size, 16 + quadrants[texel].x, quadrants[texel].y);
		const RGBA fromSrgb = TexelAt(pixels, size, quadrants[texel].x, quadrants[texel].y);
		EXPECT_EQ(fromUnorm, (RGBA{expected[0], expected[1], expected[2], 255})) << "texel " << texel << " as UNORM";
		EXPECT_NEAR(fromSrgb.r, expected[0], 1) << "texel " << texel;
		EXPECT_NEAR(fromSrgb.g, expected[1], 1) << "texel " << texel;
		EXPECT_NEAR(fromSrgb.b, expected[2], 1) << "texel " << texel;
		EXPECT_EQ(fromSrgb.a, 255) << "texel " << texel;
	}

	UISystem::DestroySurface(surface);
	device->DestroyResource(srgb);
	device->DestroyResource(unorm);
}

namespace {

	UI::ElementHandle PlacedBox(UI::Context& context, UI::ElementHandle parent, f32 x, f32 y, f32 width, f32 height) {
		UI::ElementHandle element = context.CreateElement();
		context.AppendChild(parent, element);
		UI::Style style;
		style.position = UI::Position::Absolute;
		style.left = UI::Length::Pixel(x);
		style.top = UI::Length::Pixel(y);
		style.width = UI::Length::Pixel(width);
		style.height = UI::Length::Pixel(height);
		context.SetStyle(element, style);
		return element;
	}

	void SetBackground(UI::Context& context, UI::ElementHandle element, const UI::Brush& brush) {
		UI::Style style = context.GetStyle(element);
		style.background = brush;
		context.SetStyle(element, style);
	}

	void SetDropShadow(UI::Context& context, UI::ElementHandle element, const UI::DropShadow& drop) {
		UI::Style style = context.GetStyle(element);
		style.dropShadow = drop;
		context.SetStyle(element, style);
	}

	RHI::TextureHandle UploadHalfClearPicture(RHI::Device& device) {
		Vector<u8> texels{GetMemoryResource(MemTag::Debug)};
		texels.resize(16 * 16 * 4, 0);
		for (i32 y = 0; y < 16; ++y) {
			for (i32 x = 0; x < 8; ++x) {
				u8* texel = &texels[(static_cast<size_t>(y) * 16 + x) * 4];
				texel[0] = 255;
				texel[3] = 255;
			}
		}
		RHI::TextureDesc desc;
		desc.size = {16, 16};
		desc.format = RHI::Format::RGBA8Unorm;
		desc.usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;
		desc.debugName = "UISurfaceTestHalfClear";
		RHI::TextureHandle texture = device.CreateTextureResource(desc);
		if (texture.IsValid()) {
			device.UploadTexture(texture, {texels.data(), texels.size()});
		}
		return texture;
	}

	constexpr RGBA WHITE{255, 255, 255, 255};
	constexpr RGBA BLACK{0, 0, 0, 255};

} // anonymous namespace

TEST_F(UISurfaceFixture, DropShadowMatchesCaster) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	RHI::Device* device = RenderSystem::GetDevice();
	ASSERT_NE(device, nullptr);
	RHI::TextureHandle picture = UploadHalfClearPicture(*device);
	ASSERT_TRUE(picture.IsValid());

	UISurfaceId surface = UISystem::CreateSurface({64, 48});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);
	SetBackground(*context, context->GetRoot(), UI::Brush::SolidColor(Color::White()));

	const UI::DropShadow drop{0.0f, 12.0f, 0.0f, Color::Black()};
	UI::ElementHandle sprite = PlacedBox(*context, context->GetRoot(), 8.0f, 8.0f, 16.0f, 16.0f);
	SetBackground(*context, sprite, UI::Brush::ImageBrush(picture, UI::SamplerKind::NearestClamp));
	SetDropShadow(*context, sprite, drop);
	UI::ElementHandle tile = PlacedBox(*context, context->GetRoot(), 40.0f, 8.0f, 16.0f, 16.0f);
	SetBackground(*context, tile, UI::Brush::SolidColor(Color(0.0f, 0.0f, 1.0f, 1.0f)));
	SetDropShadow(*context, tile, drop);

	UISystem::Update();
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	EXPECT_EQ(TexelAt(pixels, size, 12, 30), BLACK);
	EXPECT_EQ(TexelAt(pixels, size, 20, 30), WHITE);
	EXPECT_EQ(TexelAt(pixels, size, 12, 37), WHITE);
	EXPECT_EQ(TexelAt(pixels, size, 44, 30), BLACK);
	EXPECT_EQ(TexelAt(pixels, size, 52, 30), BLACK);
	EXPECT_EQ(TexelAt(pixels, size, 32, 30), WHITE);
	EXPECT_EQ(TexelAt(pixels, size, 12, 22), (RGBA{255, 0, 0, 255}));

	UISystem::DestroySurface(surface);
	device->DestroyResource(picture);
}

TEST_F(UISurfaceFixture, DropShadowMatchesBoxShadow) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	UISurfaceId surface = UISystem::CreateSurface({128, 48});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);

	constexpr f32 BLUR = 8.0f;
	UI::ElementHandle plated = PlacedBox(*context, context->GetRoot(), 16.0f, 8.0f, 24.0f, 32.0f);
	UI::Style platedStyle = context->GetStyle(plated);
	platedStyle.background = UI::Brush::SolidColor(Color::White());
	platedStyle.AddBoxShadow(UI::BoxShadow{0.0f, 0.0f, BLUR, 0.0f, Color::Black(), false});
	context->SetStyle(plated, platedStyle);
	UI::ElementHandle dropped = PlacedBox(*context, context->GetRoot(), 72.0f, 8.0f, 24.0f, 32.0f);
	SetBackground(*context, dropped, UI::Brush::SolidColor(Color::White()));
	SetDropShadow(*context, dropped, UI::DropShadow{0.0f, 0.0f, BLUR, Color::Black()});

	UISystem::Update();
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	u32 inked = 0;
	for (i32 step = 0; step < 8; ++step) {
		const u8 plate = TexelAt(pixels, size, 40 + step, 24).a;
		const u8 drop = TexelAt(pixels, size, 96 + step, 24).a;
		EXPECT_NEAR(drop, plate, 3) << step;
		inked += plate > 0 ? 1u : 0u;
	}
	EXPECT_GE(inked, 3u);
	EXPECT_NEAR(TexelAt(pixels, size, 96, 24).a, 106, 3);
	EXPECT_EQ(TexelAt(pixels, size, 96 + 8, 24).a, 0);

	UISystem::DestroySurface(surface);
}

TEST_F(UISurfaceFixture, MaskStaysInSlot) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	UISurfaceId surface = UISystem::CreateSurface({64, 48});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);
	SetBackground(*context, context->GetRoot(), UI::Brush::SolidColor(Color::White()));

	const UI::DropShadow drop{0.0f, 12.0f, 0.0f, Color::Black()};
	UI::ElementHandle neighbour = PlacedBox(*context, context->GetRoot(), 36.0f, 8.0f, 16.0f, 16.0f);
	SetBackground(*context, neighbour, UI::Brush::SolidColor(Color::White()));
	SetDropShadow(*context, neighbour, drop);
	UI::ElementHandle overhanging = PlacedBox(*context, context->GetRoot(), 0.0f, 8.0f, 8.0f, 8.0f);
	SetBackground(*context, overhanging, UI::Brush::SolidColor(Color::White()));
	SetDropShadow(*context, overhanging, drop);
	UI::ElementHandle overhang = PlacedBox(*context, overhanging, -60.0f, 0.0f, 60.0f, 8.0f);
	SetBackground(*context, overhang, UI::Brush::SolidColor(Color::White()));

	UISystem::Update();
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	ASSERT_EQ(TexelAt(pixels, size, 44, 24), BLACK);
	EXPECT_EQ(TexelAt(pixels, size, 35, 24), WHITE);
	EXPECT_EQ(TexelAt(pixels, size, 52, 24), WHITE);

	UISystem::DestroySurface(surface);
}

TEST_F(UISurfaceFixture, HalfOpaqueCastsHalf) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	UISurfaceId surface = UISystem::CreateSurface({64, 48});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);
	SetBackground(*context, context->GetRoot(), UI::Brush::SolidColor(Color::White()));

	const UI::DropShadow drop{0.0f, 20.0f, 0.0f, Color::Black()};
	auto halfOpaque = [&](f32 x) {
		UI::ElementHandle element = PlacedBox(*context, context->GetRoot(), x, 4.0f, 16.0f, 16.0f);
		UI::Style style = context->GetStyle(element);
		style.background = UI::Brush::SolidColor(Color::White());
		style.dropShadow = drop;
		style.opacity = 0.5f;
		context->SetStyle(element, style);
		return element;
	};
	halfOpaque(8.0f);
	UI::ElementHandle group = halfOpaque(40.0f);
	PlacedBox(*context, group, 0.0f, 0.0f, 16.0f, 16.0f);

	UISystem::Update();
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	EXPECT_NEAR(TexelAt(pixels, size, 16, 36).r, 128, 3);
	EXPECT_NEAR(TexelAt(pixels, size, 48, 36).r, 128, 3);

	UISystem::DestroySurface(surface);
}

TEST_F(UISurfaceFixture, CasterThatDiesTakesShadowWithIt) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	UISurfaceId surface = UISystem::CreateSurface({32, 32});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);
	SetBackground(*context, context->GetRoot(), UI::Brush::SolidColor(Color::White()));
	UI::ElementHandle caster = PlacedBox(*context, context->GetRoot(), 4.0f, 4.0f, 8.0f, 8.0f);
	SetBackground(*context, caster, UI::Brush::SolidColor(Color::White()));
	SetDropShadow(*context, caster, UI::DropShadow{0.0f, 12.0f, 0.0f, Color::Black()});

	UISystem::Update();
	RenderSystem::RenderAll();
	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	ASSERT_EQ(TexelAt(pixels, size, 8, 20), BLACK);

	context->DestroyElement(caster);
	UISystem::Update();
	RenderSystem::RenderAll();
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	EXPECT_EQ(TexelAt(pixels, size, 8, 20), WHITE);

	UISystem::DestroySurface(surface);
}

namespace {

	std::vector<u8> TexelsOf(const Vector<u8>& pixels, Vec2I size, const UI::Rect& box) {
		std::vector<u8> out;
		const i32 x0 = std::max(0, static_cast<i32>(box.x));
		const i32 y0 = std::max(0, static_cast<i32>(box.y));
		const i32 x1 = std::min(size.x, static_cast<i32>(box.x + box.w));
		const i32 y1 = std::min(size.y, static_cast<i32>(box.y + box.h));
		for (i32 y = y0; y < y1; ++y) {
			const size_t row = (static_cast<size_t>(y) * size.x) * 4;
			out.insert(out.end(), pixels.begin() + static_cast<std::ptrdiff_t>(row + static_cast<size_t>(x0) * 4),
					   pixels.begin() + static_cast<std::ptrdiff_t>(row + static_cast<size_t>(x1) * 4));
		}
		return out;
	}

	size_t InkGlyphs(std::string_view text) {
		return static_cast<size_t>(std::count_if(text.begin(), text.end(), [](char c) { return c != ' '; }));
	}

} // anonymous namespace

TEST_F(UISurfaceFixture, FullGlyphPageReclaimed) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	const Handle<FontAsset> font = UISystem::GetDefaultFont();
	if (!font.IsValid()) {
		GTEST_SKIP() << "engine font not mounted";
	}
	UISurfaceId surface = UISystem::CreateSurface({320, 160});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);
	UI::IFontSystem* fonts = UISystem::GetFontSystem();

	constexpr std::string_view STEADY = "Steady 0123";
	constexpr std::string_view SWEEP = "Sphinx of black quartz judge my vow 456789";
	UI::ElementHandle steady = PlacedBox(*context, context->GetRoot(), 4.0f, 4.0f, 300.0f, 24.0f);
	context->SetText(steady, font, 16.0f, Color::White(), STEADY);
	UI::ElementHandle sweep = PlacedBox(*context, context->GetRoot(), 4.0f, 40.0f, 4000.0f, 400.0f);
	context->SetText(sweep, font, 12.0f, Color::White(), SWEEP);

	UISystem::Update();
	RenderSystem::RenderAll();
	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
	const UI::Rect steadyBox = context->GetLayout(steady).borderBox;
	const std::vector<u8> drawn = TexelsOf(pixels, size, steadyBox);
	ASSERT_TRUE(std::any_of(drawn.begin(), drawn.end(), [](u8 v) { return v != 0; }));

	auto glyphsOf = [&](UI::ElementHandle element) {
		const UI::Context::ShapedTextView view = context->GetShapedText(element);
		return view.run ? view.run->glyphs.size() : size_t{0};
	};
	u32 reclaims = 0;
	u32 shortFrames = 0;
	u32 fullFrames = 0;
	u32 steadyChanged = 0;
	for (i32 px = 13; px <= 90; ++px) {
		context->SetText(sweep, font, static_cast<f32>(px), Color::White(), SWEEP);
		const u32 before = fonts->GetAtlasGeneration();
		UISystem::Update();
		reclaims += fonts->GetAtlasGeneration() != before ? 1u : 0u;
		RenderSystem::RenderAll();
		if (glyphsOf(sweep) != InkGlyphs(SWEEP)) {
			++shortFrames;
		}
		UI::AtlasPage page{};
		if (fonts->GetAtlasPage(page) && page.full) {
			++fullFrames;
			const u32 held = fonts->GetAtlasGeneration();
			UI::RenderList list;
			context->BuildRenderList(list);
			EXPECT_EQ(fonts->GetAtlasGeneration(), held);
		}
		ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
		if (TexelsOf(pixels, size, steadyBox) != drawn) {
			++steadyChanged;
		}
	}
	EXPECT_LE(shortFrames, reclaims + 1);
	EXPECT_EQ(steadyChanged, 0u);
	EXPECT_GE(fullFrames, 2u);
	EXPECT_GE(reclaims, 2u);

	UISystem::Update();
	RenderSystem::RenderAll();
	EXPECT_EQ(glyphsOf(sweep), InkGlyphs(SWEEP));
	EXPECT_EQ(glyphsOf(steady), InkGlyphs(STEADY));

	UISystem::DestroySurface(surface);
}

TEST_F(UISurfaceFixture, OversizedGlyphSkipped) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	const Handle<FontAsset> font = UISystem::GetDefaultFont();
	if (!font.IsValid()) {
		GTEST_SKIP() << "engine font not mounted";
	}
	UISurfaceId surface = UISystem::CreateSurface({64, 64});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);

	UI::ElementHandle giant = PlacedBox(*context, context->GetRoot(), 0.0f, 0.0f, 4000.0f, 4000.0f);
	context->SetText(giant, font, 1600.0f, Color::White(), "W");
	UI::ElementHandle after = PlacedBox(*context, context->GetRoot(), 0.0f, 0.0f, 64.0f, 24.0f);
	context->SetText(after, font, 16.0f, Color::White(), "after");
	UISystem::Update();
	RenderSystem::RenderAll();

	const UI::Context::ShapedTextView giantView = context->GetShapedText(giant);
	ASSERT_NE(giantView.run, nullptr);
	ASSERT_TRUE(giantView.run->glyphs.empty());
	const UI::Context::ShapedTextView afterView = context->GetShapedText(after);
	ASSERT_NE(afterView.run, nullptr);
	EXPECT_EQ(afterView.run->glyphs.size(), 5u);
	UI::AtlasPage page{};
	ASSERT_TRUE(UISystem::GetFontSystem()->GetAtlasPage(page));
	EXPECT_FALSE(page.full);

	UISystem::DestroySurface(surface);
}

TEST_F(UISurfaceFixture, NewGlyphsDrawSameFrame) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	const Handle<FontAsset> font = UISystem::GetDefaultFont();
	if (!font.IsValid()) {
		GTEST_SKIP() << "engine font not mounted";
	}
	UISurfaceId surface = UISystem::CreateSurface({320, 96});
	ASSERT_NE(surface, INVALID_UI_SURFACE);
	UI::Context* context = UISystem::GetSurfaceContext(surface);
	ASSERT_NE(context, nullptr);
	UI::IFontSystem* fonts = UISystem::GetFontSystem();

	constexpr std::string_view TEXT = "Batched glyphs 0123456789";
	UI::ElementHandle label = PlacedBox(*context, context->GetRoot(), 4.0f, 4.0f, 312.0f, 88.0f);
	context->SetText(label, font, 11.0f, Color::White(), TEXT);
	UISystem::Update();
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Debug)};
	Vec2I size{};
	for (f32 px : {17.0f, 23.0f, 31.0f}) {
		context->SetText(label, font, px, Color::White(), TEXT);
		UI::AtlasPage before{};
		ASSERT_TRUE(fonts->GetAtlasPage(before));
		{
#if !defined(SHALLTEAR_CONFIG_RELEASE)
			ZoneProbe::ZoneScope zones;
#endif
			UISystem::Update();
			RenderSystem::RenderAll();
#if !defined(SHALLTEAR_CONFIG_RELEASE)
			Profiler::NextFrame();
			EXPECT_EQ(ZoneProbe::ZoneCalls("RHI.ImmediateSubmit"), 0u) << px;
			EXPECT_EQ(ZoneProbe::ZoneCalls("Render.Offscreen"), 1u);
#endif
		}
		UI::AtlasPage after{};
		ASSERT_TRUE(fonts->GetAtlasPage(after));
		ASSERT_GE(after.entries, before.entries + InkGlyphs(TEXT) / 2) << px;

		ASSERT_TRUE(ReadbackSurface(surface, pixels, size));
		const std::vector<u8> ink = TexelsOf(pixels, size, context->GetLayout(label).borderBox);
		EXPECT_TRUE(std::any_of(ink.begin(), ink.end(), [](u8 v) { return v != 0; }));
	}

	UISystem::DestroySurface(surface);
}
