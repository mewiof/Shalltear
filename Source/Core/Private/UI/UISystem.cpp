#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/RHICommandList.h"
#include "Shalltear/Core/Graphics/RHIDevice.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Input/Input.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Profiler.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/UIPanelRenderer.h"
#include "Shalltear/Core/UI/UIPanelSettings.h"
#include "Shalltear/Core/Window/WindowEvents.h"
#include "Shalltear/Core/Window/WindowSystem.h"
#include "Asset/EngineBuiltins.h"
#include "Module/EngineStateTable.h"
#include "UI/FreeTypeFontSystem.h"
#include "UI/VectorIconSystem.h"
#include "UI/RHIUIRenderer.h"
#include "UI/UIInternal.h"
#include "StaticLinkAnchors.h"
#include "Window/WindowInternal.h"
#include "ScreenInternal.h"
#include "UI/UIScreenInternal.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		using namespace Shalltear::UI;

		const u8 _FRAME_STAGE_OWNER = 0;

		struct WindowUI {
			Handle<Window> window;
			UI::Context* context = nullptr;
			UI::DockManager* dockManager = nullptr;
			bool floating = false;
			UI::ElementHandle dockHost{};
			bool owesCursorRestore = false;
			Vec2I renderSizeSeen{};
		};

		struct UISurface {
			UISurfaceId id = INVALID_UI_SURFACE;
			UI::Context* context = nullptr;
			RHI::TextureHandle texture;
			Vec2I sizePx{};
			f32 contentScale = 1.0f;

			bool screen = false;
			UIScreenSurfaceDesc screenDesc;
			bool pointerInside = false;
		};

		struct PanelEntry {
			StringHash id;
			str title;
			UI::DockBuildFn build;
			Handle<VectorAsset> iconImage;
			f32 iconSizePx = 0.0f;
		};

		struct DeferredDetach {
			StringHash panelId;
			Vec2I screenPos{};
		};
		struct DeferredAttach {
			Handle<Window> dragged;
			Handle<Window> target;
			UI::DockDropTarget drop;
		};

		class UIState : public Object, public IWindowOverlay {
		public:
			Vector<WindowUI> windows{GetMemoryResource(MemTag::UI)};
			Vector<UISurface> surfaces{GetMemoryResource(MemTag::UI)};
			UISurfaceId nextSurfaceId = 1;
			Vector<PanelEntry> panels{GetMemoryResource(MemTag::UI)};

			UIScreenDesc screen;
			Vector<u32> screenOrder{GetMemoryResource(MemTag::UI)};
			UISurfaceId screenCapture = INVALID_UI_SURFACE;
			UISurfaceId screenKeyboard = INVALID_UI_SURFACE;

			bool ScreenIsWindow(Handle<Window> window) const {
				return screen.target == UIScreenDesc::Target::Window && window.IsValid() &&
					   window == WindowSystem::GetPrimaryWindow();
			}

			RHIUIRenderer* renderer = nullptr;
			FreeTypeFontSystem* fontSystem = nullptr;
			VectorIconSystem* iconSystem = nullptr;

			Handle<FontAsset> defaultFont;
			bool defaultFontAttempted = false;

			bool detachEnabled = true;
			bool cursorModeYielded = false;
			UISystem::ContextInitFn contextInitializer;
			UI::DockPanelCloseFn panelCloseCallback;
			UISystem::FloatingDockWindowPolicy floatingPolicy;
			Vector<DeferredDetach> pendingDetaches{GetMemoryResource(MemTag::UI)};
			Vector<DeferredAttach> pendingAttaches{GetMemoryResource(MemTag::UI)};
			Vector<Handle<Window>> pendingCloses{GetMemoryResource(MemTag::UI)};

			bool dragPayloadActive = false;
			UI::DragPayload dragPayload;
			Handle<Window> ghostWindow;
			UI::ElementHandle ghostElement;

			Handle<Window> dragTarget;
			UI::DockDropTarget dragDrop;

			RenderList scratchList;

			u32 frameDrawCalls = 0;
			u32 frameQuads = 0;

			WindowUI* Find(Handle<Window> window) {
				for (WindowUI& entry : windows) {
					if (entry.window == window) {
						return &entry;
					}
				}
				return nullptr;
			}

			void NotifyPanelClosed(StringHash panelId) {
				if (panelCloseCallback) {
					panelCloseCallback(panelId);
					return;
				}
				for (WindowUI& entry : windows) {
					if (entry.dockManager) {
						entry.dockManager->UndockPanel(panelId);
					}
				}
			}

			void CollectOverlayWindows(Vector<Handle<Window>>& out) override {
				for (const WindowUI& entry : windows) {
					out.push_back(entry.window);
				}
				if (screen.target == UIScreenDesc::Target::Window && !screenOrder.empty()) {
					Handle<Window> primary = WindowSystem::GetPrimaryWindow();
					if (primary.IsValid() && !Find(primary)) {
						out.push_back(primary);
					}
				}
			}

			u32 RecordOverlay(Handle<Window> window, RHI::CommandList& cmd,
							  RHI::TextureHandle backbuffer, Vec2I sizePx) override {
				if (!renderer) {
					return 0;
				}
				u32 drawCalls = 0;
				UI::SubmitInfo info;
				info.commandList = &cmd;
				info.target = backbuffer;
				info.targetSizePx = sizePx;
				if (ScreenIsWindow(window)) {
					for (u32 index : screenOrder) {
						UISurface& surface = surfaces[index];
						surface.context->BuildRenderList(scratchList);
						renderer->Submit(scratchList, info);
						drawCalls += renderer->GetLastDrawCalls();
						frameDrawCalls += renderer->GetLastDrawCalls();
						frameQuads += renderer->GetLastQuads();
					}
				}
				WindowUI* entry = Find(window);
				if (!entry || !entry->context) {
					return drawCalls;
				}
				entry->context->BuildRenderList(scratchList);
				renderer->Submit(scratchList, info);
				drawCalls += renderer->GetLastDrawCalls();
				frameDrawCalls += renderer->GetLastDrawCalls();
				frameQuads += renderer->GetLastQuads();
				return drawCalls;
			}

			bool HasOffscreenWork() override {
				for (const UISurface& surface : surfaces) {
					if (surface.texture.IsValid()) {
						return true;
					}
				}
				return false;
			}

			u32 RecordOffscreen(RHI::CommandList& cmd) override {
				if (!renderer) {
					return 0;
				}
				u32 drawCalls = 0;
				for (UISurface& surface : surfaces) {
					if (!surface.texture.IsValid()) {
						continue;
					}
					cmd.TransitionTexture(surface.texture, RHI::ResourceState::RenderTarget);
					RHI::RenderPassDesc::Attachment attachment;
					attachment.texture = surface.texture;
					attachment.load = RHI::LoadOp::Clear;
					attachment.clearColor = Color::Clear();
					RHI::RenderPassDesc clearPass;
					clearPass.colorAttachments = {&attachment, 1};
					cmd.BeginRenderPass(clearPass);
					cmd.EndRenderPass();

					surface.context->BuildRenderList(scratchList);
					UI::SubmitInfo info;
					info.commandList = &cmd;
					info.target = surface.texture;
					info.targetSizePx = surface.sizePx;
					renderer->Submit(scratchList, info);
					drawCalls += renderer->GetLastDrawCalls();
					frameDrawCalls += renderer->GetLastDrawCalls();
					frameQuads += renderer->GetLastQuads();

					cmd.TransitionTexture(surface.texture, RHI::ResourceState::ShaderResource);
				}
				return drawCalls;
			}

			void OnWindowResized(const WindowResizedEvent& event) {
				if (WindowUI* entry = Find(event.window)) {
					const Window* window = event.window.Get();
					if (window && entry->renderSizeSeen.x > 0) {
						SyncWindowContext(*entry, *window);
					} else {
						entry->context->Resize(event.clientSizePx);
					}
				}
			}

			void OnWindowDPIChanged(const WindowDPIChangedEvent& event) {
				if (WindowUI* entry = Find(event.window)) {
					const Window* window = event.window.Get();
					entry->context->SetContentScale(event.scale * (window ? WindowInternal::RenderScaleOf(*window) : 1.0f));
				}
			}

			void SyncWindowContext(WindowUI& entry, const Window& window) {
				entry.renderSizeSeen = WindowInternal::GetRenderSizeOverride(window);
				const Vec2I size = WindowInternal::GetRenderSize(window);
				if (entry.context->GetSize() != size) {
					entry.context->Resize(size);
				}
				entry.context->SetContentScale(window.GetContentScale() * WindowInternal::RenderScaleOf(window));
			}

			void OnWindowDestroying(const WindowDestroyingEvent& event) {
				Vector<StringHash> docked{GetMemoryResource(MemTag::UI)};
				if (WindowUI* entry = Find(event.window); entry && entry->dockManager) {
					entry->dockManager->GetDockedPanels(docked);
				}
				for (StringHash panel : docked) {
					NotifyPanelClosed(panel);
				}

				for (size_t i = 0; i < windows.size(); ++i) {
					if (windows[i].window == event.window) {
						Delete(windows[i].dockManager);
						Delete(windows[i].context);
						windows.erase(windows.begin() + static_cast<i64>(i));
						break;
					}
				}
			}

			void OnDragBegan(const WindowDragBeganEvent& event) {
				(void)event;
				dragTarget = {};
				dragDrop = {};
			}

			void OnDragMoved(const WindowDragMovedEvent& event) {
				WindowUI* dragged = Find(event.window);
				if (!dragged || !dragged->dockManager) {
					return;
				}
				Handle<Window> targetWindow = WindowSystem::GetWindowAtPoint(event.screenCursorPx, event.window);
				WindowUI* target = targetWindow.IsValid() ? Find(targetWindow) : nullptr;

				for (WindowUI& entry : windows) {
					if (entry.dockManager && (!target || !(entry.window == targetWindow))) {
						entry.dockManager->HideExternalOverlay();
					}
				}
				dragTarget = {};
				dragDrop = {};

				if (target && target->dockManager) {
					Window* resolved = targetWindow.Get();
					if (resolved) {
						Vec2I client = resolved->ScreenToClient(event.screenCursorPx);
						const Vec2 point{static_cast<f32>(client.x), static_cast<f32>(client.y)};
						target->dockManager->ShowExternalOverlay(point);
						dragTarget = targetWindow;
						dragDrop = target->dockManager->GetDropTargetAt(point);
					}
				}
			}

			void OnDragEnded(const WindowDragEndedEvent& event) {
				for (WindowUI& entry : windows) {
					if (entry.dockManager) {
						entry.dockManager->HideExternalOverlay();
					}
				}
				WindowUI* dragged = Find(event.window);
				if (dragged && dragged->dockManager && dragTarget.IsValid() && dragDrop.zone != UI::DockZone::None) {
					pendingAttaches.push_back({event.window, dragTarget, dragDrop});
				}
				dragTarget = {};
				dragDrop = {};
			}
		};

		UIState* _state = nullptr;

		UI::DockTabIcon IconOf(const PanelEntry& panel) {
			return {panel.iconImage, panel.iconSizePx};
		}

		void ReplayPanels(UI::DockManager& dockManager) {
			for (const PanelEntry& panel : _state->panels) {
				dockManager.RegisterPanel(panel.id, panel.title, panel.build, IconOf(panel));
			}
		}

		void InstallDetach(WindowUI& entry) {
			entry.dockManager->SetDetachCallback([](StringHash panelId, Vec2I screenPos) {
				if (_state && _state->detachEnabled) {
					_state->pendingDetaches.push_back({panelId, screenPos});
				}
			});
			entry.dockManager->SetPanelCloseCallback([](StringHash panelId) {
				if (_state) {
					_state->NotifyPanelClosed(panelId);
				}
			});
		}

		void DestroyDragGhost(UIState& state) {
			if (state.ghostElement.IsValid()) {
				if (WindowUI* entry = state.Find(state.ghostWindow)) {
					if (entry->context) {
						entry->context->DestroyElement(state.ghostElement);
					}
				}
			}
			state.ghostElement = {};
			state.ghostWindow = {};
		}

		void EndDragPayload(UIState& state) {
			DestroyDragGhost(state);
			for (WindowUI& entry : state.windows) {
				if (entry.context) {
					entry.context->ClearDragTarget();
				}
			}
			state.dragPayloadActive = false;
			state.dragPayload = {};
		}

		void UpdateDragPayloadFlow(UIState& state) {
			Handle<Window> hoveredWindow;
			if (WindowSystem::IsInitialized()) {
				hoveredWindow = WindowSystem::GetWindowAtPoint(Input::GetMousePosition());
			}
			WindowUI* hovered = hoveredWindow.IsValid() ? state.Find(hoveredWindow) : nullptr;
			Vec2 clientMouse{};
			if (hovered) {
				Vec2I client = Input::GetMousePosition(hoveredWindow);
				clientMouse = {static_cast<f32>(client.x), static_cast<f32>(client.y)};
			}

			if (!Input::IsMouseButtonDown(MouseButton::Left)) {
				if (hovered && hovered->context) {
					hovered->context->DeliverDrop(state.dragPayload, clientMouse);
				}
				EndDragPayload(state);
				return;
			}

			for (WindowUI& entry : state.windows) {
				if (!entry.context) {
					continue;
				}
				if (hovered == &entry) {
					entry.context->UpdateDragTarget(state.dragPayload, clientMouse);
				} else {
					entry.context->ClearDragTarget();
				}
			}

			if (!hovered || !hovered->context) {
				DestroyDragGhost(state);
				return;
			}
			UI::Context& ctx = *hovered->context;
			if (!(state.ghostWindow == hoveredWindow) || !ctx.IsAlive(state.ghostElement)) {
				DestroyDragGhost(state);
				state.ghostWindow = hoveredWindow;
				state.ghostElement = ctx.CreateElement();
				ctx.AppendChild(ctx.GetRoot(), state.ghostElement);
				ctx.SetHoverable(state.ghostElement, false);
				ctx.SetText(state.ghostElement, UISystem::GetDefaultFont(), 13.0f,
							ctx.GetStyleVarColor("--slt-payload-ghost-text"_hash, Color(0.95f, 0.95f, 0.95f)),
							state.dragPayload.display);
			}
			f32 scale = std::max(0.001f, ctx.GetContentScale());
			UI::Style ghost;
			ghost.position = UI::Position::Absolute;
			ghost.left = UI::Length::Pixel((clientMouse.x + 14.0f) / scale);
			ghost.top = UI::Length::Pixel((clientMouse.y + 10.0f) / scale);
			ghost.background = UI::Brush::SolidColor(ctx.GetStyleVarColor("--slt-payload-ghost"_hash, Color(0.16f, 0.16f, 0.2f, 0.95f)));
			ghost.padding = {8.0f, 4.0f, 8.0f, 4.0f};
			ghost.borderRadius = UI::Radii::All(ctx.GetStyleVarFloat("--slt-radius"_hash, 4.0f));
			ghost.borderColor = ctx.GetStyleVarColor("--slt-ghost-border"_hash, Color(0.4f, 0.4f, 0.45f, 1.0f));
			ghost.borderWidth = UI::Edges::All(1.0f);
			ghost.zIndex = 20000;
			ghost.pointerEvents = false;
			ctx.SetStyle(state.ghostElement, ghost);
		}

		UI::DockManager* CreateFloatingDockEntry(str title, Vec2I screenPosPx, Vec2I clientSizePx) {
			WindowDesc desc;
			desc.title = std::move(title);
			desc.clientSize = clientSizePx;
			desc.position = screenPosPx;
			desc.flags = WindowFlags::Resizable | WindowFlags::NoMaximize |
						 _state->floatingPolicy.extraFlags;
			Handle<Window> window = WindowSystem::Create(desc);
			if (!window.IsValid()) {
				return nullptr;
			}
			UISystem::CreateContext(window);
			UI::DockManager* dockManager = UISystem::CreateDockManager(window);
			if (WindowUI* entry = _state->Find(window)) {
				entry->floating = true;
			}
			if (_state->floatingPolicy.onCreated) {
				_state->floatingPolicy.onCreated(window);
			}
			return dockManager;
		}

		void ServiceDeferredOps() {
			UIState& state = *_state;

			Vector<DeferredDetach> detaches{GetMemoryResource(MemTag::UI)};
			detaches.swap(state.pendingDetaches);
			for (const DeferredDetach& detach : detaches) {
				const PanelEntry* panel = nullptr;
				for (const PanelEntry& candidate : state.panels) {
					if (candidate.id == detach.panelId) {
						panel = &candidate;
						break;
					}
				}
				UI::DockManager* dockManager = CreateFloatingDockEntry(
					panel ? panel->title : str("Panel"), detach.screenPos, {480, 360});
				if (dockManager) {
					dockManager->DockPanel(detach.panelId, dockManager->GetRootNode(), UI::DockZone::Center);
				}
			}

			Vector<DeferredAttach> attaches{GetMemoryResource(MemTag::UI)};
			attaches.swap(state.pendingAttaches);
			for (const DeferredAttach& attach : attaches) {
				WindowUI* dragged = state.Find(attach.dragged);
				WindowUI* target = state.Find(attach.target);
				if (!dragged || !target || !dragged->dockManager || !target->dockManager) {
					continue;
				}
				Vector<StringHash> panels{GetMemoryResource(MemTag::UI)};
				dragged->dockManager->GetDockedPanels(panels);
				if (!panels.empty()) {
					target->dockManager->DockPanel(panels[0], attach.drop.node, attach.drop.zone);
					u32 newNode = target->dockManager->FindNodeOfPanel(panels[0]);
					for (size_t i = 1; i < panels.size(); ++i) {
						target->dockManager->DockPanel(panels[i], newNode, UI::DockZone::Center);
					}
				}
				for (StringHash panel : panels) {
					dragged->dockManager->UndockPanel(panel);
				}
				state.pendingCloses.push_back(attach.dragged);
			}

			for (Handle<Window> window : state.pendingCloses) {
				WindowSystem::RequestDestroy(window);
			}
			state.pendingCloses.clear();
		}

	} // anonymous namespace

	void UISystem::Initialize() {
		if (_state) {
			return;
		}
		_state = New<UIState>(MemTag::UI);
		UIState& state = *_state;

		SLT_UI_SYSTEM_ANCHORS(SLT_ANCHOR_TYPE)

		state.renderer = New<RHIUIRenderer>(MemTag::UI, RenderSystem::GetDevice());
		FreeTypeFontSystem::CreateInfo fontInfo;
		fontInfo.backend = state.renderer;
		state.fontSystem = New<FreeTypeFontSystem>(MemTag::UI, fontInfo);
		VectorIconSystem::CreateInfo iconInfo;
		iconInfo.backend = state.renderer;
		state.iconSystem = New<VectorIconSystem>(MemTag::UI, iconInfo);

		Handle<UIState> self(&state);
		EventBus& bus = EventBus::GetGlobal();
		WindowEvents::Resized.Subscribe(bus, self, &UIState::OnWindowResized);
		WindowEvents::DPIChanged.Subscribe(bus, self, &UIState::OnWindowDPIChanged);
		WindowEvents::Destroying.Subscribe(bus, self, &UIState::OnWindowDestroying);
		WindowEvents::DragBegan.Subscribe(bus, self, &UIState::OnDragBegan);
		WindowEvents::DragMoved.Subscribe(bus, self, &UIState::OnDragMoved);
		WindowEvents::DragEnded.Subscribe(bus, self, &UIState::OnDragEnded);

		if (RenderSystem::IsInitialized()) {
			RenderSystem::SetWindowOverlay(&state);
		}
		FrameStages::Register(
			FrameStage::UIUpdate, FrameStagePosition::Normal,
			[](const FrameStageArgs&, void*) { UISystem::Update(); },
			nullptr, &_FRAME_STAGE_OWNER);
		ScreenInternal::Initialize();
		SLT_LOG_INFO("UI", "UISystem initialized{}", RenderSystem::IsRenderingAvailable() ? "" : " (headless)");
	}

	void UISystem::Terminate() {
		if (!_state) {
			return;
		}
		UIState& state = *_state;

		if (RenderSystem::IsInitialized()) {
			RenderSystem::SetWindowOverlay(nullptr);
		}
		ScreenInternal::Terminate();
		FrameStages::UnregisterOwnedBy(&_FRAME_STAGE_OWNER);
		EventBus::GetGlobal().UnsubscribeAll(state.GetInstanceId());

		for (WindowUI& entry : state.windows) {
			Delete(entry.dockManager);
			Delete(entry.context);
		}
		state.windows.clear();

		// before the device goes (surface textures are ours)
		for (UISurface& surface : state.surfaces) {
			if (surface.texture.IsValid()) {
				if (RHI::Device* device = RenderSystem::GetDevice()) {
					device->DestroyResource(surface.texture);
				}
			}
			Delete(surface.context);
		}
		state.surfaces.clear();

		if (Asset* font = state.defaultFont.Get()) {
			AssetDatabase::UnloadAsset(font);
		}
		state.defaultFont = {};

		Delete(state.fontSystem);
		state.fontSystem = nullptr;
		Delete(state.iconSystem);
		state.iconSystem = nullptr;
		Delete(state.renderer);
		state.renderer = nullptr;

		Delete(_state);
		_state = nullptr;
	}

	bool UISystem::IsInitialized() {
		return _state != nullptr;
	}

	UI::Context* UISystem::CreateContext(Handle<Window> window) {
		if (!_state) {
			return nullptr;
		}
		if (WindowUI* existing = _state->Find(window)) {
			return existing->context;
		}
		Window* resolved = window.Get();
		if (!resolved) {
			return nullptr;
		}
		UI::ContextDesc desc;
		desc.sizePx = WindowInternal::GetRenderSize(*resolved);
		desc.contentScale = resolved->GetContentScale() * WindowInternal::RenderScaleOf(*resolved);
		desc.tooltipFont = GetDefaultFont();
		WindowUI entry;
		entry.window = window;
		entry.renderSizeSeen = WindowInternal::GetRenderSizeOverride(*resolved);
		entry.context = New<UI::Context>(MemTag::UI, desc, _state->fontSystem, _state->iconSystem);
		_state->windows.push_back(std::move(entry));
		UI::Context* context = _state->windows.back().context;
		if (_state->contextInitializer) {
			_state->contextInitializer(*context);
		}
		return context;
	}

	void UISystem::SetContextInitializer(ContextInitFn initializer) {
		if (!_state) {
			return;
		}
		_state->contextInitializer = std::move(initializer);
	}

	void UISystem::ForEachContext(const std::function<void(Handle<Window>, UI::Context&)>& visit) {
		if (!_state || !visit) {
			return;
		}
		for (const WindowUI& entry : _state->windows) {
			if (entry.context) {
				visit(entry.window, *entry.context);
			}
		}
	}

	void UISystem::DestroyContext(Handle<Window> window) {
		if (!_state) {
			return;
		}
		for (size_t i = 0; i < _state->windows.size(); ++i) {
			if (_state->windows[i].window == window) {
				Delete(_state->windows[i].dockManager);
				Delete(_state->windows[i].context);
				_state->windows.erase(_state->windows.begin() + static_cast<i64>(i));
				return;
			}
		}
	}

	UI::Context* UISystem::GetContext(Handle<Window> window) {
		if (!_state) {
			return nullptr;
		}
		WindowUI* entry = _state->Find(window);
		return entry ? entry->context : nullptr;
	}

	bool UISystem::IsContextLive(const UI::Context* context) {
		if (!_state || !context) {
			return false;
		}
		for (const WindowUI& entry : _state->windows) {
			if (entry.context == context) {
				return true;
			}
		}
		for (const UISurface& surface : _state->surfaces) {
			if (surface.context == context) {
				return true;
			}
		}
		return false;
	}

	namespace {

		void AllocateSurfaceTexture(UISurface& surface, Vec2I sizePx) {
			RHI::Device* device = RenderSystem::GetDevice();
			if (!device) {
				return;
			}
			if (surface.texture.IsValid()) {
				device->DestroyResource(surface.texture);
				surface.texture = {};
			}
			if (sizePx.x <= 0 || sizePx.y <= 0) {
				return;
			}
			RHI::TextureDesc desc;
			desc.size = sizePx;
			desc.format = RHI::Format::RGBA8Unorm;
			desc.usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferSrc;
			desc.debugName = "UISurface";
			surface.texture = device->CreateTextureResource(desc);
		}

		UISurface* FindSurface(UISurfaceId id) {
			if (!_state || id == INVALID_UI_SURFACE) {
				return nullptr;
			}
			for (UISurface& surface : _state->surfaces) {
				if (surface.id == id) {
					return &surface;
				}
			}
			return nullptr;
		}

		void FreeSurfaceTexture(UISurface& surface) {
			if (!surface.texture.IsValid()) {
				return;
			}
			if (RHI::Device* device = RenderSystem::GetDevice()) {
				device->DestroyResource(surface.texture);
			}
			surface.texture = {};
		}

		f32 SettingsSortOrder(const UISurface& surface) {
			const UIPanelSettings* settings = surface.screenDesc.settings.Get();
			return settings ? settings->sortOrder : 0.0f;
		}

		void SortScreenSurfaces(UIState& state) {
			state.screenOrder.clear();
			for (u32 index = 0; index < static_cast<u32>(state.surfaces.size()); ++index) {
				if (state.surfaces[index].screen) {
					state.screenOrder.push_back(index);
				}
			}
			std::stable_sort(state.screenOrder.begin(), state.screenOrder.end(), [&state](u32 a, u32 b) {
				const UISurface& left = state.surfaces[a];
				const UISurface& right = state.surfaces[b];
				const f32 leftSettings = SettingsSortOrder(left);
				const f32 rightSettings = SettingsSortOrder(right);
				if (leftSettings != rightSettings) {
					return leftSettings < rightSettings;
				}
				if (left.screenDesc.sortOrder != right.screenDesc.sortOrder) {
					return left.screenDesc.sortOrder < right.screenDesc.sortOrder;
				}
				return left.id < right.id;
			});
		}

		InputEvent InContentSpace(const InputEvent& event) {
			const Window* window = event.window.Get();
			if (!window) {
				return event;
			}
			InputEvent mapped = event;
			mapped.mousePosPx = WindowInternal::ClientToRender(*window, event.mousePosPx);
			return mapped;
		}

		bool ReadScreen(const UIState& state, Vec2I& outSizePx, f32& outDPIScale) {
			switch (state.screen.target) {
			case UIScreenDesc::Target::Window: {
				Window* primary = WindowSystem::GetPrimaryWindow().Get();
				if (!primary) {
					return false;
				}
				outSizePx = WindowInternal::GetRenderSize(*primary);
				outDPIScale = primary->GetContentScale() * WindowInternal::RenderScaleOf(*primary);
				break;
			}
			case UIScreenDesc::Target::Texture:
				outSizePx = state.screen.sizePx;
				outDPIScale = state.screen.dpiScale;
				break;
			case UIScreenDesc::Target::None:
				return false;
			}
			return outSizePx.x > 0 && outSizePx.y > 0 && outDPIScale > 0.0f;
		}

		f32 ResolveScreenScale(const UISurface& surface, Vec2I sizePx, f32 dpiScale) {
			const UIPanelSettings* settings = surface.screenDesc.settings.Get();
			return settings ? settings->ResolveContentScale(sizePx, dpiScale)
							: UIPanelSettings::ResolveDefaultContentScale(sizePx, dpiScale);
		}

		void SizeScreenSurface(const UIState& state, UISurface& surface, Vec2I sizePx, f32 dpiScale) {
			const bool wantsTexture = state.screen.target == UIScreenDesc::Target::Texture;
			const f32 scale = ResolveScreenScale(surface, sizePx, dpiScale);
			if (surface.sizePx != sizePx) {
				surface.sizePx = sizePx;
				surface.context->Resize(sizePx);
				if (wantsTexture) {
					AllocateSurfaceTexture(surface, sizePx);
				}
			}
			if (surface.contentScale != scale) {
				surface.contentScale = scale;
				surface.context->SetContentScale(scale);
			}
			if (wantsTexture && !surface.texture.IsValid()) {
				AllocateSurfaceTexture(surface, sizePx);
			} else if (!wantsTexture) {
				FreeSurfaceTexture(surface);
			}
		}

		bool HasElementUnder(const UI::Context& context, Vec2 point) {
			UI::ElementHandle hit = context.HitTest(point);
			return hit.IsValid() && !(hit == context.GetRoot());
		}

		UISurface* TopmostScreenSurfaceUnder(UIState& state, Vec2 point) {
			for (auto it = state.screenOrder.rbegin(); it != state.screenOrder.rend(); ++it) {
				UISurface& surface = state.surfaces[*it];
				if (HasElementUnder(*surface.context, point)) {
					return &surface;
				}
			}
			return nullptr;
		}

		UISurface* FindScreenSurface(UIState& state, UISurfaceId id) {
			if (id == INVALID_UI_SURFACE) {
				return nullptr;
			}
			for (u32 index : state.screenOrder) {
				if (state.surfaces[index].id == id) {
					return &state.surfaces[index];
				}
			}
			return nullptr;
		}

		void RouteScreenInput(UIState& state, const InputEvent& event, const UI::Context* above) {
			if (state.screenOrder.empty()) {
				return;
			}
			const Vec2 point{static_cast<f32>(event.mousePosPx.x), static_cast<f32>(event.mousePosPx.y)};
			auto occluded = [&]() { return above && HasElementUnder(*above, point); };
			auto leave = [&](UISurface& surface) {
				if (!surface.pointerInside) {
					return;
				}
				InputEvent out{};
				out.type = InputEventType::MouseLeave;
				out.window = event.window;
				out.mods = event.mods;
				surface.context->InjectInput(out);
				surface.pointerInside = false;
			};
			auto deliverPointer = [&](UISurface& surface) {
				surface.context->InjectInput(event);
				surface.pointerInside = true;
			};

			switch (event.type) {
			case InputEventType::MouseMove: {
				UISurface* target = FindScreenSurface(state, state.screenCapture);
				if (!target && !occluded()) {
					target = TopmostScreenSurfaceUnder(state, point);
				}
				for (u32 index : state.screenOrder) {
					UISurface& surface = state.surfaces[index];
					if (&surface == target) {
						deliverPointer(surface);
					} else {
						leave(surface);
					}
				}
				break;
			}
			case InputEventType::MouseDown: {
				UISurface* target = FindScreenSurface(state, state.screenCapture);
				if (!target && !occluded()) {
					target = TopmostScreenSurfaceUnder(state, point);
				}
				if (!target) {
					state.screenKeyboard = INVALID_UI_SURFACE;
					if (occluded()) {
						break;
					}
					InputEvent release = event;
					release.type = InputEventType::MouseUp;
					for (u32 index : state.screenOrder) {
						UISurface& surface = state.surfaces[index];
						surface.context->InjectInput(event);
						surface.context->InjectInput(release);
					}
					break;
				}
				state.screenKeyboard = target->id;
				deliverPointer(*target);
				state.screenCapture = target->id;
				break;
			}
			case InputEventType::MouseUp: {
				UISurface* target = FindScreenSurface(state, state.screenCapture);
				if (!target && !occluded()) {
					target = TopmostScreenSurfaceUnder(state, point);
				}
				if (target) {
					deliverPointer(*target);
				}
				state.screenCapture = INVALID_UI_SURFACE;
				break;
			}
			case InputEventType::MouseWheel: {
				if (!occluded()) {
					if (UISurface* target = TopmostScreenSurfaceUnder(state, point)) {
						deliverPointer(*target);
					}
				}
				break;
			}
			case InputEventType::KeyDown:
			case InputEventType::KeyUp:
			case InputEventType::Text: {
				if (above && above->WantsKeyboard()) {
					break;
				}
				UISurface* target = FindScreenSurface(state, state.screenKeyboard);
				if (!target) {
					for (auto it = state.screenOrder.rbegin(); it != state.screenOrder.rend(); ++it) {
						UISurface& surface = state.surfaces[*it];
						if (surface.context->GetFocused().IsValid()) {
							target = &surface;
							break;
						}
					}
				}
				if (target) {
					target->context->InjectInput(event);
				}
				break;
			}
			case InputEventType::MouseLeave: {
				for (u32 index : state.screenOrder) {
					leave(state.surfaces[index]);
				}
				break;
			}
			case InputEventType::MouseEnter:
			case InputEventType::GamepadConnected:
			case InputEventType::GamepadDisconnected:
				break;
			}
		}

	} // anonymous namespace

	UISurfaceId UISystem::CreateSurface(Vec2I sizePx, f32 contentScale) {
		if (!_state || sizePx.x <= 0 || sizePx.y <= 0 || contentScale <= 0.0f) {
			return INVALID_UI_SURFACE;
		}
		UI::ContextDesc desc;
		desc.sizePx = sizePx;
		desc.contentScale = contentScale;
		desc.tooltipFont = GetDefaultFont();

		UISurface surface;
		surface.id = _state->nextSurfaceId++;
		surface.sizePx = sizePx;
		surface.contentScale = contentScale;
		surface.context = New<UI::Context>(MemTag::UI, desc, _state->fontSystem, _state->iconSystem);
		AllocateSurfaceTexture(surface, sizePx);
		_state->surfaces.push_back(surface);
		return surface.id;
	}

	void UISystem::DestroySurface(UISurfaceId surface) {
		if (!_state) {
			return;
		}
		for (size_t i = 0; i < _state->surfaces.size(); ++i) {
			if (_state->surfaces[i].id != surface) {
				continue;
			}
			FreeSurfaceTexture(_state->surfaces[i]);
			Delete(_state->surfaces[i].context);
			const bool wasScreen = _state->surfaces[i].screen;
			_state->surfaces.erase(_state->surfaces.begin() + static_cast<i64>(i));
			if (wasScreen) {
				if (_state->screenCapture == surface) {
					_state->screenCapture = INVALID_UI_SURFACE;
				}
				if (_state->screenKeyboard == surface) {
					_state->screenKeyboard = INVALID_UI_SURFACE;
				}
				SortScreenSurfaces(*_state);
			}
			return;
		}
	}

	void UISystem::ResizeSurface(UISurfaceId surface, Vec2I sizePx, f32 contentScale) {
		UISurface* entry = FindSurface(surface);
		if (!entry || sizePx.x <= 0 || sizePx.y <= 0 || contentScale <= 0.0f) {
			return;
		}
		if (entry->sizePx != sizePx) {
			entry->sizePx = sizePx;
			entry->context->Resize(sizePx);
			AllocateSurfaceTexture(*entry, sizePx);
		}
		if (entry->contentScale != contentScale) {
			entry->contentScale = contentScale;
			entry->context->SetContentScale(contentScale);
		}
	}

	UI::Context* UISystem::GetSurfaceContext(UISurfaceId surface) {
		UISurface* entry = FindSurface(surface);
		return entry ? entry->context : nullptr;
	}

	void UISystem::ForEachSurface(const std::function<void(UISurfaceId, UI::Context&)>& visit) {
		if (!_state || !visit) {
			return;
		}
		for (const UISurface& surface : _state->surfaces) {
			if (surface.context) {
				visit(surface.id, *surface.context);
			}
		}
	}

	RHI::TextureHandle UISystem::GetSurfaceTexture(UISurfaceId surface) {
		UISurface* entry = FindSurface(surface);
		return entry ? entry->texture : RHI::TextureHandle{};
	}

	void UISystem::SetScreen(const UIScreenDesc& desc) {
		if (!_state) {
			return;
		}
		_state->screen = desc;
	}

	UISurfaceId UISystem::CreateScreenSurface(const UIScreenSurfaceDesc& desc) {
		if (!_state) {
			return INVALID_UI_SURFACE;
		}
		Vec2I sizePx{1, 1};
		f32 dpiScale = 1.0f;
		const bool live = ReadScreen(*_state, sizePx, dpiScale);
		if (!live) {
			sizePx = {1, 1};
			dpiScale = 1.0f;
		}

		UISurface surface;
		surface.id = _state->nextSurfaceId++;
		surface.screen = true;
		surface.screenDesc = desc;
		surface.sizePx = sizePx;
		surface.contentScale = ResolveScreenScale(surface, sizePx, dpiScale);

		UI::ContextDesc contextDesc;
		contextDesc.sizePx = sizePx;
		contextDesc.contentScale = surface.contentScale;
		contextDesc.tooltipFont = GetDefaultFont();
		surface.context = New<UI::Context>(MemTag::UI, contextDesc, _state->fontSystem, _state->iconSystem);
		if (live && _state->screen.target == UIScreenDesc::Target::Texture) {
			AllocateSurfaceTexture(surface, sizePx);
		}
		_state->surfaces.push_back(std::move(surface));
		SortScreenSurfaces(*_state);
		return _state->surfaces.back().id;
	}

	void UISystem::SetScreenSurfaceDesc(UISurfaceId surface, const UIScreenSurfaceDesc& desc) {
		UISurface* entry = FindSurface(surface);
		if (!entry || !entry->screen) {
			return;
		}
		entry->screenDesc = desc;
		SortScreenSurfaces(*_state);
	}

	void UISystem::ForEachScreenSurface(const std::function<void(UISurfaceId, UI::Context&)>& visit) {
		if (!_state || !visit) {
			return;
		}
		SortScreenSurfaces(*_state);
		Vector<UISurfaceId> ids{GetMemoryResource(MemTag::UI)};
		ids.reserve(_state->screenOrder.size());
		for (u32 index : _state->screenOrder) {
			ids.push_back(_state->surfaces[index].id);
		}
		for (UISurfaceId id : ids) {
			if (UISurface* entry = FindSurface(id)) {
				visit(id, *entry->context);
			}
		}
	}

	std::string_view UISystem::GetSurfaceName(UISurfaceId surface) {
		UISurface* entry = FindSurface(surface);
		return entry && entry->screen ? std::string_view(entry->screenDesc.name) : std::string_view{};
	}

	void UISystem::InjectScreenInput(const InputEvent& event) {
		if (!_state) {
			return;
		}
		RouteScreenInput(*_state, event, nullptr);
	}

	UI::DockManager* UISystem::CreateDockManager(Handle<Window> window) {
		if (!_state) {
			return nullptr;
		}
		UI::Context* context = CreateContext(window);
		if (!context) {
			return nullptr;
		}
		WindowUI* entry = _state->Find(window);
		if (entry->dockManager) {
			return entry->dockManager;
		}
		entry->dockManager = New<UI::DockManager>(MemTag::UI, *context, GetDefaultFont());
		ReplayPanels(*entry->dockManager);
		InstallDetach(*entry);
		return entry->dockManager;
	}

	UI::DockManager* UISystem::GetDockManager(Handle<Window> window) {
		if (!_state) {
			return nullptr;
		}
		WindowUI* entry = _state->Find(window);
		return entry ? entry->dockManager : nullptr;
	}

	void UISystem::RegisterPanel(StringHash id, std::string_view title, UI::DockBuildFn build, const UI::DockTabIcon& icon) {
		if (!_state) {
			return;
		}
		for (PanelEntry& panel : _state->panels) {
			if (panel.id == id) {
				panel.title = str(title);
				panel.build = build;
				panel.iconImage = icon.image;
				panel.iconSizePx = icon.sizePx;
				for (WindowUI& entry : _state->windows) {
					if (entry.dockManager) {
						entry.dockManager->RegisterPanel(id, title, build, IconOf(panel));
					}
				}
				return;
			}
		}
		_state->panels.push_back({id, str(title), build, icon.image, icon.sizePx});
		for (WindowUI& entry : _state->windows) {
			if (entry.dockManager) {
				entry.dockManager->RegisterPanel(id, title, build, icon);
			}
		}
	}

	void UISystem::SetPanelTitle(StringHash id, std::string_view title) {
		if (!_state) {
			return;
		}
		for (PanelEntry& panel : _state->panels) {
			if (panel.id != id) {
				continue;
			}
			panel.title = str(title);
			for (WindowUI& entry : _state->windows) {
				if (entry.dockManager) {
					entry.dockManager->SetPanelTitle(id, title);
				}
			}
			return;
		}
	}

	void UISystem::UnregisterPanel(StringHash id) {
		if (!_state) {
			return;
		}
		for (size_t i = 0; i < _state->panels.size(); ++i) {
			if (_state->panels[i].id == id) {
				_state->panels.erase(_state->panels.begin() + static_cast<i64>(i));
				break;
			}
		}
		for (WindowUI& entry : _state->windows) {
			if (entry.dockManager) {
				entry.dockManager->UnregisterPanel(id);
			}
		}
	}

	void UISystem::SetPanelCloseCallback(UI::DockPanelCloseFn callback) {
		if (_state) {
			_state->panelCloseCallback = std::move(callback);
		}
	}

	void UISystem::SetFloatingDockWindowPolicy(FloatingDockWindowPolicy policy) {
		if (_state) {
			_state->floatingPolicy = std::move(policy);
		}
	}

	void UISystem::SetDockHost(Handle<Window> window, UI::ElementHandle host) {
		if (!_state) {
			return;
		}
		if (WindowUI* entry = _state->Find(window)) {
			entry->dockHost = host;
		}
	}

	Handle<Window> UISystem::CreateFloatingDockWindow(Vec2I screenPosPx, Vec2I clientSizePx) {
		if (!_state || !WindowSystem::IsInitialized()) {
			return {};
		}
		UI::DockManager* dockManager = CreateFloatingDockEntry(str("Panels"), screenPosPx, clientSizePx);
		if (!dockManager) {
			return {};
		}
		for (WindowUI& entry : _state->windows) {
			if (entry.dockManager == dockManager) {
				return entry.window;
			}
		}
		return {};
	}

	void UISystem::GetDockWindows(Vector<Handle<Window>>& out) {
		if (!_state) {
			return;
		}
		for (const WindowUI& entry : _state->windows) {
			if (entry.dockManager) {
				out.push_back(entry.window);
			}
		}
	}

	bool UISystem::IsFloatingDockWindow(Handle<Window> window) {
		if (!_state) {
			return false;
		}
		WindowUI* entry = _state->Find(window);
		return entry && entry->floating;
	}

	UI::IFontSystem* UISystem::GetFontSystem() {
		return _state ? _state->fontSystem : nullptr;
	}

	UI::IVectorIconSystem* UISystem::GetVectorIconSystem() {
		return _state ? _state->iconSystem : nullptr;
	}

	Handle<FontAsset> UISystem::GetDefaultFont() {
		if (!_state) {
			return {};
		}
		UIState& state = *_state;
		if (!state.defaultFontAttempted) {
			state.defaultFontAttempted = true;
			Asset* asset = AssetDatabase::LoadAssetByGuid(EngineBuiltins::DefaultFont);
			if (asset && asset->GetAssetTypeId() == StringHash("FontAsset")) {
				state.defaultFont = Handle<FontAsset>(static_cast<FontAsset*>(asset));
			} else {
				if (asset) {
					AssetDatabase::UnloadAsset(asset);
				}
				SLT_LOG_WARN("UI", "the default font (Inter-Regular) is unavailable");
			}
		}
		return state.defaultFont;
	}

	void UISystem::Update() {
		if (!_state) {
			return;
		}
		SLT_PROFILE_ZONE("UI.Update");
		UIState& state = *_state;
		state.frameDrawCalls = 0;
		state.frameQuads = 0;

		if (state.fontSystem) {
			state.fontSystem->ReclaimIfFull();
		}

		ServiceDeferredOps();

		SortScreenSurfaces(state);
		if (!state.screenOrder.empty()) {
			Vec2I screenSize{};
			f32 screenDPI = 1.0f;
			const bool screenLive = ReadScreen(state, screenSize, screenDPI);
			for (u32 index : state.screenOrder) {
				UISurface& surface = state.surfaces[index];
				if (screenLive) {
					SizeScreenSurface(state, surface, screenSize, screenDPI);
				} else {
					FreeSurfaceTexture(surface);
				}
			}
		}

		for (WindowUI& entry : state.windows) {
			if (const Window* window = entry.window.Get()) {
				if (WindowInternal::GetRenderSizeOverride(*window) != entry.renderSizeSeen) {
					state.SyncWindowContext(entry, *window);
				}
			}
		}

		for (const InputEvent& event : Input::GetFrameEvents()) {
			if (!event.window.IsValid()) {
				continue;
			}
			if (WindowUI* entry = state.Find(event.window)) {
				entry->context->InjectInput(InContentSpace(event));
			}
		}
		if (state.screen.target == UIScreenDesc::Target::Window && !state.screenOrder.empty()) {
			Handle<Window> primary = WindowSystem::GetPrimaryWindow();
			if (primary.IsValid()) {
				WindowUI* primaryEntry = state.Find(primary);
				const UI::Context* above = primaryEntry ? primaryEntry->context : nullptr;
				for (const InputEvent& event : Input::GetFrameEvents()) {
					if (event.window == primary) {
						RouteScreenInput(state, InContentSpace(event), above);
					}
				}
			}
		}

		if (state.dragPayloadActive) {
			UpdateDragPayloadFlow(state);
		}

		bool leftDown = Input::IsMouseButtonDown(MouseButton::Left);
		bool leftPressed = Input::WasMouseButtonPressed(MouseButton::Left);

		for (WindowUI& entry : state.windows) {
			Window* window = entry.window.Get();
			if (!window) {
				continue;
			}

			bool suppressHover = state.dragPayloadActive || state.dragTarget.IsValid();
			if (!suppressHover) {
				for (WindowUI& other : state.windows) {
					if (other.dockManager && other.dockManager->IsDraggingTab()) {
						suppressHover = true;
						break;
					}
				}
			}
			entry.context->SetHoverSuppressed(suppressHover);

			entry.context->RouteInput();
			entry.context->SettleLayout();

			if (entry.dockManager) {
				Vec2I clientMouse = Input::GetMousePosition(entry.window);
				bool focused = window->IsFocused();
				entry.dockManager->UpdateInteraction(
					{static_cast<f32>(clientMouse.x), static_cast<f32>(clientMouse.y)},
					Input::GetMousePosition(),
					focused && leftDown, focused && leftPressed);
				UI::ElementHandle buildParent = entry.context->GetRoot();
				if (entry.dockHost.IsValid() && entry.context->IsAlive(entry.dockHost)) {
					buildParent = entry.dockHost;
				}
				entry.dockManager->Build(buildParent);

				if (entry.floating) {
					Vector<StringHash> docked{GetMemoryResource(MemTag::UI)};
					entry.dockManager->GetDockedPanels(docked);
					if (docked.empty()) {
						state.pendingCloses.push_back(entry.window);
					}
				}
			}

			entry.context->SettleLayout();
			window->SetCursor(entry.context->GetDesiredCursor());
			// cursor mode has 1 writer per context window
			const bool claimLive = entry.context->_impl->pointerClaim != 0;
			if (!state.cursorModeYielded || claimLive || entry.owesCursorRestore) {
				window->SetCursorMode(entry.context->GetPointerClaimMode());
			}
			entry.owesCursorRestore = claimLive;
		}

		for (UISurface& surface : state.surfaces) {
			surface.context->Update();
		}

		if (state.iconSystem) {
			state.iconSystem->EndFrame();
		}
	}

	bool UISystem::WantsMouse(Handle<Window> window) {
		UI::Context* context = GetContext(window);
		if (context && context->WantsMouse()) {
			return true;
		}
		return _state && _state->ScreenIsWindow(window) && ScreenWantsMouse();
	}

	bool UISystem::ScreenWantsMouse() {
		if (!_state) {
			return false;
		}
		for (u32 index : _state->screenOrder) {
			if (_state->surfaces[index].context->WantsMouse()) {
				return true;
			}
		}
		return false;
	}

	bool UISystem::WantsKeyboard(Handle<Window> window) {
		UI::Context* context = GetContext(window);
		if (context && context->WantsKeyboard()) {
			return true;
		}
		if (_state && _state->ScreenIsWindow(window)) {
			for (u32 index : _state->screenOrder) {
				if (_state->surfaces[index].context->WantsKeyboard()) {
					return true;
				}
			}
		}
		return false;
	}

	void UISystem::SetPanelDetachEnabled(bool enabled) {
		if (_state) {
			_state->detachEnabled = enabled;
		}
	}

	void UISystem::SetCursorModeYielded(bool yielded) {
		if (_state) {
			_state->cursorModeYielded = yielded;
		}
	}

	void UISystem::BeginDragPayload(const UI::DragPayload& payload) {
		if (!_state) {
			return;
		}
		_state->dragPayload = payload;
		_state->dragPayloadActive = true;
	}

	bool UISystem::IsDragPayloadActive() {
		return _state && _state->dragPayloadActive;
	}

	const UI::DragPayload* UISystem::GetDragPayload() {
		return (_state && _state->dragPayloadActive) ? &_state->dragPayload : nullptr;
	}

	void UISystem::CancelDragPayload() {
		if (_state && _state->dragPayloadActive) {
			EndDragPayload(*_state);
		}
	}

	UISystem::Stats UISystem::GetStats() {
		Stats stats;
		if (_state) {
			stats.drawCalls = _state->frameDrawCalls;
			stats.quads = _state->frameQuads;
			stats.contexts = static_cast<u32>(_state->windows.size() + _state->surfaces.size());
		}
		return stats;
	}

	namespace UIScreenInternal {

		UIScreenDesc::Target GetTarget() {
			return _state ? _state->screen.target : UIScreenDesc::Target::None;
		}

		Vec2I GetSize() {
			Vec2I size{};
			f32 dpiScale = 1.0f;
			if (!_state || !ReadScreen(*_state, size, dpiScale)) {
				return {};
			}
			return size;
		}

	} // namespace UIScreenInternal

	namespace ModuleInternal {

		void CaptureUISystemEngineState(EngineStateTable& table) {
			table.uiSystem = _state;
		}

		void BindUISystemEngineState(const EngineStateTable& table) {
			_state = static_cast<UIState*>(table.uiSystem);
		}

	} // namespace ModuleInternal

} // namespace Shalltear
