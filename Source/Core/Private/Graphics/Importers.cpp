#include "Graphics/Importers.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Graphics/GraphicsProfile.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/StyleSheetAsset.h"
#include "Shalltear/Core/UI/UIDocument.h"
#include "Shalltear/Core/UI/UIPanelSettings.h"
#include "Shalltear/Core/UI/VectorAsset.h"

namespace Shalltear::GraphicsImporters {

	namespace {

		ShaderAssetImporter& GetShaderImporter() {
			static ShaderAssetImporter importer;
			return importer;
		}
		TextureAssetImporter& GetTextureImporter() {
			static TextureAssetImporter importer;
			return importer;
		}
		MaterialAssetImporter& GetMaterialImporter() {
			static MaterialAssetImporter importer;
			return importer;
		}
		GraphicsProfileImporter& GetGraphicsProfileImporter() {
			static GraphicsProfileImporter importer;
			return importer;
		}
		RenderPassImporter& GetRenderPassImporter() {
			static RenderPassImporter importer;
			return importer;
		}
		FontAssetImporter& GetFontImporter() {
			static FontAssetImporter importer;
			return importer;
		}
		UIDocumentImporter& GetUIDocumentImporter() {
			static UIDocumentImporter importer;
			return importer;
		}
		StyleSheetImporter& GetStyleSheetImporter() {
			static StyleSheetImporter importer;
			return importer;
		}
		UIPanelSettingsImporter& GetUIPanelSettingsImporter() {
			static UIPanelSettingsImporter importer;
			return importer;
		}
		VectorAssetImporter& GetVectorImporter() {
			static VectorAssetImporter importer;
			return importer;
		}

	} // anonymous namespace

	void RegisterAll() {
		AssetImporterRegistry::Register(StringHash("ShaderAsset"), &GetShaderImporter(), {".hlsl"});
		AssetImporterRegistry::Register(StringHash("TextureAsset"), &GetTextureImporter(), {".png", ".jpg", ".jpeg", ".tga", ".hdr"});
		AssetImporterRegistry::Register(StringHash("MaterialAsset"), &GetMaterialImporter(), {".mat"});
		AssetImporterRegistry::Register(StringHash("GraphicsProfile"), &GetGraphicsProfileImporter(), {".slprofile"});
		AssetImporterRegistry::Register(StringHash("RenderPass"), &GetRenderPassImporter(), {".slpass"});
		AssetImporterRegistry::Register(StringHash("FontAsset"), &GetFontImporter(), {".ttf", ".otf"});
		AssetImporterRegistry::Register(StringHash("UIDocumentAsset"), &GetUIDocumentImporter(), {".slxml"});
		AssetImporterRegistry::Register(StringHash("StyleSheetAsset"), &GetStyleSheetImporter(), {".slss"});
		AssetImporterRegistry::Register(StringHash("UIPanelSettings"), &GetUIPanelSettingsImporter(), {".slpanel"});
		AssetImporterRegistry::Register(StringHash("VectorAsset"), &GetVectorImporter(), {".svg"});
	}

	void UnregisterAll() {
		AssetImporterRegistry::Unregister(&GetShaderImporter());
		AssetImporterRegistry::Unregister(&GetTextureImporter());
		AssetImporterRegistry::Unregister(&GetMaterialImporter());
		AssetImporterRegistry::Unregister(&GetGraphicsProfileImporter());
		AssetImporterRegistry::Unregister(&GetRenderPassImporter());
		AssetImporterRegistry::Unregister(&GetFontImporter());
		AssetImporterRegistry::Unregister(&GetUIDocumentImporter());
		AssetImporterRegistry::Unregister(&GetStyleSheetImporter());
		AssetImporterRegistry::Unregister(&GetUIPanelSettingsImporter());
		AssetImporterRegistry::Unregister(&GetVectorImporter());
	}

} // namespace Shalltear::GraphicsImporters
