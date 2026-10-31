#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorConsole.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorVirtualRows.h"

namespace Shalltear {

	class ConsolePanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(ConsolePanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Console")
		SLT_REFLECT_FIELD(showTrace)
		SLT_REFLECT_FIELD(showMessages)
		SLT_REFLECT_FIELD(showWarnings)
		SLT_REFLECT_FIELD(showErrors)
		SLT_REFLECT_FIELD(textFilter)
		SLT_REFLECT_FIELD(autoscroll, SLT_TRANSIENT)
		SLT_END_REFLECT()

		EditorIcons::Icon GetTabIcon() const override;

		ConsolePanel() = default;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
		void OnTick(f32 deltaSeconds) override;

		bool showTrace = true;
		bool showMessages = true;
		bool showWarnings = true;
		bool showErrors = true;
		str textFilter;
		bool autoscroll = true;
	private:
		enum class LogKind : u8 {
			Trace,
			Messages,
			Warnings,
			Errors,
			Count
		};
		static constexpr u32 KIND_COUNT = static_cast<u32>(LogKind::Count);
		static LogKind KindOf(LogLevel level);
		bool& ShownFlag(LogKind kind);
		bool IsShown(LogKind kind) const;
		void ToggleKind(LogKind kind);

		bool PassesFilter(const ConsoleEntry& entry) const;
		bool DrainStream();
		void RefreshModel(UI::Context& context, bool structural);
		void ResetWindow();
		UI::ElementHandle BuildRowBlock(UI::Context& context, u32 index);
		void ShowExpansion(UI::Context& context, UI::ElementHandle block, const ConsoleEntry& entry);
		bool IsExpanded(u64 sequence) const;
		void SetExpanded(u64 sequence, bool expanded);
		bool FindVisible(u64 sequence, u32& index) const;
		void RefreshCounts(UI::Context& context);
		void RefreshEmptyState(UI::Context& context);

		void FollowUserScroll(UI::Context& context);
		void PinTail(UI::Context& context);
		void FollowTailThisFrame();
		void SetTail(bool on);

		Vector<ConsoleEntry> _entries{GetMemoryResource(MemTag::Editor)};
		u64 _lastSequence = 0;
		Vector<u64> _expanded{GetMemoryResource(MemTag::Editor)};
		bool _rowsDirty = false;

		Vector<u32> _visible{GetMemoryResource(MemTag::Editor)};
		u64 _visibleTail = 0;
		EditorVirtualRows _window;
		u64 _realizedThrough = 0;

		u32 _counts[KIND_COUNT] = {};

		UI::ElementHandle _viewport;
		UI::ElementHandle _scrollRoot;
		UI::ElementHandle _emptyState;
		UI::ElementHandle _droppedLabel;
		UI::Widgets::CheckboxParts _autoscrollBox;
		struct KindSwitch {
			UI::ElementHandle button;
			UI::ElementHandle icon;
			UI::ElementHandle count;
			u32 drawnCount = 0xFFFFFFFFu;
			bool drawnShown = false;
		};
		KindSwitch _switches[KIND_COUNT];

		f32 _pinnedScrollY = -1.0f;
		f32 _lastScrollY = -1.0f;
		bool _wasAtEnd = true;
		bool _tailHeldOff = false;
	};

} // namespace Shalltear
