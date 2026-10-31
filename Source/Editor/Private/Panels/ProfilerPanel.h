#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorPanel.h"

namespace Shalltear {

	class ProfilerPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(ProfilerPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Profiler")
		SLT_EDITOR_PANEL_CATEGORY("Analysis")
		SLT_REFLECT_FIELD(paused)
		SLT_END_REFLECT()

		EditorIcons::Icon GetTabIcon() const override;

		ProfilerPanel() = default;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
		void OnTick(f32 deltaSeconds) override;

		bool paused = false;
	private:
		void RefreshStats(UI::Context& context);
		void RebuildModules(UI::Context& context);

		static constexpr u32 HISTORY = 240;

		f32 _frameSeconds[HISTORY] = {};
		u32 _head = 0;
		f32 _refreshAccum = 1.0e6f;

		UI::ElementHandle _frameLabel;
		UI::ElementHandle _renderLabel;
		UI::ElementHandle _uiLabel;
		UI::ElementHandle _memoryContainer;
		UI::ElementHandle _zoneContainer;
		struct ModuleHost {
			StringHash id;
			UI::ElementHandle host;
		};
		UI::ElementHandle _moduleContainer;
		Vector<ModuleHost> _moduleHosts{GetMemoryResource(MemTag::Editor)};
		u64 _moduleVersion = 0;
	};

} // namespace Shalltear
