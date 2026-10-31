#include "Shalltear/Editor/EditorLayoutOps.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/PanelManager.h"

#include "Shalltear/Core/Config.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <format>

namespace Shalltear::EditorLayoutOps {

	namespace {

		RectI PlacementRect(const WindowPlacement& placement) {
			return RectI(placement.position, placement.clientSize);
		}

		struct Edges {
			i64 left;
			i64 top;
			i64 right;
			i64 bottom;
		};

		Edges EdgesOf(const RectI& rect) {
			return {rect.position.x, rect.position.y,
					static_cast<i64>(rect.position.x) + rect.size.x,
					static_cast<i64>(rect.position.y) + rect.size.y};
		}

		struct Extent {
			i64 width;
			i64 height;
		};

		Extent OverlapExtent(const RectI& a, const RectI& b) {
			Edges lhs = EdgesOf(a);
			Edges rhs = EdgesOf(b);
			return {Math::Min(lhs.right, rhs.right) - Math::Max(lhs.left, rhs.left),
					Math::Min(lhs.bottom, rhs.bottom) - Math::Max(lhs.top, rhs.top)};
		}

		i64 OverlapArea(const RectI& a, const RectI& b) {
			Extent extent = OverlapExtent(a, b);
			if (extent.width <= 0 || extent.height <= 0) {
				return 0;
			}
			return extent.width * extent.height;
		}

		const MonitorInfo* PickMonitor(const RectI& rect, std::span<const MonitorInfo> monitors) {
			const MonitorInfo* best = nullptr;
			i64 bestArea = 0;
			for (const MonitorInfo& monitor : monitors) {
				i64 area = OverlapArea(rect, monitor.workArea);
				if (area > bestArea) {
					bestArea = area;
					best = &monitor;
				}
			}
			if (best) {
				return best;
			}
			for (const MonitorInfo& monitor : monitors) {
				if (monitor.primary) {
					return &monitor;
				}
			}
			return monitors.empty() ? nullptr : &monitors.front();
		}

		i32 SlideInside(i32 position, i32 size, i32 min, i32 max) {
			return Math::Max(min, Math::Min(max - size, position));
		}

	} // anonymous namespace

	bool IsPlacementVisible(const WindowPlacement& placement, std::span<const MonitorInfo> monitors) {
		RectI rect = PlacementRect(placement);
		for (const MonitorInfo& monitor : monitors) {
			Extent extent = OverlapExtent(rect, monitor.workArea);
			if (extent.width >= MinVisiblePx.x && extent.height >= MinVisiblePx.y) {
				return true;
			}
		}
		return false;
	}

	WindowPlacement ResolvePlacement(const WindowPlacement& saved, std::span<const MonitorInfo> monitors, Vec2I minClientSize) {
		WindowPlacement resolved = saved;

		if (resolved.state == WindowState::Minimized) {
			resolved.state = WindowState::Normal;
		}
		resolved.clientSize = {Math::Max(resolved.clientSize.x, minClientSize.x),
							   Math::Max(resolved.clientSize.y, minClientSize.y)};

		const MonitorInfo* monitor = PickMonitor(PlacementRect(resolved), monitors);
		if (!monitor) {
			return resolved;
		}

		const RectI& work = monitor->workArea;
		resolved.clientSize = {Math::Min(resolved.clientSize.x, Math::Max(work.size.x, minClientSize.x)),
							   Math::Min(resolved.clientSize.y, Math::Max(work.size.y, minClientSize.y))};

		if (IsPlacementVisible(resolved, monitors)) {
			return resolved;
		}

		resolved.position = {SlideInside(resolved.position.x, resolved.clientSize.x, work.Left(), work.Right()),
							 SlideInside(resolved.position.y, resolved.clientSize.y, work.Top(), work.Bottom())};
		return resolved;
	}

	constexpr size_t DEFAULT_NODE_COUNT = 7;

	constexpr f32 MAIN_COLUMN_RATIO = 0.80f;
	constexpr f32 UPPER_ROW_RATIO = 0.75f;
	constexpr f32 HIERARCHY_RATIO = 0.17f;

