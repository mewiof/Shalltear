#include "Shalltear/Editor/PanelManager.h"

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Input.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/Window/Window.h"

#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorExtensions.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorListView.h"
#include "Shalltear/Editor/EditorTheme.h"

#include "SceneFileFlows.h"

#include <cmath>

namespace Shalltear {

	namespace {

		struct PanelRecord {
			Handle<EditorPanel> panel;
			StringHash typeName{};
			u32 serial = 0;
			StringHash dockId{};
			UI::Context* buildContext = nullptr;
			UI::ElementHandle buildRoot{};
			UI::ElementHandle playTint{};
			str shownTitle;
			UI::ElementHandle closePrompt{};
		};

		struct CapturedPanel {
			StringHash typeName{};
			u32 serial = 0;
			StringHash dockId;
			str stateJSON;
			Handle<Window> window;
			StringHash holdId;
			StringHash activePanel;
			bool hadDockSlot = false;
		};
		void ReleaseHold(const CapturedPanel& capture);

		class PanelManagerState : public Object {
		public:
			Vector<PanelRecord> records{GetMemoryResource(MemTag::Editor)};
			Vector<CapturedPanel> captured{GetMemoryResource(MemTag::Editor)};
			Vector<Object*> pendingDestroy{GetMemoryResource(MemTag::Editor)};
			struct PendingFloat {
				StringHash dockId;
				Vec2 size;
			};
			Vector<PendingFloat> pendingFloats{GetMemoryResource(MemTag::Editor)};
			Handle<Window> defaultWindow;
			StringHash focusedDockId{};
			PlayModeState playState = PlayModeState::Editing;

			void OnModuleReloadBegan(const EditorModuleReloadBeganEvent& event) {
				PanelManager::CaptureModulePanels(event.moduleBase);
				EditorExtensions::Unload(event.moduleBase);
			}

			void OnModuleReloadEnded(const EditorModuleReloadEndedEvent& event) {
				EditorExtensions::Load(event.moduleBase);
				PanelManager::RestoreModulePanels();
			}

			void OnPlayModeChanged(const EditorPlayModeChangedEvent& event);

			void OnSelectionChanged(const EditorSelectionChangedEvent&) {
				Vector<Handle<EditorPanel>> open{GetMemoryResource(MemTag::Editor)};
				for (const PanelRecord& record : records) {
					open.push_back(record.panel);
				}
				for (Handle<EditorPanel>& handle : open) {
					if (EditorPanel* panel = handle.Get()) {
						panel->OnSelectionChanged();
					}
				}
			}
		};

		PanelManagerState* _state = nullptr;

		PanelRecord* FindRecord(Handle<EditorPanel> panel) {
			for (PanelRecord& record : _state->records) {
				if (record.panel == panel) {
					return &record;
				}
			}
			return nullptr;
		}

		PanelRecord* FindRecordByDockId(StringHash dockId) {
			for (PanelRecord& record : _state->records) {
				if (record.dockId == dockId) {
					return &record;
				}
			}
			return nullptr;
		}

		UI::DockTabIcon TabIconOf(const EditorPanel& panel) {
			EditorIcons::Icon icon = panel.GetTabIcon();
			if (icon.glyph.empty()) {
				return {};
			}
			UI::DockTabIcon tab;
			tab.image = EditorIcons::GetGlyph(icon);
			tab.sizePx = EditorIcons::TabSizePx() * EditorIcons::GlyphSizeScale(icon);
			return tab;
		}

		u32 LowestFreeSerial(StringHash typeName) {
			for (u32 candidate = 1;; ++candidate) {
				bool taken = false;
				for (const PanelRecord& record : _state->records) {
					if (record.typeName == typeName && record.serial == candidate) {
						taken = true;
						break;
					}
				}
				if (!taken) {
					return candidate;
				}
			}
		}

		bool IsOpenablePanelType(const TypeInfo* type) {
			return type != nullptr &&
				   type->createFn != nullptr &&
				   type != EditorPanel::GetReflectType() &&
				   TypeRegistry::IsDerivedFrom(type, EditorPanel::GetReflectType());
		}

