#pragma once

#include "GraphWindowParts.h"

#include "Shalltear/Core/UI/Context.h"

namespace Shalltear {

	struct GraphDocument;

	namespace VisualEffectsEditor::EffectAssetSettings {

		void Build(UI::Context& context, UI::ElementHandle parent, const GraphDocument& document, const GraphWindowParts::Editor& editor);

	} // namespace VisualEffectsEditor::EffectAssetSettings

} // namespace Shalltear