	void BuildDefaultLayout(Handle<Window> window) {
		auto open = [&](const char* typeName) {
			const TypeInfo* type = TypeRegistry::FindType(StringHash(typeName));
			if (!type) {
				return StringHash{};
			}
			Handle<EditorPanel> panel = PanelManager::OpenPanel(type, window);
			return panel.IsValid() ? panel.Get()->GetDockId() : StringHash{};
		};
		StringHash hierarchy = open("HierarchyPanel");
		StringHash inspector = open("InspectorPanel");
		StringHash console = open("ConsolePanel");
		StringHash assetBrowser = open("AssetBrowserPanel");
		StringHash sceneView = open("SceneViewPanel");
		StringHash runtimeView = open("RuntimeViewPanel");

		UI::DockManager* dock = UISystem::GetDockManager(window);
		if (!dock) {
			return;
		}

		Vector<UI::DockNodeDesc> layout{GetMemoryResource(MemTag::Editor)};
		layout.resize(DEFAULT_NODE_COUNT);
		auto split = [&layout](size_t index, UI::DockSplit orientation, f32 ratio, i32 childA, i32 childB) {
			layout[index].split = orientation;
			layout[index].ratio = ratio;
			layout[index].childA = childA;
			layout[index].childB = childB;
		};
		auto leaf = [&layout](size_t index, std::initializer_list<StringHash> tabs) {
			for (StringHash tab : tabs) {
				if (tab.GetValue() != 0) {
					layout[index].tabs.push_back(tab);
				}
			}
		};

		split(0, UI::DockSplit::Horizontal, MAIN_COLUMN_RATIO, 1, 6);
		split(1, UI::DockSplit::Vertical, UPPER_ROW_RATIO, 2, 5);
		split(2, UI::DockSplit::Horizontal, HIERARCHY_RATIO, 3, 4);
		leaf(3, {hierarchy});
		leaf(4, {sceneView, runtimeView});
		leaf(5, {assetBrowser, console});
		leaf(6, {inspector});

		if (!dock->RestoreLayout({layout.data(), layout.size()})) {
			SLT_LOG_WARN("Editor", "the default layout was rejected by the dock (panels open but unarranged)");
		}
	}

	void ResetLayout(Handle<Window> window) {
		Vector<Handle<EditorPanel>> open{GetMemoryResource(MemTag::Editor)};
		PanelManager::GetOpenPanels(open);
		for (Handle<EditorPanel> panel : open) {
			PanelManager::ClosePanel(panel);
		}
		BuildDefaultLayout(window);
	}

	namespace {

		constexpr i64 LAYOUT_VERSION = 1;

		str EscapeIniValue(std::string_view text) {
			str out;
			out.reserve(text.size());
			for (char c : text) {
				switch (c) {
				case '\\':
					out += "\\\\";
					break;
				case '\n':
					out += "\\n";
					break;
				case '\r':
					out += "\\r";
					break;
				default:
					out += c;
					break;
				}
			}
			return out;
		}

		str UnescapeIniValue(std::string_view text) {
			str out;
			out.reserve(text.size());
			for (size_t i = 0; i < text.size(); ++i) {
				if (text[i] == '\\' && i + 1 < text.size()) {
					char next = text[++i];
					switch (next) {
					case 'n':
						out += '\n';
						break;
					case 'r':
						out += '\r';
						break;
					default:
						out += next;
						break;
					}
				} else {
					out += text[i];
				}
			}
			return out;
		}

		str HashToHex(StringHash hash) {
			return std::format("{:#018x}", hash.GetValue());
		}

		StringHash HexToHash(std::string_view text) {
			if (text.starts_with("0x") || text.starts_with("0X")) {
				text.remove_prefix(2);
			}
			u64 value = 0;
			std::from_chars(text.data(), text.data() + text.size(), value, 16);
			return StringHash(value);
		}

