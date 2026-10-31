#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Element.h"

#include <string_view>

namespace Shalltear {

	class FontAsset;

	namespace UI {
		class Context;
	}

	struct UIDocumentAttribute {
		str name;
		str value;
	};

	class UIDocumentAsset;
	class StyleSheetAsset;

	struct UIDocumentSheet {
		str src;

		Handle<StyleSheetAsset> sheet;
	};

	struct UIDocumentTemplate {
		str name;
		str src;

		Handle<UIDocumentAsset> document;
	};

	struct UIDocumentOverride {
		str elementId;

		Vector<UIDocumentAttribute> attributes{GetMemoryResource(MemTag::Asset)};

		str text;
	};

	struct SHALLTEAR_CORE_API UIDocumentNode {
		str tag;
		str id;
		str classes[UI::MAX_ELEMENT_CLASSES];
		u8 classCount = 0;
		str style;
		str text;

		Vector<UIDocumentAttribute> attributes{GetMemoryResource(MemTag::Asset)};

		str templateName;
		Vector<UIDocumentOverride> overrides{GetMemoryResource(MemTag::Asset)};

		Vector<UIDocumentNode> children{GetMemoryResource(MemTag::Asset)};
	};

	struct UIInstantiateDesc {
		Handle<FontAsset> font;
		f32 fontSizePx = 14.0f;
		Color textColor = Color::White();
	};

	class UIDocumentAsset;

	struct UIInstantiateMapping {
		const UIDocumentAsset* document = nullptr;
		const UIDocumentNode* node = nullptr;
		UI::ElementHandle element;
	};

	class SHALLTEAR_CORE_API UIDocumentAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(UIDocumentAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		UIDocumentAsset() = default;
		~UIDocumentAsset() override;

		static constexpr u32 FORMAT_VERSION = 1;

		UIDocumentNode root;

		Vector<UIDocumentSheet> sheets{GetMemoryResource(MemTag::Asset)};

		Vector<UIDocumentTemplate> templates{GetMemoryResource(MemTag::Asset)};

		u32 version = 0;

		[[nodiscard]] u64 GetContentVersion() const;

		bool CanAdoptReimport() const override { return true; }
		void AdoptReimport(Asset& fresh) override;

		bool Import(const Path& sourcePath) override;
		bool Export(const Path& destPath) const override;
		StringHash GetAssetTypeId() const override { return UIDocumentAsset::GetReflectType()->name; }

		bool Parse(std::string_view source, str& error);
		[[nodiscard]] str Write() const;

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;

		UI::ElementHandle Instantiate(UI::Context& context, UI::ElementHandle parent, const UIInstantiateDesc& desc = {}) const;

		UI::ElementHandle Instantiate(UI::Context& context, UI::ElementHandle parent, const UIInstantiateDesc& desc,
									  Vector<UIInstantiateMapping>& mapping) const;
	protected:
		void OnUnload() override;
	};

	class SHALLTEAR_CORE_API UIDocumentImporter : public IAssetImporter {
	public:
		// must match the class spelling
		StringHash GetAssetType() const override { return StringHash("UIDocumentAsset"); }
		Asset* Import(const Path& sourcePath) override;

		Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings) override;

		bool CanCreateAsset() const override { return true; }
		std::string_view GetCreateMenuLabel() const override { return "UI Document"; }
		std::string_view GetCreateDefaultFileName() const override { return "New UI Document.slxml"; }
		Asset* CreateAsset() override;
	};

} // namespace Shalltear