		void SyncPlayTint(PanelRecord& record, bool playing) {
			UI::Context* context = record.buildContext;
			if (!context || !UISystem::IsContextLive(context) || !context->IsAlive(record.buildRoot)) {
				return;
			}
			EditorPanel* panel = record.panel.Get();
			const bool wanted = playing && panel && panel->WantsPlayModeTint();
			if (!wanted) {
				if (context->IsAlive(record.playTint)) {
					context->DestroyElement(record.playTint);
				}
				record.playTint = {};
				return;
			}
			if (context->IsAlive(record.playTint)) {
				return;
			}
			record.playTint = context->CreateElement();
			context->AppendChild(record.buildRoot, record.playTint);
			context->AddClass(record.playTint, EditorTheme::Class::PlayTint);
		}

		StringHash PanelOwningElement(const UI::Context& context, UI::ElementHandle element) {
			for (UI::ElementHandle walk = element; walk.IsValid();) {
				for (const PanelRecord& record : _state->records) {
					if (record.buildContext == &context && record.buildRoot == walk) {
						return record.dockId;
					}
				}
				const UI::Element* node = context.GetElement(walk);
				if (!node || node->parent == UI::INVALID_UI_INDEX) {
					break;
				}
				walk = context.MakeHandle(node->parent);
			}
			return {};
		}

		void UpdateFocusedPanel() {
			if (!UISystem::IsInitialized()) {
				return;
			}
			Vector<Handle<Window>> windows{GetMemoryResource(MemTag::Editor)};
			UISystem::GetDockWindows(windows);
			if (windows.empty()) {
				return;
			}

			for (Handle<Window> window : windows) {
				const UI::DockManager* dock = UISystem::GetDockManager(window);
				if (!dock) {
					continue;
				}
				StringHash live = dock->GetFocusedPanel();
				if (live.GetValue() != 0 && live != _state->focusedDockId) {
					_state->focusedDockId = live;
					break;
				}
			}

			if (_state->focusedDockId.GetValue() != 0 && !FindRecordByDockId(_state->focusedDockId)) {
				_state->focusedDockId = {};
			}

			for (const InputEvent& event : Input::GetFrameEvents()) {
				if (event.type != InputEventType::MouseDown) {
					continue;
				}
				UI::Context* context = UISystem::GetContext(event.window);
				if (!context) {
					continue;
				}
				const Vec2 point{static_cast<f32>(event.mousePosPx.x), static_cast<f32>(event.mousePosPx.y)};
				StringHash hit = PanelOwningElement(*context, context->HitTest(point));
				if (hit.GetValue() != 0) {
					_state->focusedDockId = hit;
				}
			}

			for (Handle<Window> window : windows) {
				if (UI::DockManager* dock = UISystem::GetDockManager(window)) {
					dock->SetFocusedPanel(_state->focusedDockId);
				}
			}
		}

		void CloseInternal(PanelRecord record, bool destroyImmediately) {
			if (EditorPanel* panel = record.panel.Get()) {
				panel->OnClose();
			}
			if (UISystem::IsInitialized()) {
				UISystem::UnregisterPanel(record.dockId);
			}
			if (Object* raw = record.panel.Get()) {
				if (destroyImmediately) {
					TypeRegistry::DestroyObjectInstance(raw);
				} else {
					_state->pendingDestroy.push_back(raw);
				}
			}
		}

		void PanelManagerState::OnPlayModeChanged(const EditorPlayModeChangedEvent& event) {
			playState = event.state;
			for (PanelRecord& record : records) {
				SyncPlayTint(record, playState != PlayModeState::Editing);
			}
		}

	} // anonymous namespace

	void PanelManager::Initialize() {
		if (_state) {
			return;
		}
		_state = New<PanelManagerState>(MemTag::Editor);

		Handle<PanelManagerState> self(_state);
		EventBus& bus = EventBus::GetGlobal();
		EditorEvents::SelectionChanged.Subscribe(bus, self, &PanelManagerState::OnSelectionChanged);
		EditorEvents::PlayModeChanged.Subscribe(bus, self, &PanelManagerState::OnPlayModeChanged);
		EditorEvents::ModuleReloadBegan.Subscribe(bus, self, &PanelManagerState::OnModuleReloadBegan);
		EditorEvents::ModuleReloadEnded.Subscribe(bus, self, &PanelManagerState::OnModuleReloadEnded);

		if (UISystem::IsInitialized()) {
			UISystem::SetPanelCloseCallback(&PanelManager::CloseByDockId);
		}
	}