		void WriteDockTree(Config& config, u32 windowIndex, const UI::DockManager& dock) {
			Vector<UI::DockNodeDesc> nodes{GetMemoryResource(MemTag::Editor)};
			dock.SaveLayout(nodes);
			for (size_t i = 0; i < nodes.size(); ++i) {
				const UI::DockNodeDesc& node = nodes[i];
				str section = std::format("Window.{}.Dock.{}", windowIndex, i);
				config.SetI64(section, "split", static_cast<i64>(node.split));
				config.SetF64(section, "ratio", node.ratio);
				str tabs;
				for (StringHash tab : node.tabs) {
					if (!tabs.empty()) {
						tabs += ',';
					}
					tabs += HashToHex(tab);
				}
				config.SetString(section, "tabs", tabs);
				config.SetI64(section, "activeTab", node.activeTab);
				config.SetI64(section, "childA", node.childA);
				config.SetI64(section, "childB", node.childB);
			}
		}

		bool ReadDockTree(const Config& config, u32 windowIndex, Vector<UI::DockNodeDesc>& out) {
			for (u32 i = 0;; ++i) {
				str section = std::format("Window.{}.Dock.{}", windowIndex, i);
				if (!config.HasSection(section)) {
					break;
				}
				UI::DockNodeDesc node;
				node.split = static_cast<UI::DockSplit>(config.GetI64(section, "split", 0));
				node.ratio = static_cast<f32>(config.GetF64(section, "ratio", 0.5));
				str tabs = config.GetString(section, "tabs");
				size_t start = 0;
				while (start < tabs.size()) {
					size_t comma = tabs.find(',', start);
					if (comma == str::npos) {
						comma = tabs.size();
					}
					if (comma > start) {
						node.tabs.push_back(HexToHash(std::string_view(tabs).substr(start, comma - start)));
					}
					start = comma + 1;
				}
				node.activeTab = static_cast<u32>(config.GetI64(section, "activeTab", 0));
				node.childA = static_cast<i32>(config.GetI64(section, "childA", -1));
				node.childB = static_cast<i32>(config.GetI64(section, "childB", -1));
				out.push_back(std::move(node));
			}
			return !out.empty();
		}

		void WriteWindowPlacement(Config& config, u32 windowIndex, const Window& window) {
			str section = std::format("Window.{}", windowIndex);
			WindowPlacement placement = window.GetPlacement();
			config.SetI64(section, "x", placement.position.x);
			config.SetI64(section, "y", placement.position.y);
			config.SetI64(section, "w", placement.clientSize.x);
			config.SetI64(section, "h", placement.clientSize.y);
			config.SetBool(section, "maximized", placement.state == WindowState::Maximized);
		}

		WindowPlacement ReadWindowPlacement(const Config& config, std::string_view section, const WindowPlacement& fallback,
											Vec2I minClientSize = MinRestoredClientSize) {
			WindowPlacement placement;
			placement.position = {static_cast<i32>(config.GetI64(section, "x", fallback.position.x)),
								  static_cast<i32>(config.GetI64(section, "y", fallback.position.y))};
			placement.clientSize = {static_cast<i32>(config.GetI64(section, "w", fallback.clientSize.x)),
									static_cast<i32>(config.GetI64(section, "h", fallback.clientSize.y))};
			placement.state = config.GetBool(section, "maximized") ? WindowState::Maximized : WindowState::Normal;
			return ResolvePlacement(placement, WindowSystem::GetMonitors(), minClientSize);
		}

		struct LayoutStoreState {
			Path directory;
			str active{DefaultLayoutName};
		};
		LayoutStoreState* _store = nullptr;

		LayoutStoreState& Store() {
			if (!_store) {
				_store = New<LayoutStoreState>(MemTag::Editor);
			}
			return *_store;
		}

		bool IsNameCharacter(char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
				   c == ' ' || c == '_' || c == '-';
		}

		constexpr size_t MAX_NAME_LENGTH = 48;

