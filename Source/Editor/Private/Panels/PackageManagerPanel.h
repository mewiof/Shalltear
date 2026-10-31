#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorListView.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/PackageOps.h"

#include <span>

namespace Shalltear {

	class PackageManagerPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(PackageManagerPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Package Manager")
		SLT_EDITOR_PANEL_CATEGORY("Configuration")
		SLT_REFLECT_FIELD(selectedPackage)
		SLT_END_REFLECT()

		EditorIcons::Icon GetTabIcon() const override;

		PackageManagerPanel() = default;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
		void OnTick(f32 deltaSeconds) override;
		void OnOpen() override;

		void OnEnabledSetChanged(const SettingChangedEvent& event);

		str selectedPackage;
	private:
		void Refresh();
		const PackageOps::PackageRow* Selected() const;

		str DisplayNameOf(std::string_view name) const;
		str JoinDisplayNames(std::span<const str> names) const;

		void RebuildList(UI::Context& context);
		void RebuildDetail(UI::Context& context);
		void RefreshFooter(UI::Context& context);

		void BuildEmptyState(UI::Context& context, UI::ElementHandle parent);
		void BuildGroupHeader(UI::Context& context, UI::ElementHandle host, std::string_view title);
		void DecorateRow(UI::Context& context, const PackageOps::PackageRow& row,
						 const UI::Widgets::TreeRowParts& parts);

		static u64 RowIdOf(std::string_view name);
		str NameOfRow(u64 id) const;
		void TakeSelectionFromList();
		void PushSelectionToList();

		void Toggle(UI::Context& context, const str& name, bool enable);

		Vector<PackageOps::PackageManifest> _catalogue{GetMemoryResource(MemTag::Editor)};
		Vector<PackageOps::ResolvedPackage> _resolution{GetMemoryResource(MemTag::Editor)};
		Vector<PackageOps::PackageRow> _rows{GetMemoryResource(MemTag::Editor)};
		Vector<str> _diagnostics{GetMemoryResource(MemTag::Editor)};

		bool _dirty = true;
		bool _detailDirty = false;

		EditorListView _list;

		UI::ElementHandle _detailViewport;
		UI::ElementHandle _diagnosticStrip;
		UI::ElementHandle _footerLabel;

		str _lastFooter;
	};

} // namespace Shalltear