	void PanelManager::Terminate() {
		if (!_state) {
			return;
		}
		for (const CapturedPanel& stale : _state->captured) {
			ReleaseHold(stale);
		}
		_state->captured.clear();
		CloseAllPanels();
		for (Object* raw : _state->pendingDestroy) {
			TypeRegistry::DestroyObjectInstance(raw);
		}
		_state->pendingDestroy.clear();
		if (UISystem::IsInitialized()) {
			UISystem::SetPanelCloseCallback({});
		}
		EventBus::GetGlobal().UnsubscribeAll(_state->GetInstanceId());
		Delete(_state);
		_state = nullptr;
	}

	void PanelManager::SetDefaultWindow(Handle<Window> window) {
		if (_state) {
			_state->defaultWindow = window;
		}
	}

	Handle<EditorPanel> PanelManager::OpenInternal(const TypeInfo* type, u32 serial, std::string_view stateJSON,
												   Handle<Window> targetWindow, bool autoDock) {
		Object* raw = TypeRegistry::CreateObjectInstance(type, MemTag::Editor);
		if (!raw) {
			SLT_LOG_WARN("Editor", "panel type {:#x} couldn't be instantiated", type->name.GetValue());
			return {};
		}
		EditorPanel* panel = static_cast<EditorPanel*>(raw);
		panel->_instanceSerial = serial;
		panel->_dockId = MakeDockId(type, serial);

		if (!stateJSON.empty()) {
			TextSerializer::Deserialize(panel, type, stateJSON);
		}

		Handle<EditorPanel> handle(panel);
		_state->records.push_back({handle, type->name, serial, panel->_dockId});

		if (UISystem::IsInitialized()) {
			StringHash dockId = panel->_dockId;
			UISystem::RegisterPanel(dockId, panel->GetTitle(), [handle, dockId](UI::Context& context, UI::ElementHandle parent) {
										if (EditorPanel* live = handle.Get()) {
											live->BuildContent(context, parent);
										}
										if (PanelRecord* record = FindRecordByDockId(dockId)) {
											record->buildContext = &context;
											record->buildRoot = parent;
											record->playTint = {};
											SyncPlayTint(*record, _state->playState != PlayModeState::Editing);
										} }, TabIconOf(*panel));
			if (autoDock) {
				Handle<Window> window = targetWindow.IsValid() ? targetWindow : _state->defaultWindow;
				if (UI::DockManager* dock = UISystem::GetDockManager(window)) {
					dock->DockPanel(panel->_dockId, dock->GetRootNode(), UI::DockZone::Center);
				}
			}
		}

		panel->OnOpen();
		return handle;
	}

	Handle<EditorPanel> PanelManager::OpenPanel(const TypeInfo* type, Handle<Window> targetWindow) {
		if (!_state || !IsOpenablePanelType(type)) {
			SLT_LOG_WARN("Editor", "OpenPanel: not an openable panel type");
			return {};
		}
		const AttributeInfo* floating = FindTypeAttribute(type, StringHash("EditorPanelFloatingSize"));
		if (!targetWindow.IsValid() && floating && floating->value.Is<Vec2>() && UISystem::IsInitialized()) {
			Handle<EditorPanel> panel = OpenInternal(type, LowestFreeSerial(type->name), {}, {}, false);
			if (EditorPanel* live = panel.Get()) {
				_state->pendingFloats.push_back({live->GetDockId(), floating->value.As<Vec2>()});
			}
			return panel;
		}
		return OpenInternal(type, LowestFreeSerial(type->name), {}, targetWindow, true);
	}

