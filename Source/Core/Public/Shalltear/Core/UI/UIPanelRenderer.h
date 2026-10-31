#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UIDocument.h"
#include "Shalltear/Core/UI/UIPanelSettings.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/UITypes.h"

#include <string_view>

namespace Shalltear {

	struct FrameStageArgs;
	namespace UI {
		class Context;
	}

	class SHALLTEAR_CORE_API UIPanelRenderer : public Component {
	public:
		SLT_REFLECT_COMPONENT(UIPanelRenderer, Component)
		SLT_REFLECT_FIELD(_document)
		SLT_REFLECT_FIELD(_panelSettings)
		SLT_REFLECT_FIELD(_sortOrder)
		SLT_END_REFLECT()

		UIPanelRenderer() = default;
		~UIPanelRenderer() override;

		Handle<UIDocumentAsset> GetDocument() const { return _document; }
		void SetDocument(Handle<UIDocumentAsset> document);

		Handle<UIPanelSettings> GetPanelSettings() const { return _panelSettings; }
		void SetPanelSettings(Handle<UIPanelSettings> settings);

		// lower draws first
		f32 GetSortOrder() const { return _sortOrder; }
		void SetSortOrder(f32 order);

		UI::Context* GetContext() const;
		UI::ElementHandle FindElement(std::string_view id) const;
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		void Build();
		void Teardown();
		void Aim();
		void Populate();
		void Reconcile();
		static void ReconcileAll(const FrameStageArgs& args, void* user);

		Handle<UIDocumentAsset> _document;
		Handle<UIPanelSettings> _panelSettings;
		f32 _sortOrder = 0.0f;

		UISurfaceId _surface = INVALID_UI_SURFACE;
		Handle<UIDocumentAsset> _builtDocument;
		Handle<UIPanelSettings> _aimedSettings;
		f32 _aimedSortOrder = 0.0f;
		u64 _builtVersion = 0;

		struct NamedElement {
			StringHash id;
			UI::ElementHandle element;
		};
		Vector<NamedElement> _named{GetMemoryResource(MemTag::UI)};
	};

} // namespace Shalltear
