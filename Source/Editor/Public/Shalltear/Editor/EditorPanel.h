#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Element.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {

	namespace UI {
		class Context;
	}

	class EditorListView;

	namespace EditorIcons {
		struct Icon;
	}

	enum class PanelCommand : u8 {
		Delete,
		Duplicate,
		Rename,
		SelectAll,

		Count
	};

	class SHALLTEAR_EDITOR_API EditorPanel : public Object {
	public:
		SLT_REFLECT_CLASS(EditorPanel)
		SLT_REFLECT_PARENT(Object)
		SLT_END_REFLECT()

		EditorPanel() = default;

		virtual str GetTitleBase() const;

		virtual EditorIcons::Icon GetTabIcon() const;

		// must not stash state in elements
		void BuildContent(UI::Context& context, UI::ElementHandle root);

		virtual void OnTick(f32 deltaSeconds) { (void)deltaSeconds; }

		virtual bool WantsPlayModeTint() const { return true; }

		virtual void OnSubmitView(f32 deltaSeconds) { (void)deltaSeconds; }

		virtual UISurfaceId GetPresentedSurface() const { return INVALID_UI_SURFACE; }

		virtual void OnOpen() {}
		virtual void OnClose() {}
		virtual void OnSelectionChanged() {}

		virtual bool HasUnsavedChanges() const { return false; }
		virtual str GetSaveChangesMessage() const;
		virtual void SaveChanges() {}
		virtual void DiscardChanges() {}

		virtual bool OnCommand(PanelCommand command);

		void FocusList(EditorListView& list);

		u32 GetInstanceSerial() const { return _instanceSerial; }
		StringHash GetDockId() const { return _dockId; }
		str GetTitle() const;
	protected:
		virtual void OnBuildContent(UI::Context& context, UI::ElementHandle root) = 0;

		UI::Context* GetPanelContext() const;
	private:
		friend class PanelManager;
		friend class EditorListView;

		void RegisterList(EditorListView* list);
		void UnregisterList(EditorListView* list);

		u32 _instanceSerial = 0;
		StringHash _dockId{};
		UI::Context* _lastBuildContext = nullptr;
		Vector<EditorListView*> _lists{GetMemoryResource(MemTag::Editor)};
	};

} // namespace Shalltear

#define SLT_EDITOR_PANEL_NAME(NameLiteral) \
	builder.AddAttribute(::Shalltear::StringHash("EditorPanelName"), ::Shalltear::Variant(::Shalltear::str(NameLiteral)));

#define SLT_EDITOR_PANEL_CATEGORY(NameLiteral) \
	builder.AddAttribute(::Shalltear::StringHash("EditorPanelCategory"), ::Shalltear::Variant(::Shalltear::str(NameLiteral)));

#define SLT_EDITOR_PANEL_FLOATING_SIZE(Width, Height) \
	builder.AddAttribute(::Shalltear::StringHash("EditorPanelFloatingSize"), ::Shalltear::Variant(::Shalltear::Vec2(Width, Height)));