	Handle<EditorPanel> PanelManager::RestorePanel(const TypeInfo* type, u32 serial, std::string_view stateJSON) {
		if (!_state || !IsOpenablePanelType(type) || serial == 0) {
			return {};
		}
		for (const PanelRecord& record : _state->records) {
			if (record.typeName == type->name && record.serial == serial) {
				SLT_LOG_WARN("Editor", "RestorePanel: serial {} already open for type {:#x}", serial, type->name.GetValue());
				return {};
			}
		}
		return OpenInternal(type, serial, stateJSON, {}, false);
	}

	void PanelManager::ClosePanel(Handle<EditorPanel> panel) {
		if (!_state) {
			return;
		}
		for (size_t i = 0; i < _state->records.size(); ++i) {
			if (_state->records[i].panel == panel) {
				PanelRecord record = _state->records[i];
				_state->records.erase(_state->records.begin() + static_cast<i64>(i));
				CloseInternal(record, false);
				return;
			}
		}
	}

	void PanelManager::CloseAllPanels() {
		if (!_state) {
			return;
		}
		Vector<PanelRecord> records{GetMemoryResource(MemTag::Editor)};
		records.swap(_state->records);
		for (PanelRecord& record : records) {
			CloseInternal(record, true);
		}
	}

	void PanelManager::CloseByDockId(StringHash dockId) {
		Handle<EditorPanel> panel = PanelManager::FindPanelByDockId(dockId);
		if (!panel.IsValid()) {
			SLT_LOG_WARN("Editor", "close requested on unmanaged dock panel {:#x}", dockId.GetValue());
			return;
		}
		PanelRecord* record = FindRecordByDockId(dockId);
		UI::Context* context = record ? record->buildContext : nullptr;
		if (context && UISystem::IsInitialized() && !UISystem::IsContextLive(context)) {
			context = nullptr;
		}
		if (!panel->HasUnsavedChanges() || !context) {
			PanelManager::ClosePanel(panel);
			return;
		}
		if (context->IsAlive(record->closePrompt)) {
			return;
		}
		auto answer = [panel](bool save) {
			EditorPanel* live = panel.Get();
			if (!live) {
				return;
			}
			if (save) {
				live->SaveChanges();
				if (live->HasUnsavedChanges()) {
					return;
				}
			} else {
				live->DiscardChanges();
			}
			PanelManager::ClosePanel(panel);
		};
		record->closePrompt = SceneFileFlows::OpenConfirmPopup(
			*context, panel->GetSaveChangesMessage(), "Save", [answer]() { answer(true); }, "Discard",
			[answer]() { answer(false); });
	}

	void PanelManager::ConfirmUnsavedPanelsThen(UI::Context& context, std::function<void()> proceed) {
		Vector<Handle<EditorPanel>> unsaved{GetMemoryResource(MemTag::Editor)};
		Vector<str> messages{GetMemoryResource(MemTag::Editor)};
		if (_state) {
			for (const PanelRecord& record : _state->records) {
				EditorPanel* panel = record.panel.Get();
				if (panel && panel->HasUnsavedChanges()) {
					unsaved.push_back(record.panel);
					messages.push_back(panel->GetSaveChangesMessage());
				}
			}
		}
		if (unsaved.empty()) {
			if (proceed) {
				proceed();
			}
			return;
		}
		SceneFileFlows::OpenDetailedConfirmPopup(
			context, unsaved.size() == 1 ? str("A window has unsaved changes.") : str("Windows have unsaved changes."),
			{messages.data(), messages.size()}, "Save All & Continue",
			[unsaved, proceed]() {
				for (const Handle<EditorPanel>& handle : unsaved) {
					if (EditorPanel* panel = handle.Get()) {
						panel->SaveChanges();
						if (panel->HasUnsavedChanges()) {
							return;
						}
					}
				}
				if (proceed) {
					proceed();
				}
			},
			"Discard",
			[unsaved, proceed]() {
				for (const Handle<EditorPanel>& handle : unsaved) {
					if (EditorPanel* panel = handle.Get()) {
						panel->DiscardChanges();
					}
				}
				if (proceed) {
					proceed();
				}
			});
	}

	Handle<EditorPanel> PanelManager::FindPanelByDockId(StringHash dockId) {
		if (!_state) {
			return {};
		}
		for (const PanelRecord& record : _state->records) {
			if (record.dockId == dockId) {
				return record.panel;
			}
		}
		return {};
	}

