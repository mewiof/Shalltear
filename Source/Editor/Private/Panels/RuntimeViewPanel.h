#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorPanel.h"

#include "ViewportHost.h"

namespace Shalltear {

	class RuntimeViewPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(RuntimeViewPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Runtime View")
		SLT_END_REFLECT()

		EditorIcons::Icon GetTabIcon() const override;

		bool WantsPlayModeTint() const override { return false; }

		RuntimeViewPanel() = default;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
		void OnTick(f32 deltaSeconds) override;
		void OnSubmitView(f32 deltaSeconds) override;
		void OnClose() override;

		ViewTargetId GetViewTarget() const { return _viewport.GetTarget(); }

		bool HasInputFocus() const;

		void RequestViewportFocus() { _focusPending = true; }

		bool HasGamePointer() const { return _gamePointerArmed; }

		bool HasGamePointerAim() const { return _gamePointerAimed; }
		void AdvanceGamePointer();
		void DisarmGamePointer() { _gamePointerArmed = false; }
	private:
		void HonorFocusRequest(UI::Context& context);
		void SyncCameraLabel(UI::Context& context);
		void BuildToggles(UI::Context& context);
		void RefreshToggles(UI::Context& context);
		void SyncScreen(UI::Context& context);
		void ForwardInputToScreen(UI::Context& context, const UI::Rect& rect);
		void ReleaseScreenPointer();

		ViewportHost _viewport;
		UI::ElementHandle _statusLabel;
		UI::ElementHandle _toggleStrip;
		u64 _toggleVersion = 0;
		Vector<u8> _toggleStates{GetMemoryResource(MemTag::Editor)};
		UI::ElementHandle _screenLayer;
		Vector<Handle<RenderPass>> _passRows{GetMemoryResource(MemTag::Editor)};
		bool _labelSaysCamera = false;
		bool _focusPending = false;
		bool _pointerInScreen = false;
		bool _pressInScreen = false;
		MouseButton _pressButton = MouseButton::Left;
		bool _gamePointerArmed = false;
		bool _gamePointerAimed = false;
	};

} // namespace Shalltear
