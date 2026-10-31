#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/DragDrop.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <functional>

namespace Shalltear {

	struct EditorInsertStripDesc {
		std::function<bool(const UI::DragPayload&)> onDragOver;
		std::function<void(const UI::DragPayload&, Vec2)> onDrop;
	};

	class SHALLTEAR_EDITOR_API EditorInsertStrips {
	public:
		void BeginBuild(const UI::Context& context);

		void PendStrip(EditorInsertStripDesc desc);
		void PendInFlow(std::function<void(UI::Context&, UI::ElementHandle host)> build);

		[[nodiscard]] std::function<void(UI::Context&, UI::ElementHandle host)> Take();
		void DrainInto(UI::Context& context, UI::ElementHandle host);

		void SyncHits(UI::Context& context);

		[[nodiscard]] static f32 HeightPx();
	private:
		struct Pending {
			bool overlay = false;
			std::function<void(UI::Context&, UI::ElementHandle host, u32 above)> build;
		};
		static void Drain(std::span<const Pending> pending, UI::Context& context, UI::ElementHandle host);

		Vector<Pending> _pending{GetMemoryResource(MemTag::Editor)};
		Vector<Pending> _tail{GetMemoryResource(MemTag::Editor)};
		Vector<UI::ElementHandle> _built{GetMemoryResource(MemTag::Editor)};
		bool _live = false;
	};

} // namespace Shalltear