	Handle<EditorPanel> PanelManager::GetFocusedPanel() {
		if (!_state || _state->focusedDockId.GetValue() == 0) {
			return {};
		}
		return FindPanelByDockId(_state->focusedDockId);
	}

	PlayModeState PanelManager::GetPlayModeState() {
		return _state ? _state->playState : PlayModeState::Editing;
	}

	void PanelManager::GetOpenPanels(Vector<Handle<EditorPanel>>& out) {
		if (!_state) {
			return;
		}
		for (const PanelRecord& record : _state->records) {
			out.push_back(record.panel);
		}
	}

	u32 PanelManager::GetOpenPanelCount() {
		return _state ? static_cast<u32>(_state->records.size()) : 0;
	}

	void PanelManager::GetPanelTypes(Vector<const TypeInfo*>& out) {
		Vector<const TypeInfo*> all{GetMemoryResource(MemTag::Editor)};
		TypeRegistry::ForEachType(
			[](const TypeInfo* type, void* user) {
				static_cast<Vector<const TypeInfo*>*>(user)->push_back(type);
			},
			&all);
		for (const TypeInfo* type : all) {
			if (IsOpenablePanelType(type)) {
				out.push_back(type);
			}
		}
	}

	str PanelManager::GetPanelTypeName(const TypeInfo* type) {
		if (type) {
			if (const AttributeInfo* attribute = FindTypeAttribute(type, "EditorPanelName"_hash)) {
				if (attribute->value.Is<str>()) {
					return attribute->value.As<str>();
				}
			}
		}
		return "Panel";
	}

	str PanelManager::GetPanelTypeCategory(const TypeInfo* type) {
		if (type) {
			if (const AttributeInfo* attribute = FindTypeAttribute(type, "EditorPanelCategory"_hash)) {
				if (attribute->value.Is<str>()) {
					return attribute->value.As<str>();
				}
			}
		}
		return {};
	}

	namespace {

		bool BringForward(StringHash dockId, bool takeKeyboard) {
			Vector<Handle<Window>> windows{GetMemoryResource(MemTag::Editor)};
			UISystem::GetDockWindows(windows);
			for (Handle<Window> window : windows) {
				if (UI::DockManager* dock = UISystem::GetDockManager(window)) {
					if (dock->ActivatePanel(dockId)) {
						if (takeKeyboard) {
							dock->SetFocusedPanel(dockId);
						}
						return true;
					}
				}
			}
			return false;
		}

	} // anonymous namespace

	bool PanelManager::RevealPanel(Handle<EditorPanel> handle) {
		EditorPanel* panel = handle.Get();
		if (!panel) {
			return false;
		}
		BringForward(panel->GetDockId(), false);
		return true;
	}

	bool PanelManager::FocusPanel(Handle<EditorPanel> handle) {
		EditorPanel* panel = handle.Get();
		if (!panel) {
			return false;
		}
		BringForward(panel->GetDockId(), true);
		return true;
	}

	bool PanelManager::FocusPanelOfType(StringHash typeName) {
		const TypeInfo* type = TypeRegistry::FindType(typeName);
		if (!type) {
			return false;
		}
		Vector<Handle<EditorPanel>> open{GetMemoryResource(MemTag::Editor)};
		GetOpenPanels(open);
		for (Handle<EditorPanel> handle : open) {
			EditorPanel* panel = handle.Get();
			if (!panel || panel->GetType() != type) {
				continue;
			}
			return FocusPanel(handle);
		}
		return OpenPanel(type).IsValid();
	}

	StringHash PanelManager::MakeDockId(const TypeInfo* type, u32 serial) {
		if (!type) {
			return {};
		}
		constexpr u64 PRIME = 0x00000100000001b3ull;
		u64 hash = type->name.GetValue();
		hash ^= static_cast<u64>('#');
		hash *= PRIME;
		for (u32 shift = 0; shift < 32; shift += 8) {
			hash ^= (static_cast<u64>(serial) >> shift) & 0xFFu;
			hash *= PRIME;
		}
		return StringHash(hash);
	}

	namespace {

