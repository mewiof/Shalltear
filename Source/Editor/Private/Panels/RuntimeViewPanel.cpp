#include "RuntimeViewPanel.h"

#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Input/Input.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/UI/RenderList.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"
#include "Shalltear/Core/Window/Window.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorInputOps.h"
#include "Shalltear/Editor/EditorViewportOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/RuntimeViewToolbar.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/UIDebugOps.h"

#include "PanelHeader.h"

namespace Shalltear {

	namespace {
		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		Color TextDim() { return EditorTheme::GetColor(ColorToken::TextDim); }
		constexpr std::string_view NoCameraText = "no camera in the loaded scenes";

		u64 _claimFrame = ~0ull;
		const void* _claimant = nullptr;

		bool EventCarriesPointer(InputEventType type) {
			return type == InputEventType::MouseMove || type == InputEventType::MouseDown ||
				   type == InputEventType::MouseUp || type == InputEventType::MouseWheel ||
				   type == InputEventType::MouseEnter;
		}

		Handle<Window> WindowOf(const UI::Context& context) {
			Handle<Window> found;
			UISystem::ForEachContext([&found, &context](Handle<Window> window, UI::Context& candidate) {
				if (!found.IsValid() && &candidate == &context) {
					found = window;
				}
			});
			return found;
		}

		std::string_view FileNameOf(std::string_view path) {
			const size_t slash = path.find_last_of('/');
			return slash == std::string_view::npos ? path : path.substr(slash + 1);
		}
	} // anonymous namespace