		str SanitizeName(std::string_view name) {
			while (!name.empty() && name.front() == ' ') {
				name.remove_prefix(1);
			}
			while (!name.empty() && name.back() == ' ') {
				name.remove_suffix(1);
			}
			if (name.empty() || name.size() > MAX_NAME_LENGTH) {
				return {};
			}
			for (char c : name) {
				if (!IsNameCharacter(c)) {
					return {};
				}
			}
			return str(name);
		}

		bool SameName(std::string_view a, std::string_view b) {
			return a.size() == b.size() &&
				   std::equal(a.begin(), a.end(), b.begin(), [](char lhs, char rhs) {
					   return std::tolower(static_cast<unsigned char>(lhs)) ==
							  std::tolower(static_cast<unsigned char>(rhs));
				   });
		}

		Path FileFor(std::string_view name) {
			LayoutStoreState& store = Store();
			if (store.directory.ToString().empty()) {
				return {};
			}
			str sanitized = SanitizeName(name);
			if (sanitized.empty()) {
				return {};
			}
			return store.directory / Path(sanitized + ".ini");
		}

	} // anonymous namespace

	str CaptureLayout(Handle<Window> mainWindow) {
		Config config;
		config.SetI64("Editor", "version", LAYOUT_VERSION);
		config.SetString("Editor", "layout", GetActiveLayout());

		Vector<Handle<Window>> windows{GetMemoryResource(MemTag::Editor)};
		windows.push_back(mainWindow);
		Vector<Handle<Window>> dockWindows{GetMemoryResource(MemTag::Editor)};
		UISystem::GetDockWindows(dockWindows);
		for (Handle<Window> window : dockWindows) {
			if (!(window == mainWindow) && UISystem::IsFloatingDockWindow(window)) {
				windows.push_back(window);
			}
		}

		for (size_t i = 0; i < windows.size(); ++i) {
			Window* resolved = windows[i].Get();
			if (!resolved) {
				continue;
			}
			WriteWindowPlacement(config, static_cast<u32>(i), *resolved);
			if (const UI::DockManager* dock = UISystem::GetDockManager(windows[i])) {
				WriteDockTree(config, static_cast<u32>(i), *dock);
			}
		}

		Vector<Handle<EditorPanel>> panels{GetMemoryResource(MemTag::Editor)};
		PanelManager::GetOpenPanels(panels);
		u32 panelIndex = 0;
		for (Handle<EditorPanel> handle : panels) {
			EditorPanel* panel = handle.Get();
			if (!panel) {
				continue;
			}
			const TypeInfo* type = panel->GetType();
			str section = std::format("Panel.{}", panelIndex++);
			config.SetString(section, "type", HashToHex(type->name));
			config.SetI64(section, "serial", panel->GetInstanceSerial());
			config.SetString(section, "state", EscapeIniValue(TextSerializer::Serialize(panel, type)));
		}

		return config.Serialize();
	}