		StringHash HoldIdFor(StringHash dockId) {
			return StringHash(std::format("panel-reload-hold/{:x}", dockId.GetValue()));
		}

		void CaptureDockSlot(StringHash dockId, std::string_view title, CapturedPanel& out) {
			if (!UISystem::IsInitialized()) {
				return;
			}
			Vector<Handle<Window>> windows{GetMemoryResource(MemTag::Editor)};
			UISystem::GetDockWindows(windows);
			for (Handle<Window> window : windows) {
				UI::DockManager* dock = UISystem::GetDockManager(window);
				if (!dock) {
					continue;
				}
				u32 node = dock->FindNodeOfPanel(dockId);
				if (node == UI::INVALID_DOCK_NODE) {
					continue;
				}
				out.window = window;
				out.hadDockSlot = true;
				out.activePanel = dock->GetActivePanel(node);
				out.holdId = HoldIdFor(dockId);
				UISystem::RegisterPanel(out.holdId, title, [](UI::Context&, UI::ElementHandle) {});
				dock->DockPanel(out.holdId, node, UI::DockZone::Center);
				return;
			}
		}

		void ReleaseHold(const CapturedPanel& capture) {
			if (capture.holdId.GetValue() != 0 && UISystem::IsInitialized()) {
				UISystem::UnregisterPanel(capture.holdId);
			}
		}

		void RedockRestoredPanel(const CapturedPanel& capture, StringHash dockId) {
			if (!capture.hadDockSlot || !UISystem::IsInitialized()) {
				return;
			}
			UI::DockManager* dock = UISystem::GetDockManager(capture.window);
			if (dock) {
				dock->DockPanel(dockId, dock->FindNodeOfPanel(capture.holdId), UI::DockZone::Center);
				const StringHash forward = capture.activePanel == capture.dockId ? dockId : capture.activePanel;
				if (forward.GetValue() != 0) {
					dock->ActivatePanel(forward);
				}
			}
			ReleaseHold(capture);
		}

	} // anonymous namespace

	u32 PanelManager::CaptureModulePanels(const void* moduleBase) {
		if (!_state || !moduleBase) {
			return 0;
		}
		if (!_state->captured.empty()) {
			SLT_LOG_WARN("Editor", "module panel capture overwritten ({} panel(s) never restored)", _state->captured.size());
			for (const CapturedPanel& stale : _state->captured) {
				ReleaseHold(stale);
			}
			_state->captured.clear();
		}

		Vector<PanelRecord> survivors{GetMemoryResource(MemTag::Editor)};
		Vector<PanelRecord> doomed{GetMemoryResource(MemTag::Editor)};
		for (const PanelRecord& record : _state->records) {
			EditorPanel* panel = record.panel.Get();
			const TypeInfo* type = panel ? panel->GetType() : nullptr;
			if (!panel || !type || type->ownerModule != moduleBase) {
				survivors.push_back(record);
				continue;
			}
			CapturedPanel capture;
			capture.typeName = record.typeName;
			capture.serial = record.serial;
			capture.dockId = record.dockId;
			capture.stateJSON = TextSerializer::Serialize(panel, type);
			CaptureDockSlot(record.dockId, panel->GetTitle(), capture);
			_state->captured.push_back(std::move(capture));
			doomed.push_back(record);
		}
		_state->records.swap(survivors);

		for (PanelRecord& record : doomed) {
			CloseInternal(record, true);
		}
		if (!doomed.empty()) {
			SLT_LOG_INFO("Editor", "captured {} module panel(s) for the reload", doomed.size());
		}
		return static_cast<u32>(doomed.size());
	}

