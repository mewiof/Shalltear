#pragma once

#include "EffectAsset.h"
#include "EffectDescription.h"

#include "GraphCompiler.h"
#include "GraphEffect.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	class ShaderAsset;
	class VisualEffectAsset;
	struct GraphDocument;

	namespace VisualEffectsEditor {

		struct EffectShaderText {
			str file;
			str text;
		};

		struct EffectBuild {
			GraphEffect effect;
			Vector<EffectShaderText> shaders{GetMemoryResource(MemTag::Editor)};
			Vector<VisualEffectsImpl::EffectDependency> dependencies{GetMemoryResource(MemTag::Editor)};
		};

		str GeneratedDirectory(const Path& sourcePath, const Path& virtualPath, std::string_view leaf = {});

		bool Prepare(const GraphDocument& document, std::string_view graph, const str& directory, EffectBuild& out,
					 Vector<GraphDiagnostic>* diagnostics = nullptr, const GraphEffectCompileOptions& options = {});
		bool WriteShaders(const EffectBuild& build, std::string_view graph);
		bool Describe(const GraphEffect& effect, std::string_view graph, VisualEffectsImpl::EffectDescription& out);
		u64 Fingerprint(const GraphEffect& effect, std::string_view graph);
		VisualEffectAsset* Assemble(const EffectBuild& build, std::string_view graph, Vector<ShaderAsset*>& shaders);

	} // namespace VisualEffectsEditor

} // namespace Shalltear