	bool ApplyLayout(Handle<Window> mainWindow, std::string_view document) {
		Config config;
		config.Parse(document);
		if (config.GetI64("Editor", "version", 0) != LAYOUT_VERSION) {
			SLT_LOG_INFO("Editor", "layout version mismatch (arrangement on screen left alone)");
			return false;
		}

		Vector<Handle<EditorPanel>> open{GetMemoryResource(MemTag::Editor)};
		PanelManager::GetOpenPanels(open);
		for (Handle<EditorPanel> panel : open) {
			PanelManager::ClosePanel(panel);
		}

		str name = config.GetString("Editor", "layout");
		if (!name.empty()) {
			Store().active = name;
		}

		if (Window* main = mainWindow.Get()) {
			if (config.HasSection("Window.0")) {
				main->SetPlacement(ReadWindowPlacement(config, "Window.0", main->GetPlacement()));
			}
		}

		for (u32 i = 0;; ++i) {
			str section = std::format("Panel.{}", i);
			if (!config.HasSection(section)) {
				break;
			}
			StringHash typeName = HexToHash(config.GetString(section, "type"));
			const TypeInfo* type = TypeRegistry::FindType(typeName);
			if (!type) {
				SLT_LOG_WARN("Editor", "layout references unknown panel type {}. Dropped", config.GetString(section, "type"));
				continue;
			}
			u32 serial = static_cast<u32>(config.GetI64(section, "serial", 1));
			str state = UnescapeIniValue(config.GetString(section, "state"));
			PanelManager::RestorePanel(type, serial, state);
		}

		Vector<Handle<Window>> windows{GetMemoryResource(MemTag::Editor)};
		windows.push_back(mainWindow);
		for (u32 i = 1;; ++i) {
			str section = std::format("Window.{}", i);
			if (!config.HasSection(section)) {
				break;
			}
			WindowPlacement fallback{Vec2I{100, 100}, Vec2I{480, 360}, WindowState::Normal};
			WindowPlacement placement = ReadWindowPlacement(config, section, fallback, fallback.clientSize);
			Handle<Window> window = UISystem::CreateFloatingDockWindow(placement.position, placement.clientSize);
			if (!window.IsValid()) {
				break;
			}
			if (placement.state == WindowState::Maximized) {
				if (Window* resolved = window.Get()) {
					resolved->SetPlacement(placement);
				}
			}
			windows.push_back(window);
		}

		for (size_t i = 0; i < windows.size(); ++i) {
			Vector<UI::DockNodeDesc> nodes{GetMemoryResource(MemTag::Editor)};
			if (!ReadDockTree(config, static_cast<u32>(i), nodes)) {
				continue;
			}
			if (UI::DockManager* dock = UISystem::GetDockManager(windows[i])) {
				if (!dock->RestoreLayout({nodes.data(), nodes.size()})) {
					SLT_LOG_WARN("Editor", "dock tree for window {} was malformed. Using the default", i);
				}
			}
		}

		SLT_LOG_INFO("Editor", "layout '{}' restored ({} panel(s))", Store().active, PanelManager::GetOpenPanelCount());
		return true;
	}

	void InitializeLayoutStore(const Path& directory) {
		LayoutStoreState& store = Store();
		store.directory = directory;
		if (!directory.ToString().empty()) {
			FileSystem::CreateDirectory(directory);
		}
	}

	void ShutdownLayoutStore() {
		Delete(_store);
		_store = nullptr;
	}

	str GetActiveLayout() {
		return Store().active;
	}