	u32 PanelManager::RestoreModulePanels() {
		if (!_state || _state->captured.empty()) {
			return 0;
		}
		Vector<CapturedPanel> pending{GetMemoryResource(MemTag::Editor)};
		pending.swap(_state->captured);

		u32 restored = 0;
		for (const CapturedPanel& capture : pending) {
			const TypeInfo* type = TypeRegistry::FindType(capture.typeName);
			if (!IsOpenablePanelType(type)) {
				SLT_LOG_WARN("Editor", "panel type {:#x} didn't survive the reload (instance gone)",
							 capture.typeName.GetValue());
				ReleaseHold(capture);
				continue;
			}
			u32 serial = capture.serial;
			for (const PanelRecord& record : _state->records) {
				if (record.typeName == capture.typeName && record.serial == serial) {
					serial = LowestFreeSerial(capture.typeName);
					break;
				}
			}
			Handle<EditorPanel> panel = OpenInternal(type, serial, capture.stateJSON, {}, false);
			if (!panel.IsValid()) {
				ReleaseHold(capture);
				continue;
			}
			RedockRestoredPanel(capture, panel.Get()->GetDockId());
			++restored;
		}
		SLT_LOG_INFO("Editor", "restored {} of {} module panel(s)", restored, pending.size());
		return restored;
	}

	u32 PanelManager::GetCapturedPanelCount() {
		return _state ? static_cast<u32>(_state->captured.size()) : 0;
	}

	void PanelManager::TickAll(f32 deltaSeconds) {
		if (!_state) {
			return;
		}
		for (Object* raw : _state->pendingDestroy) {
			TypeRegistry::DestroyObjectInstance(raw);
		}
		_state->pendingDestroy.clear();

		Vector<PanelManagerState::PendingFloat> floats{GetMemoryResource(MemTag::Editor)};
		floats.swap(_state->pendingFloats);
		for (const PanelManagerState::PendingFloat& entry : floats) {
			PanelRecord* record = FindRecordByDockId(entry.dockId);
			if (!record) {
				continue;
			}
			Handle<Window> host = _state->defaultWindow;
			Window* main = host.Get();
			const f32 scale = main ? main->GetContentScale() : 1.0f;
			const Vec2I client{static_cast<i32>(std::lround(entry.size.x * scale)),
							   static_cast<i32>(std::lround(entry.size.y * scale))};
			Vec2I position{0, 0};
			if (main) {
				const Vec2I origin = main->GetPosition();
				const Vec2I size = main->GetClientSize();
				position = {origin.x + (size.x - client.x) / 2, origin.y + (size.y - client.y) / 2};
			}
			Handle<Window> window = UISystem::CreateFloatingDockWindow(position, client);
			if (UI::DockManager* dock = UISystem::GetDockManager(window.IsValid() ? window : host)) {
				dock->DockPanel(entry.dockId, dock->GetRootNode(), UI::DockZone::Center);
			}
			FocusPanel(record->panel);
		}

		Vector<Handle<EditorPanel>> open{GetMemoryResource(MemTag::Editor)};
		for (const PanelRecord& record : _state->records) {
			open.push_back(record.panel);
		}
		for (Handle<EditorPanel>& handle : open) {
			if (EditorPanel* panel = handle.Get()) {
				for (EditorListView* list : panel->_lists) {
					list->Tick(deltaSeconds);
				}
				panel->OnTick(deltaSeconds);
			}
		}

		for (PanelRecord& record : _state->records) {
			EditorPanel* panel = record.panel.Get();
			if (!panel) {
				continue;
			}
			str title = panel->GetTitle();
			if (panel->HasUnsavedChanges()) {
				title += "*";
			}
			if (title != record.shownTitle) {
				record.shownTitle = title;
				if (UISystem::IsInitialized()) {
					UISystem::SetPanelTitle(record.dockId, title);
				}
			}
		}

		std::erase_if(_state->records, [](const PanelRecord& record) {
			if (!record.panel.IsValid()) {
				SLT_LOG_WARN("Editor", "open panel instance vanished without ClosePanel (dock id {:#x})", record.dockId.GetValue());
				return true;
			}
			return false;
		});

		UpdateFocusedPanel();
	}

	void PanelManager::SubmitViews(f32 deltaSeconds) {
		if (!_state) {
			return;
		}
		Vector<Handle<EditorPanel>> open{GetMemoryResource(MemTag::Editor)};
		for (const PanelRecord& record : _state->records) {
			open.push_back(record.panel);
		}
		for (Handle<EditorPanel>& handle : open) {
			if (EditorPanel* panel = handle.Get()) {
				panel->OnSubmitView(deltaSeconds);
			}
		}
	}

} // namespace Shalltear
