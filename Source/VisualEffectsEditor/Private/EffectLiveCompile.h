#pragma once

#include "GraphCompiler.h"
#include "GraphEffect.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct GraphDocument;

	namespace VisualEffectsEditor::EffectLiveCompiles {

		struct Status {
			bool compiling = false;
			f64 seconds = 0.0;
			bool failed = false;
			u32 adopted = 0;
		};

		bool Request(const Guid& asset, const GraphDocument& document, Vector<GraphDiagnostic>& diagnostics, const GraphEffectCompileOptions& options = {});
		void Seed(const Guid& asset, const GraphDocument& document);
		void Forget(const Guid& asset);
		void Tick();
		Status GetStatus(const Guid& asset);
		void Terminate();

	} // namespace VisualEffectsEditor::EffectLiveCompiles

} // namespace Shalltear
