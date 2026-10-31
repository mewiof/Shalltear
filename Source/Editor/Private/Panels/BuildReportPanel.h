#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/CookOps.h"
#include "Shalltear/Editor/EditorPanel.h"

namespace Shalltear {

	class BuildReportPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(BuildReportPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Build Report")
		SLT_EDITOR_PANEL_CATEGORY("Build")
		SLT_REFLECT_FIELD(selectedProfile)
		SLT_REFLECT_FIELD(sortByBytes)
		SLT_END_REFLECT()

		EditorIcons::Icon GetTabIcon() const override;

		BuildReportPanel() = default;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
		void OnTick(f32 deltaSeconds) override;
		void OnOpen() override;

		str selectedProfile;

		bool sortByBytes = true;

		void Refresh();

		bool HasReport() const { return _loaded; }
		const CookOps::CookReport& GetReport() const { return _report; }
	private:
		i64 ReportStamp() const;
		void RebuildBody(UI::Context& context);
		void BuildSummary(UI::Context& context, UI::ElementHandle parent);
		void BuildAssetList(UI::Context& context, UI::ElementHandle parent);
		void BuildWhyRow(UI::Context& context, UI::ElementHandle parent, const CookOps::CookReportAsset& line);

		str _projectPath;
		Vector<str> _profiles{GetMemoryResource(MemTag::Editor)};
		CookOps::CookReport _report;
		bool _loaded = false;
		str _loadError;

		UI::ElementHandle _bodyViewport;
		bool _dirty = true;

		f64 _pollCountdown = 0.0;
		i64 _reportStamp = 0;

		Vector<u32> _expanded{GetMemoryResource(MemTag::Editor)};
	};

} // namespace Shalltear
