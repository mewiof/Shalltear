#include "ImportPipeline.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/StringHash.h"

#include "ModelImport.h"
#include "PrimitiveMeshImporter.h"
#include "TextureCompression.h"

#if defined(SLT_WITH_EDITOR)
#include "ImportPipelineEditor.h"
#endif

namespace Shalltear {

	namespace {

		ModelImporter& GetModelImporter() {
			static ModelImporter importer;
			return importer;
		}
		MeshDescriptorImporter& GetMeshDescriptorImporter() {
			static MeshDescriptorImporter importer;
			return importer;
		}
		PrimitiveMeshImporter& GetPrimitiveImporter() {
			static PrimitiveMeshImporter importer;
			return importer;
		}

	} // anonymous namespace

	void RegisterImportPipeline(const void* owner) {
		AssetImporterRegistry::Register(StringHash("MeshAsset"), &GetPrimitiveImporter(), {".primitive"}, owner);

		for (std::string_view extension : ModelExtensions()) {
			AssetImporterRegistry::RegisterExtension(extension, &GetModelImporter(), owner);
		}

		AssetImporterRegistry::RegisterExtension(".slmesh", &GetMeshDescriptorImporter(), owner);

		RegisterTextureCodecs(owner);
	}

	void ShutdownImportPipeline() {
#if defined(SLT_WITH_EDITOR)
		ImportPipelineEditor::ShutdownModelRigSection();
#endif
		ShutdownTextureCodecs();
	}

} // namespace Shalltear

SLT_IMPLEMENT_PACKAGE_WITH_SHUTDOWN(ImportPipeline, &Shalltear::RegisterImportPipeline, &Shalltear::ShutdownImportPipeline)
