#include "VisualEffectsEditor.h"

#include "EffectGraphBuild.h"
#include "EffectGraphEditOps.h"
#include "EffectSubgraphImporter.h"
#include "GraphShading.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Editor/GraphDocument.h"

#include "Shalltear/VisualEffects/VisualEffectAsset.h"

#include <cstring>

namespace Shalltear::VisualEffectsEditor {

	namespace {

		class EffectGraphImporter : public IAssetImporter {
		public:
			StringHash GetAssetType() const override { return VisualEffectAsset::GetReflectType()->name; }

			Asset* Import(const Path& sourcePath) override { return ImportWithSettings(sourcePath, Path(), JSONValue{}); }

			// bump when graph output changes
			u32 GetImporterVersion() const override {
				const u64 targets = GraphShadingTargets::Fingerprint();
				return 32u ^ static_cast<u32>(targets ^ (targets >> 32));
			}

			Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue&) override {
				const str graph = virtualPath.ToString().empty() ? sourcePath.ToString() : virtualPath.ToString();
				const str text = FileSystem::ReadTextFile(sourcePath, MemTag::Editor);
				GraphDocument document;
				str error;
				if (text.empty() || !GraphDocumentOps::Read(text, document, error)) {
					SLT_LOG_ERROR("VisualEffects", "'{}' isn't an effect graph document: {}", graph, text.empty() ? str("it is empty or unreadable") : error);
					return nullptr;
				}
				const str directory = GeneratedDirectory(sourcePath, virtualPath);
				if (directory.empty()) {
					SLT_LOG_ERROR("VisualEffects", "'{}' can't import with no project open (its kernels are generated under the project's Library)", graph);
					return nullptr;
				}
				EffectBuild build;
				if (!Prepare(document, graph, directory, build) || !WriteShaders(build, graph)) {
					return nullptr;
				}
				Vector<ShaderAsset*> shaders{GetMemoryResource(MemTag::Asset)};
				for (const EffectShaderText& generated : build.shaders) {
					ShaderAsset* shader = New<ShaderAsset>(MemTag::Asset);
					if (!shader->Import(Path(generated.file))) {
						SLT_LOG_ERROR("VisualEffects", "'{}': the shader generated from it didn't compile ('{}')", graph, generated.file);
						Delete(shader);
						for (ShaderAsset* made : shaders) {
							Delete(made);
						}
						return nullptr;
					}
					shaders.push_back(shader);
				}
				return Assemble(build, graph, shaders);
			}

			bool CanCreateAsset() const override { return true; }
			std::string_view GetCreateMenuLabel() const override { return GetCreateTemplateLabel(0); }
			std::string_view GetCreateDefaultFileName() const override { return "New Effect.sleffect"; }
			bool CreateAssetSource(Buffer& out) override { return CreateTemplateSource(0, out); }
			u32 GetCreateTemplateCount() const override { return EffectGraphEditOps::TemplateCount(); }
			std::string_view GetCreateTemplateLabel(u32 index) const override { return EffectGraphEditOps::TemplateLabel(index); }
			std::string_view GetCreateTemplateFileName(u32 index) const override {
				return index < EffectGraphEditOps::TemplateCount() ? GetCreateDefaultFileName() : std::string_view{};
			}
			bool CreateTemplateSource(u32 index, Buffer& out) override {
				const str text = EffectGraphEditOps::TemplateDocument(index);
				if (text.empty()) {
					return false;
				}
				out = Buffer(text.size(), MemTag::Asset);
				std::memcpy(out.data, text.data(), text.size());
				return true;
			}
		};

		EffectGraphImporter& GetImporter() {
			static EffectGraphImporter importer;
			return importer;
		}

	} // anonymous namespace

	void RegisterImporter(const void* owner) {
		AssetImporterRegistry::Register(GetImporter().GetAssetType(), &GetImporter(), {GRAPH_EFFECT_EXTENSION}, owner);
		EffectSubgraphImporters::Register(owner);
		RegisterVectorFieldImporter(owner);
	}

} // namespace Shalltear::VisualEffectsEditor