	void GetLayoutNames(Vector<str>& out) {
		out.push_back(str(DefaultLayoutName));

		LayoutStoreState& store = Store();
		if (store.directory.ToString().empty()) {
			return;
		}
		Vector<str> found{GetMemoryResource(MemTag::Editor)};
		std::error_code ec;
		for (std::filesystem::directory_iterator it(std::filesystem::path{store.directory.ToString()}, ec), end;
			 !ec && it != end; it.increment(ec)) {
			std::error_code typeEc;
			if (it->is_directory(typeEc) || typeEc || it->path().extension() != ".ini") {
				continue;
			}
			str name = SanitizeName(it->path().stem().generic_string());
			if (!name.empty() && !SameName(name, DefaultLayoutName)) {
				found.push_back(std::move(name));
			}
		}
		std::sort(found.begin(), found.end(), [](const str& lhs, const str& rhs) {
			return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
												[](char a, char b) {
													return std::tolower(static_cast<unsigned char>(a)) <
														   std::tolower(static_cast<unsigned char>(b));
												});
		});
		for (str& name : found) {
			out.push_back(std::move(name));
		}
	}

	void SaveActiveLayout(Handle<Window> mainWindow) {
		Path file = FileFor(Store().active);
		if (file.ToString().empty()) {
			return;
		}
		FileSystem::WriteTextFile(file, CaptureLayout(mainWindow));
	}

	bool SwitchLayout(Handle<Window> mainWindow, std::string_view name) {
		str target = SanitizeName(name);
		if (target.empty()) {
			return false;
		}
		if (SameName(target, Store().active)) {
			return true;
		}

		SaveActiveLayout(mainWindow);

		Path file = FileFor(target);
		str document;
		if (!file.ToString().empty() && FileSystem::Exists(file)) {
			document = FileSystem::ReadTextFile(file, MemTag::Editor);
		}
		if (!document.empty() && ApplyLayout(mainWindow, document)) {
			Store().active = target;
			return true;
		}

		Store().active = target;
		ResetLayout(mainWindow);
		return true;
	}

	bool SaveLayoutAs(Handle<Window> mainWindow, std::string_view name) {
		str target = SanitizeName(name);
		if (target.empty()) {
			return false;
		}
		Store().active = target;
		Path file = FileFor(target);
		if (file.ToString().empty()) {
			return true;
		}
		return FileSystem::WriteTextFile(file, CaptureLayout(mainWindow));
	}

	bool RenameLayout(std::string_view from, std::string_view to) {
		str source = SanitizeName(from);
		str target = SanitizeName(to);
		if (source.empty() || target.empty() || SameName(source, DefaultLayoutName)) {
			return false;
		}
		if (SameName(source, target)) {
			return true;
		}
		Path sourceFile = FileFor(source);
		Path targetFile = FileFor(target);
		if (sourceFile.ToString().empty() || targetFile.ToString().empty()) {
			return false;
		}
		if (FileSystem::Exists(targetFile) || !FileSystem::Exists(sourceFile)) {
			return false;
		}
		str document = FileSystem::ReadTextFile(sourceFile, MemTag::Editor);
		Config config;
		config.Parse(document);
		config.SetString("Editor", "layout", target);
		document = config.Serialize();
		if (!FileSystem::WriteTextFile(targetFile, document)) {
			return false;
		}
		std::error_code ec;
		std::filesystem::remove(std::filesystem::path{sourceFile.ToString()}, ec);
		if (SameName(Store().active, source)) {
			Store().active = target;
		}
		return true;
	}

	bool DeleteLayout(std::string_view name) {
		str target = SanitizeName(name);
		if (target.empty() || SameName(target, DefaultLayoutName)) {
			return false;
		}
		Path file = FileFor(target);
		if (file.ToString().empty() || !FileSystem::Exists(file)) {
			return false;
		}
		std::error_code ec;
		if (!std::filesystem::remove(std::filesystem::path{file.ToString()}, ec) || ec) {
			return false;
		}
		if (SameName(Store().active, target)) {
			Store().active = str(DefaultLayoutName);
		}
		return true;
	}

	void BringPanelsForward(const TypeInfo* panelType, Vector<StringHash>& outPrevious) {
		if (!panelType) {
			return;
		}
		Vector<Handle<EditorPanel>> panels{GetMemoryResource(MemTag::Editor)};
		PanelManager::GetOpenPanels(panels);
		Vector<Handle<Window>> windows{GetMemoryResource(MemTag::Editor)};
		UISystem::GetDockWindows(windows);

		for (Handle<EditorPanel> handle : panels) {
			EditorPanel* panel = handle.Get();
			if (!panel || panel->GetType() != panelType) {
				continue;
			}
			StringHash id = panel->GetDockId();
			for (Handle<Window> window : windows) {
				UI::DockManager* dock = UISystem::GetDockManager(window);
				if (!dock) {
					continue;
				}
				u32 node = dock->FindNodeOfPanel(id);
				if (node == UI::INVALID_DOCK_NODE) {
					continue;
				}
				StringHash displaced = dock->GetActivePanel(node);
				if (dock->ActivatePanel(id) && displaced.GetValue() != 0 && displaced != id) {
					outPrevious.push_back(displaced);
				}
				break;
			}
		}
	}

	void RestoreForwardPanels(std::span<const StringHash> panels) {
		Vector<Handle<Window>> windows{GetMemoryResource(MemTag::Editor)};
		UISystem::GetDockWindows(windows);
		for (StringHash id : panels) {
			for (Handle<Window> window : windows) {
				if (UI::DockManager* dock = UISystem::GetDockManager(window)) {
					if (dock->ActivatePanel(id)) {
						break;
					}
				}
			}
		}
	}

} // namespace Shalltear::EditorLayoutOps