	void RuntimeViewPanel::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};

		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Column;
		rootStyle.alignItems = UI::Align::Stretch;
		rootStyle.width = UI::Length::Percent(1.0f);
		rootStyle.height = UI::Length::Percent(1.0f);
		context.SetStyle(root, rootStyle);

		UI::ElementHandle bar = PanelHeader::BuildStrip(context, root);

		_statusLabel = context.CreateElement();
		context.AppendChild(bar, _statusLabel);
		UI::Style statusStyle = context.GetStyle(_statusLabel);
		statusStyle.flexGrow = 1.0f;
		statusStyle.flexShrink = 1.0f;
		statusStyle.width = UI::Length::Pixel(0.0f);
		statusStyle.textOverflow = UI::TextOverflow::Ellipsis;
		context.SetStyle(_statusLabel, statusStyle);
		context.SetText(_statusLabel, font, EditorTheme::GetMetric(MetricToken::FontSmall), TextDim(), NoCameraText);

		_toggleStrip = context.CreateElement();
		context.AppendChild(bar, _toggleStrip);
		UI::Style stripStyle;
		stripStyle.flexDirection = UI::FlexDirection::Row;
		stripStyle.alignItems = UI::Align::Center;
		stripStyle.flexShrink = 0.0f;
		context.SetStyle(_toggleStrip, stripStyle);
		BuildToggles(context);

		PanelHeader::OverflowButton(context, bar, [](Vector<UI::Widgets::MenuItem>& items) {
			Vector<UIDebugOps::Target> targets{GetMemoryResource(MemTag::Editor)};
			UIDebugOps::EnumerateTargets(targets);
			bool any = false;
			UISystem::ForEachScreenSurface([&items, &targets, &any](UISurfaceId id, UI::Context&) {
				for (const UIDebugOps::Target& target : targets) {
					if (target.kind != UIDebugOps::Target::Kind::Surface || target.id != id) {
						continue;
					}
					UI::Widgets::MenuItem entry;
					entry.label = str("UI Debugger: ") + str(FileNameOf(UISystem::GetSurfaceName(id)));
					entry.onSelect = [target]() { UIDebugOps::OpenDebuggerOn(target); };
					items.push_back(std::move(entry));
					any = true;
					break;
				}
			});
			if (!any) {
				UI::Widgets::MenuItem none;
				none.label = "UI Debugger";
				none.enabled = false;
				items.push_back(std::move(none));
			}
		});

		UI::ElementHandle viewport = _viewport.Build(context, root);

		context.Events(viewport).onKeyDown = [](Key, KeyMod) { return true; };

		_screenLayer = context.CreateElement();
		context.AppendChild(viewport, _screenLayer);
		UI::Style layerStyle;
		layerStyle.position = UI::Position::Absolute;
		layerStyle.left = UI::Length::Pixel(0.0f);
		layerStyle.top = UI::Length::Pixel(0.0f);
		layerStyle.right = UI::Length::Pixel(0.0f);
		layerStyle.bottom = UI::Length::Pixel(0.0f);
		layerStyle.pointerEvents = false;
		context.SetStyle(_screenLayer, layerStyle);
		context.Events(_screenLayer).customDraw =
			[](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
				const UI::Rect area = UI::SnapRectToPixels(c.GetLayout(element).content);
				if (area.w <= 1.0f || area.h <= 1.0f) {
					return;
				}
				UISystem::ForEachScreenSurface([&paint, &area](UISurfaceId id, UI::Context&) {
					RHI::TextureHandle texture = UISystem::GetSurfaceTexture(id);
					if (texture.IsValid()) {
						paint.EmitTexturedQuad(area, texture, {0.0f, 0.0f, 1.0f, 1.0f}, Color::White(),
											   UI::SamplerKind::LinearClamp);
					}
				});
			};

		_labelSaysCamera = false;
		SyncCameraLabel(context);
	}

	void RuntimeViewPanel::OnClose() {
		_viewport.Release();
		_focusPending = false;
	}

	bool RuntimeViewPanel::HasInputFocus() const {
		UI::Context* context = GetPanelContext();
		return context && context->GetFocused() == _viewport.GetElement();
	}

	void RuntimeViewPanel::SyncCameraLabel(UI::Context& context) {
		if (!context.IsAlive(_statusLabel)) {
			return;
		}
		const bool haveCamera = EditorViewportOps::FindActiveSceneCamera().Get() != nullptr;
		if (haveCamera == _labelSaysCamera) {
			return;
		}
		_labelSaysCamera = haveCamera;
		context.SetTextString(_statusLabel, haveCamera ? std::string_view{} : NoCameraText);
	}

	void RuntimeViewPanel::HonorFocusRequest(UI::Context& context) {
		if (!_focusPending || !context.IsAlive(_viewport.GetElement())) {
			return;
		}
		_focusPending = false;
		context.SetFocus(_viewport.GetElement());
	}

	void RuntimeViewPanel::OnTick(f32) {
		UI::Context* context = GetPanelContext();
		if (!context) {
			return;
		}
		SyncCameraLabel(*context);
		HonorFocusRequest(*context);
		RefreshToggles(*context);
		SyncScreen(*context);
	}

	void RuntimeViewPanel::BuildToggles(UI::Context& context) {
		if (!context.IsAlive(_toggleStrip)) {
			return;
		}
		context.ClearChildren(_toggleStrip);
		_toggleStates.clear();
		_toggleVersion = RuntimeViewToolbar::GetVersion();
		RuntimeViewToolbar::ForEachToggle([&](StringHash id, const RuntimeViewToggle& toggle) {
			const bool on = toggle.isOn();
			_toggleStates.push_back(on ? 1 : 0);
			PanelHeader::ToolButtonDesc desc;
			desc.icon = toggle.icon;
			desc.fallback = toggle.label;
			desc.tooltip = toggle.tooltip;
			desc.checked = on;
			desc.onClick = [id]() {
				if (const RuntimeViewToggle* live = RuntimeViewToolbar::FindToggle(id)) {
					live->set(!live->isOn());
				}
			};
			PanelHeader::ToolButton(context, _toggleStrip, desc);
		});
	}

	void RuntimeViewPanel::RefreshToggles(UI::Context& context) {
		bool stale = RuntimeViewToolbar::GetVersion() != _toggleVersion;
		size_t index = 0;
		RuntimeViewToolbar::ForEachToggle([&](StringHash, const RuntimeViewToggle& toggle) {
			const u8 on = toggle.isOn() ? 1 : 0;
			stale |= index >= _toggleStates.size() || _toggleStates[index] != on;
			++index;
		});
		if (stale) {
			BuildToggles(context);
		}
	}

	void RuntimeViewPanel::SyncScreen(UI::Context& context) {
		const UI::Rect rect = _viewport.GetRect(context);
		if (!EditorViewportOps::ViewportIsOnScreen(rect)) {
			ReleaseScreenPointer();
			return;
		}
		const u64 frame = Time::GetFrameCount();
		if (_claimFrame == frame && _claimant != this) {
			return;
		}
		UIScreenDesc screen;
		screen.target = UIScreenDesc::Target::Texture;
		screen.sizePx = EditorViewportOps::ViewTargetSizeFor(rect);
		screen.dpiScale = context.GetContentScale();
		if (screen.sizePx.x <= 0 || screen.sizePx.y <= 0) {
			return;
		}
		_claimFrame = frame;
		_claimant = this;
		UISystem::SetScreen(screen);

		if (PanelManager::GetPlayModeState() == PlayModeState::Playing) {
			ForwardInputToScreen(context, rect);
		} else {
			ReleaseScreenPointer();
		}
	}

	void RuntimeViewPanel::ReleaseScreenPointer() {
		if (_pressInScreen) {
			InputEvent release{};
			release.type = InputEventType::MouseUp;
			release.button = _pressButton;
			release.mousePosPx = {-(1 << 20), -(1 << 20)};
			UISystem::InjectScreenInput(release);
		}
		if (_pointerInScreen || _pressInScreen) {
			InputEvent leave{};
			leave.type = InputEventType::MouseLeave;
			UISystem::InjectScreenInput(leave);
		}
		_pointerInScreen = false;
		_pressInScreen = false;
	}

	void RuntimeViewPanel::AdvanceGamePointer() {
		_gamePointerAimed = false;
		UI::Context* context = GetPanelContext();
		if (!context) {
			return;
		}
		Handle<Window> window = WindowOf(*context);
		if (!window.IsValid()) {
			return;
		}
		const UI::Rect rect = _viewport.GetRect(*context);
		const bool onScreen = EditorViewportOps::ViewportIsOnScreen(rect);
		const bool holdsKeyboard = context->GetFocused() == _viewport.GetElement();
		Vec2 pointer = context->GetPointerPosition();
		for (const InputEvent& event : Input::GetFrameEvents()) {
			if (!(event.window == window)) {
				continue;
			}
			if (event.type == InputEventType::KeyDown && !holdsKeyboard) {
				continue;
			}
			const Vec2 point{static_cast<f32>(event.mousePosPx.x), static_cast<f32>(event.mousePosPx.y)};
			if (EventCarriesPointer(event.type)) {
				pointer = point;
			}
			_gamePointerArmed = EditorInputOps::AdvanceGamePointerArm(
				_gamePointerArmed, event, onScreen && rect.Contains(point));
		}

		const bool onImage = onScreen && context->HitTest(pointer) == _viewport.GetElement();
		_gamePointerAimed = onImage || _pressInScreen || window->GetCursorMode() == CursorMode::Locked;
	}

	void RuntimeViewPanel::ForwardInputToScreen(UI::Context& context, const UI::Rect& rect) {
		Handle<Window> window = WindowOf(context);
		if (!window.IsValid()) {
			return;
		}
		const bool holdsKeyboard = context.GetFocused() == _viewport.GetElement();
		for (const InputEvent& event : Input::GetFrameEvents()) {
			if (!(event.window == window)) {
				continue;
			}
			switch (event.type) {
			case InputEventType::MouseMove:
			case InputEventType::MouseDown:
			case InputEventType::MouseUp:
			case InputEventType::MouseWheel: {
				const Vec2 point{static_cast<f32>(event.mousePosPx.x), static_cast<f32>(event.mousePosPx.y)};
				const bool inside = rect.Contains(point);
				if (!inside && !_pressInScreen) {
					if (_pointerInScreen) {
						InputEvent leave = event;
						leave.type = InputEventType::MouseLeave;
						UISystem::InjectScreenInput(leave);
						_pointerInScreen = false;
					}
					break;
				}
				InputEvent local = event;
				local.mousePosPx = {static_cast<i32>(point.x - rect.x), static_cast<i32>(point.y - rect.y)};
				UISystem::InjectScreenInput(local);
				_pointerInScreen = inside;
				if (event.type == InputEventType::MouseDown) {
					_pressInScreen = true;
					_pressButton = event.button;
				} else if (event.type == InputEventType::MouseUp) {
					_pressInScreen = false;
				}
				break;
			}
			case InputEventType::KeyDown:
			case InputEventType::KeyUp:
			case InputEventType::Text:
				if (holdsKeyboard) {
					UISystem::InjectScreenInput(event);
				}
				break;
			case InputEventType::MouseLeave:
				if (_pressInScreen) {
					break;
				}
				if (_pointerInScreen) {
					UISystem::InjectScreenInput(event);
					_pointerInScreen = false;
				}
				break;
			case InputEventType::MouseEnter:
			case InputEventType::GamepadConnected:
			case InputEventType::GamepadDisconnected:
				break;
			}
		}
	}

	void RuntimeViewPanel::OnSubmitView(f32 deltaSeconds) {
		UI::Context* context = GetPanelContext();
		if (!context) {
			return;
		}
		_viewport.Tick(*context, deltaSeconds);
		if (!_viewport.IsReady()) {
			return;
		}

		Camera* camera = EditorViewportOps::FindActiveSceneCamera().Get();
		SceneObject* owner = camera ? camera->GetOwner().Get() : nullptr;
		if (!owner) {
			return;
		}

		ViewDesc desc;
		desc.view = owner->GetWorldMatrix().Inverse();
		desc.projection = camera->BuildProjection(_viewport.GetAspect());
		desc.cameraPosition = owner->GetWorldPosition();
		desc.clearColor = camera->GetClearColor();
		EditorViewportOps::CollectCameraPassRows(Handle<Camera>(camera), _passRows);
		desc.passes = {_passRows.data(), _passRows.size()};
		desc.camera = Handle<Camera>(camera);
		RenderSystem::SubmitView(_viewport.GetTarget(), desc);
	}

	EditorIcons::Icon RuntimeViewPanel::GetTabIcon() const {
		return EditorIcons::Glyph::PanelGame;
	}

} // namespace Shalltear
