#include "Shalltear/Core/UI/UIDocument.h"

#include "Serialization/XML.h"
#include "UI/UIInternal.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/SpriteAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/Style.h"
#include "Shalltear/Core/UI/StyleSheetAsset.h"
#include "Shalltear/Core/UI/WidgetRegistry.h"

#include <deque>

namespace Shalltear {

	UIDocumentAsset::~UIDocumentAsset() = default;

	namespace {

		constexpr std::string_view AttrId = "id";
		constexpr std::string_view AttrClass = "class";
		constexpr std::string_view AttrStyle = "style";
		constexpr std::string_view AttrVersion = "version";
		constexpr std::string_view AttrSrc = "src";
		constexpr std::string_view AttrName = "name";
		constexpr std::string_view AttrTemplate = "template";
		constexpr std::string_view AttrElementId = "element-id";
		constexpr std::string_view AttrRichText = "enable-rich-text";
		constexpr std::string_view AttrTextKey = "text-key";

		constexpr std::string_view TagRoot = "ui";
		constexpr std::string_view TagSheet = "sheet";
		constexpr std::string_view TagTemplate = "template";
		constexpr std::string_view TagInstance = "instance";
		constexpr std::string_view TagOverrides = "attribute-overrides";

		constexpr u32 MAX_TEMPLATE_DEPTH = 16;

		str Refuse(std::string_view tag, std::string_view what) {
			return str("<") + str(tag) + str(">: ") + str(what);
		}

		str Name(const XML::XMLNode& node) {
			str spelled = str("<") + str(node.name);
			for (std::string_view key : {AttrId, AttrElementId, AttrName, AttrSrc}) {
				if (const XML::XMLAttribute* found = node.FindAttribute(key); found && !found->value.empty()) {
					spelled += str(" ") + str(key) + str("=\"") + str(found->value) + str("\"");
					break;
				}
			}
			return spelled + str(">");
		}

		str Refuse(const XML::XMLNode& node, std::string_view what) {
			return Name(node) + str(": ") + str(what);
		}

		u32 CountShadows(std::string_view value) {
			u32 count = 0;
			size_t cursor = 0;
			while (cursor <= value.size()) {
				size_t next = value.find(',', cursor);
				if (next == std::string_view::npos) {
					next = value.size();
				}
				std::string_view token = value.substr(cursor, next - cursor);
				if (token.find_first_not_of(" \t\r\n") != std::string_view::npos) {
					++count;
				}
				if (next == value.size()) {
					break;
				}
				cursor = next + 1;
			}
			return count;
		}

		bool ValidateStyleFragment(const XML::XMLNode& node, std::string_view fragment, str& error) {
			size_t cursor = 0;
			while (cursor < fragment.size()) {
				size_t colon = fragment.find(':', cursor);
				if (colon == std::string_view::npos) {
					break;
				}
				size_t semicolon = fragment.find(';', colon);
				if (semicolon == std::string_view::npos) {
					semicolon = fragment.size();
				}
				std::string_view name = fragment.substr(cursor, colon - cursor);
				size_t begin = name.find_first_not_of(" \t\r\n");
				size_t end = name.find_last_not_of(" \t\r\n");
				if (begin != std::string_view::npos) {
					name = name.substr(begin, end - begin + 1);
				}
				if (name == "box-shadow") {
					std::string_view value = fragment.substr(colon + 1, semicolon - colon - 1);
					u32 count = CountShadows(value);
					if (count > UI::Style::MAX_BOX_SHADOWS) {
						error = Refuse(node, str("box-shadow lists ") + str(std::to_string(count)) + str(" shadows, the cap is ") + str(std::to_string(UI::Style::MAX_BOX_SHADOWS)));
						return false;
					}
				}
				cursor = semicolon + 1;
			}
			return true;
		}

		str SpellBuiltInClasses(const UI::WidgetDesc& widget) {
			str spelled;
			for (std::string_view klass : widget.builtInClasses) {
				if (!spelled.empty()) {
					spelled += ", ";
				}
				spelled += str(klass);
			}
			return spelled;
		}

		u32 AuthorClassCap(const UI::WidgetDesc* widget) {
			size_t builtIn = widget ? widget->builtInClasses.size() : 0;
			return builtIn >= UI::MAX_ELEMENT_CLASSES ? 0u : static_cast<u32>(UI::MAX_ELEMENT_CLASSES - builtIn);
		}

		str SpellAttributeType(const UI::WidgetAttribute& attribute) {
			switch (attribute.type) {
			case UI::WidgetAttributeType::Bool:
				return "a boolean ('true' or 'false')";
			case UI::WidgetAttributeType::Number:
				return "a number";
			case UI::WidgetAttributeType::Choice: {
				str spelled = "1 of ";
				for (size_t i = 0; i < attribute.choices.size(); ++i) {
					spelled += (i == 0 ? "'" : "/'") + str(attribute.choices[i]) + "'";
				}
				return spelled;
			}
			case UI::WidgetAttributeType::Color:
				return "a hex color (like #ff8800)";
			case UI::WidgetAttributeType::Asset:
				return "an asset path (project://path, or project://path#name for a sprite)";
			case UI::WidgetAttributeType::String:
			default:
				return "a string";
			}
		}

		const UI::WidgetAttribute* DeclaredAttribute(const UI::WidgetDesc* widget, std::string_view name) {
			if (!widget) {
				return nullptr;
			}
			for (const UI::WidgetAttribute& candidate : widget->attributes) {
				if (candidate.name == name) {
					return &candidate;
				}
			}
			return nullptr;
		}

		bool RunWidgetValidator(const UI::WidgetDesc& widget, const Vector<UIDocumentAttribute>& attributes, u32 childCount, str& error) {
			if (!widget.validate) {
				return true;
			}
			Vector<UI::WidgetAttributeValue> values{GetMemoryResource(MemTag::Asset)};
			values.reserve(attributes.size());
			for (const UIDocumentAttribute& attribute : attributes) {
				values.push_back(UI::WidgetAttributeValue{attribute.name, attribute.value});
			}
			return widget.validate(values, childCount, error);
		}

		bool ConvertSheet(const XML::XMLNode& in, UIDocumentSheet& out, str& error) {
			for (const XML::XMLAttribute& attribute : in.attributes) {
				if (attribute.name != AttrSrc) {
					error = Refuse(in, str("unknown attribute '") + str(attribute.name) + str("'"));
					return false;
				}
				out.src = str(attribute.value);
			}
			if (out.src.empty()) {
				error = Refuse(in, str("needs a '") + str(AttrSrc) + str("' set to the style document to load"));
				return false;
			}
			if (!in.children.empty() || !in.text.empty()) {
				error = Refuse(in, "is a declaration and has no content");
				return false;
			}
			return true;
		}

		bool ConvertTemplate(const XML::XMLNode& in, UIDocumentTemplate& out, str& error) {
			for (const XML::XMLAttribute& attribute : in.attributes) {
				if (attribute.name == AttrSrc) {
					out.src = str(attribute.value);
				} else if (attribute.name == AttrName) {
					out.name = str(attribute.value);
				} else {
					error = Refuse(in, str("unknown attribute '") + str(attribute.name) + str("'"));
					return false;
				}
			}
			if (out.src.empty()) {
				error = Refuse(in, str("needs a '") + str(AttrSrc) + str("' set to the document to import"));
				return false;
			}
			if (out.name.empty()) {
				error = Refuse(in, str("needs a '") + str(AttrName) + str("' for an <") + str(TagInstance) + str("> to refer to"));
				return false;
			}
			if (!in.children.empty() || !in.text.empty()) {
				error = Refuse(in, "is a declaration and has no content");
				return false;
			}
			return true;
		}

		bool ConvertOverride(const XML::XMLNode& in, UIDocumentOverride& out, str& error) {
			for (const XML::XMLAttribute& attribute : in.attributes) {
				if (attribute.name == AttrElementId) {
					out.elementId = str(attribute.value);
					continue;
				}
				if (attribute.name == AttrId || attribute.name == AttrClass || attribute.name == AttrStyle || attribute.name == AttrRichText) {
					error = Refuse(in, str("'") + str(attribute.name) + str("' can't be overridden. Vary an instance's appearance with a selector"));
					return false;
				}
				if (attribute.name == AttrTextKey) {
					out.textKey = str(attribute.value);
					continue;
				}
				out.attributes.push_back(UIDocumentAttribute{str(attribute.name), str(attribute.value)});
			}
			if (out.elementId.empty()) {
				error = Refuse(in, str("needs an '") + str(AttrElementId) + str("' set to the element inside the template to change"));
				return false;
			}
			if (!in.children.empty()) {
				error = Refuse(in, "changes attributes and text on ONE element and takes no child elements");
				return false;
			}
			out.text = str(in.text);
			if (out.attributes.empty() && out.text.empty() && out.textKey.empty()) {
				error = Refuse(in, "overrides nothing (give it an attribute to set or text content to use)");
				return false;
			}
			return true;
		}

		bool ConvertNode(const XML::XMLNode& in, UIDocumentNode& out, str& error) {
			if (in.name == TagSheet) {
				error = Refuse(in, str("is a declaration and belongs directly inside <") + str(TagRoot) + str(">, beside the root element"));
				return false;
			}
			if (in.name == TagTemplate) {
				error = Refuse(in, str("is a declaration and belongs directly inside <") + str(TagRoot) + str(">, beside the root element"));
				return false;
			}
			if (in.name == TagOverrides) {
				error = Refuse(in, str("belongs directly inside an <") + str(TagInstance) + str(">"));
				return false;
			}
			out.tag = str(in.name);

			const bool isInstance = in.name == TagInstance;

			const UI::WidgetDesc* widget = isInstance ? nullptr : UI::WidgetRegistry::Find(in.name);
			const u32 classCap = AuthorClassCap(widget);

			for (const XML::XMLAttribute& attribute : in.attributes) {
				if (attribute.name == AttrId) {
					out.id = str(attribute.value);
				} else if (attribute.name == AttrStyle) {
					out.style = str(attribute.value);
					if (!ValidateStyleFragment(in, attribute.value, error)) {
						return false;
					}
				} else if (attribute.name == AttrClass) {
					size_t cursor = 0;
					u32 seen = 0;
					std::string_view value = attribute.value;
					while (cursor < value.size()) {
						size_t begin = value.find_first_not_of(" \t\r\n", cursor);
						if (begin == std::string_view::npos) {
							break;
						}
						size_t end = value.find_first_of(" \t\r\n", begin);
						if (end == std::string_view::npos) {
							end = value.size();
						}
						++seen;
						if (seen > classCap) {
							error = Refuse(in, str("has ") + str(std::to_string(seen)) + str("+ style classes, the cap is ") + str(std::to_string(UI::MAX_ELEMENT_CLASSES)));
							if (widget) {
								error += str(" and <") + str(in.name) + str("> contributes ") +
										 str(std::to_string(widget->builtInClasses.size())) + str(" (") + SpellBuiltInClasses(*widget) + str(")");
							}
							return false;
						}
						out.classes[seen - 1] = str(value.substr(begin, end - begin));
						cursor = end;
					}
					out.classCount = static_cast<u8>(seen);
				} else if (isInstance && attribute.name == AttrTemplate) {
					out.templateName = str(attribute.value);
				} else if (!isInstance && attribute.name == AttrTextKey) {
					if (widget && !DeclaredAttribute(widget, "text")) {
						error = Refuse(in, str("attribute '") + str(AttrTextKey) + str("' needs text (<") + str(in.name) + str("> has none)"));
						return false;
					}
					if (attribute.value.empty()) {
						error = Refuse(in, str("attribute '") + str(AttrTextKey) + str("' is empty"));
						return false;
					}
					out.textKey = str(attribute.value);
				} else if (!isInstance && attribute.name == AttrRichText) {
					if (widget && !DeclaredAttribute(widget, "text")) {
						error = Refuse(in, str("attribute '") + str(AttrRichText) + str("' needs text (<") + str(in.name) + str("> has none)"));
						return false;
					}
					if (attribute.value != "true" && attribute.value != "false") {
						error = Refuse(in, str("attribute '") + str(AttrRichText) + str("' wants true/false, not '") + str(attribute.value) + str("'"));
						return false;
					}
					out.richText = attribute.value == "true";
				} else {
					const UI::WidgetAttribute* declared = DeclaredAttribute(widget, attribute.name);
					if (!declared) {
						error = Refuse(in, str("unknown attribute '") + str(attribute.name) + str("'"));
						return false;
					}
					if (!UI::WidgetRegistry::IsValidLiteral(*declared, attribute.value)) {
						error = Refuse(in, str("attribute '") + str(attribute.name) + str("' wants ") + SpellAttributeType(*declared) +
											   str(", not '") + str(attribute.value) + str("'"));
						return false;
					}
					out.attributes.push_back(UIDocumentAttribute{str(attribute.name), str(attribute.value)});
				}
			}

			if (widget && out.attributes.size() > 1) {
				Vector<UIDocumentAttribute> ordered{GetMemoryResource(MemTag::Asset)};
				ordered.reserve(out.attributes.size());
				for (const UI::WidgetAttribute& declared : widget->attributes) {
					for (UIDocumentAttribute& authored : out.attributes) {
						if (authored.name == declared.name) {
							ordered.push_back(std::move(authored));
							break;
						}
					}
				}
				out.attributes = std::move(ordered);
			}

			if (isInstance && out.templateName.empty()) {
				error = Refuse(in, str("needs a '") + str(AttrTemplate) + str("' naming a <") + str(TagTemplate) + str("> this document declares"));
				return false;
			}

			out.text = str(in.text);
			if (widget && !out.text.empty()) {
				error = Refuse(in, "a widget element can't have text content (use its declared attributes)");
				return false;
			}
			if (widget) {
				str refused;
				if (!RunWidgetValidator(*widget, out.attributes, static_cast<u32>(in.children.size()), refused)) {
					error = Refuse(in, refused);
					return false;
				}
			}
			if (isInstance && !out.text.empty()) {
				error = Refuse(in, str("can't have text content (change text inside the template with <") + str(TagOverrides) + str(">)"));
				return false;
			}

			for (const XML::XMLNode& child : in.children) {
				if (isInstance) {
					if (child.name != TagOverrides) {
						error = Refuse(child, str("isn't allowed inside an <") + str(TagInstance) + str(">, which takes only <") + str(TagOverrides) + str("> children"));
						return false;
					}
					out.overrides.emplace_back();
					if (!ConvertOverride(child, out.overrides.back(), error)) {
						return false;
					}
					for (size_t i = 0; i + 1 < out.overrides.size(); ++i) {
						if (out.overrides[i].elementId == out.overrides.back().elementId) {
							error = Refuse(child, str("addresses '") + out.overrides.back().elementId + str("' a 2nd time in one <") + str(TagInstance) + str(">"));
							return false;
						}
					}
					continue;
				}
				out.children.emplace_back();
				if (!ConvertNode(child, out.children.back(), error)) {
					return false;
				}
			}
			return true;
		}

		XML::XMLNode BuildXML(const UIDocumentNode& in, std::deque<str>& pool) {
			XML::XMLNode out;
			out.name = in.tag;

			if (!in.templateName.empty()) {
				out.attributes.push_back({AttrTemplate, in.templateName});
			}
			if (!in.id.empty()) {
				out.attributes.push_back({AttrId, in.id});
			}
			if (in.classCount > 0) {
				str joined;
				for (u8 i = 0; i < in.classCount; ++i) {
					if (i > 0) {
						joined += ' ';
					}
					joined += in.classes[i];
				}
				pool.push_back(std::move(joined));
				out.attributes.push_back({AttrClass, pool.back()});
			}
			if (!in.style.empty()) {
				out.attributes.push_back({AttrStyle, in.style});
			}
			if (!in.richText) {
				out.attributes.push_back({AttrRichText, "false"});
			}
			if (!in.textKey.empty()) {
				out.attributes.push_back({AttrTextKey, in.textKey});
			}
			for (const UIDocumentAttribute& attribute : in.attributes) {
				out.attributes.push_back({attribute.name, attribute.value});
			}

			out.text = in.text;
			for (const UIDocumentOverride& block : in.overrides) {
				XML::XMLNode node;
				node.name = TagOverrides;
				node.attributes.push_back({AttrElementId, block.elementId});
				for (const UIDocumentAttribute& attribute : block.attributes) {
					node.attributes.push_back({attribute.name, attribute.value});
				}
				if (!block.textKey.empty()) {
					node.attributes.push_back({AttrTextKey, block.textKey});
				}
				node.text = block.text;
				out.children.push_back(std::move(node));
			}
			for (const UIDocumentNode& child : in.children) {
				out.children.push_back(BuildXML(child, pool));
			}
			return out;
		}

		const UIDocumentTemplate* FindTemplate(const UIDocumentAsset& document, std::string_view name) {
			for (const UIDocumentTemplate& declared : document.templates) {
				if (declared.name == name) {
					return &declared;
				}
			}
			return nullptr;
		}

		const UIDocumentAsset* TemplateOf(const UIDocumentAsset& document, const UIDocumentNode& node) {
			const UIDocumentTemplate* declared = FindTemplate(document, node.templateName);
			return declared ? declared->document.Get() : nullptr;
		}

		template <typename Fn>
		void ForEachExpandedElement(const UIDocumentAsset& document, const UIDocumentNode& node, std::string_view id, u32 depth, const Fn& fn) {
			if (!node.templateName.empty()) {
				const UIDocumentAsset* inner = depth < MAX_TEMPLATE_DEPTH ? TemplateOf(document, node) : nullptr;
				if (!inner) {
					return;
				}
				ForEachExpandedElement(*inner, inner->root, id.empty() ? std::string_view(inner->root.id) : id, depth + 1, fn);
				return;
			}
			fn(node, id);
			for (const UIDocumentNode& child : node.children) {
				ForEachExpandedElement(document, child, child.id, depth, fn);
			}
		}

		u32 ComposedClassCount(const UIDocumentAsset& document, const UIDocumentNode& node, u32 depth) {
			u32 count = node.classCount;
			if (!node.templateName.empty()) {
				const UIDocumentAsset* inner = depth < MAX_TEMPLATE_DEPTH ? TemplateOf(document, node) : nullptr;
				return inner ? count + ComposedClassCount(*inner, inner->root, depth + 1) : count;
			}
			const UI::WidgetDesc* widget = UI::WidgetRegistry::Find(node.tag);
			return count + (widget ? static_cast<u32>(widget->builtInClasses.size()) : 0u);
		}

		struct ImportChain {
			str virtualPath;
			const ImportChain* outer = nullptr;
		};

		const ImportChain* _importing = nullptr;

		struct ImportChainScope {
			ImportChain link;

			explicit ImportChainScope(const str& virtualPath) {
				link.virtualPath = virtualPath;
				link.outer = _importing;
				_importing = &link;
			}
			~ImportChainScope() { _importing = link.outer; }

			ImportChainScope(const ImportChainScope&) = delete;
			ImportChainScope& operator=(const ImportChainScope&) = delete;
		};

		void AppendImportChain(str& out, const ImportChain* link) {
			if (!link) {
				return;
			}
			AppendImportChain(out, link->outer);
			out += link->virtualPath;
			out += " -> ";
		}

		void ReleaseAssetRefs(Vector<UIDocumentAssetRef>& refs);

		void ReleaseTemplates(UIDocumentAsset& document) {
			ReleaseAssetRefs(document.assets);
			for (UIDocumentTemplate& declared : document.templates) {
				if (Asset* loaded = declared.document.Get()) {
					AssetDatabase::UnloadAsset(loaded);
				}
				declared.document = {};
			}
			for (UIDocumentSheet& declared : document.sheets) {
				if (Asset* loaded = declared.sheet.Get()) {
					AssetDatabase::UnloadAsset(loaded);
				}
				declared.sheet = {};
			}
		}

		bool ResolveSheets(UIDocumentAsset& document, str& error) {
			for (UIDocumentSheet& declared : document.sheets) {
				Path source(declared.src);
				Asset* loaded = AssetDatabase::LoadAsset(source);
				if (!loaded) {
					error = Refuse(TagSheet, str("uses '") + declared.src + str("' (didn't load)"));
					return false;
				}
				if (loaded->GetAssetTypeId() != StringHash("StyleSheetAsset")) {
					AssetDatabase::UnloadAsset(loaded);
					error = Refuse(TagSheet, str("uses '") + declared.src + str("' (not a style document)"));
					return false;
				}
				declared.sheet = Handle<StyleSheetAsset>(static_cast<StyleSheetAsset*>(loaded));
			}
			return true;
		}

		bool ResolveTemplates(UIDocumentAsset& document, str& error) {
			for (UIDocumentTemplate& declared : document.templates) {
				Path source(declared.src);
				const str& key = source.ToString();

				for (const ImportChain* link = _importing; link; link = link->outer) {
					if (link->virtualPath == key) {
						str spelled;
						AppendImportChain(spelled, _importing);
						error = Refuse(TagTemplate, str("'") + declared.name + str("' closes a cycle: ") + spelled + key);
						return false;
					}
				}

				Asset* loaded = AssetDatabase::LoadAsset(source);
				if (!loaded) {
					error = Refuse(TagTemplate, str("'") + declared.name + str("' uses '") + declared.src + str("' (didn't load)"));
					return false;
				}
				if (loaded->GetAssetTypeId() != StringHash("UIDocumentAsset")) {
					AssetDatabase::UnloadAsset(loaded);
					error = Refuse(TagTemplate, str("'") + declared.name + str("' uses '") + declared.src + str("' (not a layout document)"));
					return false;
				}
				declared.document = Handle<UIDocumentAsset>(static_cast<UIDocumentAsset*>(loaded));
			}
			return true;
		}

		void BindCookedHandle(UIDocumentSheet& declared, StyleSheetAsset* asset) {
			declared.sheet = Handle<StyleSheetAsset>(asset);
		}

		void BindCookedHandle(UIDocumentTemplate& declared, UIDocumentAsset* asset) {
			declared.document = Handle<UIDocumentAsset>(asset);
		}

		template <typename TAsset, typename TDeclared>
		bool BindCookedList(Vector<TDeclared>& declarations, CookReader& descriptor, std::string_view tag,
							std::string_view typeName, str& error) {
			u32 count = 0;
			descriptor.Read(count);
			if (descriptor.Failed() || count != declarations.size()) {
				error = Refuse(tag, str("the cooked record has ") + std::to_string(count) +
										str(" of them, the document declares ") +
										std::to_string(declarations.size()));
				return false;
			}
			for (TDeclared& declared : declarations) {
				Guid guid{};
				descriptor.Read(guid);
				if (descriptor.Failed()) {
					error = Refuse(tag, str("the cooked record ends inside its reference list"));
					return false;
				}
				Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
				if (!loaded) {
					error = Refuse(tag, str("uses '") + declared.src + str("' (not in the package)"));
					return false;
				}
				if (loaded->GetAssetTypeId() != StringHash(typeName)) {
					AssetDatabase::UnloadAsset(loaded);
					error = Refuse(tag, str("uses '") + declared.src + str("' (not a ") + str(typeName) + str(")"));
					return false;
				}
				BindCookedHandle(declared, static_cast<TAsset*>(loaded));
			}
			return true;
		}

		bool BindCookedReferences(UIDocumentAsset& document, CookReader& descriptor, str& error) {
			return BindCookedList<StyleSheetAsset>(document.sheets, descriptor, TagSheet, "StyleSheetAsset", error) &&
				   BindCookedList<UIDocumentAsset>(document.templates, descriptor, TagTemplate, "UIDocumentAsset",
												   error);
		}

		struct AssetUse {
			std::string_view src;
			const UI::WidgetAttribute* declared = nullptr;
			std::string_view tag;
		};

		constexpr std::string_view FontAssetTypes[] = {"FontAsset"};
		constexpr UI::WidgetAttribute FontUse = {"font-family", UI::WidgetAttributeType::Asset, {}, FontAssetTypes, {}};

		bool IsFontReference(std::string_view name) {
			return name.find("://") != std::string_view::npos;
		}

		template <typename Fn>
		void ForEachStyleFont(std::string_view fragment, std::string_view tag, const Fn& fn) {
			if (fragment.find("font-family") == std::string_view::npos) {
				return;
			}
			Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Asset)};
			UI::StyleSheet::SplitDeclarations(fragment, declarations);
			for (const UI::StyleDeclarationSource& declaration : declarations) {
				const std::string_view name = UI::FontNameOf(declaration.value);
				if (declaration.name == FontUse.name && IsFontReference(name)) {
					fn(AssetUse{name, &FontUse, tag});
				}
			}
		}

		template <typename Fn>
		void ForEachRichFont(std::string_view text, std::string_view tag, const Fn& fn) {
			if (text.find('<') == std::string_view::npos) {
				return;
			}
			Vector<std::string_view> names{GetMemoryResource(MemTag::Asset)};
			UI::CollectRichFonts(text, names);
			for (std::string_view name : names) {
				if (IsFontReference(name)) {
					fn(AssetUse{name, &FontUse, tag});
				}
			}
		}

		template <typename Fn>
		void ForEachAssetAttribute(const UIDocumentAsset& document, const UIDocumentNode& node, const Fn& fn) {
			ForEachStyleFont(node.style, node.tag, fn);
			if (node.richText) {
				ForEachRichFont(node.text, node.tag, fn);
				for (const UIDocumentAttribute& attribute : node.attributes) {
					if (attribute.name == "text") {
						ForEachRichFont(attribute.value, node.tag, fn);
					}
				}
			}
			if (node.templateName.empty()) {
				const UI::WidgetDesc* widget = UI::WidgetRegistry::Find(node.tag);
				for (const UIDocumentAttribute& attribute : node.attributes) {
					const UI::WidgetAttribute* declared = DeclaredAttribute(widget, attribute.name);
					if (declared && declared->type == UI::WidgetAttributeType::Asset) {
						fn(AssetUse{attribute.value, declared, node.tag});
					}
				}
			} else {
				for (const UIDocumentOverride& block : node.overrides) {
					ForEachExpandedElement(document, node, node.id, 0, [&](const UIDocumentNode& target, std::string_view id) {
						if (id != block.elementId) {
							return;
						}
						const UI::WidgetDesc* widget = UI::WidgetRegistry::Find(target.tag);
						if (target.richText) {
							ForEachRichFont(block.text, target.tag, fn);
							for (const UIDocumentAttribute& attribute : block.attributes) {
								if (attribute.name == "text") {
									ForEachRichFont(attribute.value, target.tag, fn);
								}
							}
						}
						for (const UIDocumentAttribute& attribute : block.attributes) {
							const UI::WidgetAttribute* declared = DeclaredAttribute(widget, attribute.name);
							if (declared && declared->type == UI::WidgetAttributeType::Asset) {
								fn(AssetUse{attribute.value, declared, target.tag});
							}
						}
					});
				}
			}
			for (const UIDocumentNode& child : node.children) {
				ForEachAssetAttribute(document, child, fn);
			}
		}

		void CollectAssetUses(const UIDocumentAsset& document, Vector<AssetUse>& out) {
			out.clear();
			ForEachAssetAttribute(document, document.root, [&out](const AssetUse& use) { out.push_back(use); });
		}

		bool IsFirstUse(const Vector<AssetUse>& uses, size_t index) {
			for (size_t i = 0; i < index; ++i) {
				if (uses[i].src == uses[index].src) {
					return false;
				}
			}
			return true;
		}

		u32 CountDistinctUses(const Vector<AssetUse>& uses) {
			u32 distinct = 0;
			for (size_t i = 0; i < uses.size(); ++i) {
				distinct += IsFirstUse(uses, i) ? 1u : 0u;
			}
			return distinct;
		}

		bool IsAcceptedAsset(const Asset& asset, const UI::WidgetAttribute& declared) {
			if (declared.assetTypes.empty()) {
				return true;
			}
			for (std::string_view type : declared.assetTypes) {
				if (asset.GetAssetTypeId() == StringHash(type)) {
					return true;
				}
			}
			return false;
		}

		const AssetUse* FindRefusingUse(const Vector<AssetUse>& uses, std::string_view src, const Asset& asset) {
			for (const AssetUse& use : uses) {
				if (use.src == src && !IsAcceptedAsset(asset, *use.declared)) {
					return &use;
				}
			}
			return nullptr;
		}

		str SpellAssetTypes(const UI::WidgetAttribute& declared) {
			str spelled;
			for (std::string_view type : declared.assetTypes) {
				spelled += (spelled.empty() ? "" : "/") + str(type);
			}
			return spelled;
		}

		Asset* LoadReferencedAsset(std::string_view src) {
			const Path whole{str(src)};
			const size_t hash = src.rfind('#');
			if (hash == std::string_view::npos || AssetDatabase::LookupGuid(whole).IsValid()) {
				return AssetDatabase::LoadAsset(whole);
			}
			const Path sourcePath{str(src.substr(0, hash))};
			Asset* source = AssetDatabase::LoadAsset(sourcePath);
			if (!source) {
				return nullptr;
			}
			const std::string_view name = src.substr(hash + 1);
			Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
			AssetDatabase::GetProducts(source->assetId, products);
			Asset* found = nullptr;
			for (const AssetProduct& product : products) {
				if ((product.name.empty() ? product.key : product.name) == name) {
					found = AssetDatabase::LoadAssetByGuid(product.guid);
					if (found) {
						break;
					}
				}
			}
			AssetDatabase::UnloadAsset(source);
			return found;
		}

		void ReleaseAssetRefs(Vector<UIDocumentAssetRef>& refs) {
			for (UIDocumentAssetRef& ref : refs) {
				if (Asset* loaded = ref.asset.Get()) {
					AssetDatabase::UnloadAsset(loaded);
				}
				ref.asset = {};
			}
			refs.clear();
		}

		bool BindCookedAssets(UIDocumentAsset& document, CookReader& descriptor, str& error) {
			Vector<AssetUse> uses{GetMemoryResource(MemTag::Asset)};
			CollectAssetUses(document, uses);
			const u32 distinct = CountDistinctUses(uses);
			u32 count = 0;
			descriptor.Read(count);
			if (descriptor.Failed() || count != distinct) {
				error = str("the cooked record has ") + std::to_string(count) + str(" asset reference(s), the document has ") +
						std::to_string(distinct);
				return false;
			}
			Vector<UIDocumentAssetRef> bound{GetMemoryResource(MemTag::Asset)};
			for (size_t i = 0; i < uses.size(); ++i) {
				if (!IsFirstUse(uses, i)) {
					continue;
				}
				const AssetUse& use = uses[i];
				Guid guid{};
				descriptor.Read(guid);
				Asset* loaded = descriptor.Failed() ? nullptr : AssetDatabase::LoadAssetByGuid(guid);
				const AssetUse* refusing = loaded ? FindRefusingUse(uses, use.src, *loaded) : &use;
				if (refusing) {
					if (loaded) {
						AssetDatabase::UnloadAsset(loaded);
					}
					ReleaseAssetRefs(bound);
					error = Refuse(refusing->tag, str("uses '") + str(use.src) + str("' (not in the package)"));
					return false;
				}
				bound.push_back(UIDocumentAssetRef{str(use.src), Handle<Asset>(loaded)});
			}
			document.assets = std::move(bound);
			return true;
		}

		str RefuseOverride(const UIDocumentNode& instance, const UIDocumentOverride& block, std::string_view what) {
			str spelled = str("<") + str(TagInstance) + str(" ") + str(AttrTemplate) + str("=\"") + instance.templateName + str("\"");
			if (!instance.id.empty()) {
				spelled += str(" ") + str(AttrId) + str("=\"") + instance.id + str("\"");
			}
			return spelled + str(">: overriding '") + block.elementId + str("': ") + str(what);
		}

		bool ValidateInstance(const UIDocumentAsset& document, const UIDocumentNode& node, str& error) {
			u32 classes = ComposedClassCount(document, node, 0);
			if (classes > UI::MAX_ELEMENT_CLASSES) {
				const UIDocumentAsset* inner = TemplateOf(document, node);
				u32 fromTemplate = inner ? ComposedClassCount(*inner, inner->root, 1) : 0;
				error = Refuse(TagInstance, str("'") + node.templateName + str("' composes to ") + str(std::to_string(classes)) +
												str(" style classes, the cap is ") + str(std::to_string(UI::MAX_ELEMENT_CLASSES)) +
												str(" (the template contributes ") + str(std::to_string(fromTemplate)) + str(")"));
				return false;
			}

			for (const UIDocumentOverride& block : node.overrides) {
				bool addressed = false;
				bool ok = true;
				ForEachExpandedElement(document, node, node.id, 0, [&](const UIDocumentNode& target, std::string_view id) {
					if (!ok || id != block.elementId) {
						return;
					}
					addressed = true;
					const UI::WidgetDesc* widget = UI::WidgetRegistry::Find(target.tag);
					for (const UIDocumentAttribute& attribute : block.attributes) {
						const UI::WidgetAttribute* declared = DeclaredAttribute(widget, attribute.name);
						if (!declared) {
							error = RefuseOverride(node, block, str("<") + target.tag + str("> has no attribute '") + attribute.name + str("'"));
							ok = false;
							return;
						}
						if (!UI::WidgetRegistry::IsValidLiteral(*declared, attribute.value)) {
							error = RefuseOverride(node, block, str("attribute '") + attribute.name + str("' wants ") + SpellAttributeType(*declared) + str(", not '") + attribute.value + str("'"));
							ok = false;
							return;
						}
					}
					if (widget && widget->validate) {
						Vector<UIDocumentAttribute> composed = target.attributes;
						for (const UIDocumentAttribute& attribute : block.attributes) {
							bool replaced = false;
							for (UIDocumentAttribute& existing : composed) {
								if (existing.name == attribute.name) {
									existing.value = attribute.value;
									replaced = true;
								}
							}
							if (!replaced) {
								composed.push_back(attribute);
							}
						}
						str refused;
						if (!RunWidgetValidator(*widget, composed, static_cast<u32>(target.children.size()), refused)) {
							error = RefuseOverride(node, block, refused);
							ok = false;
							return;
						}
					}
					if (!block.text.empty() && widget) {
						error = RefuseOverride(node, block, str("<") + target.tag + str("> is a widget and can't have text content"));
						ok = false;
					}
					if (ok && !block.textKey.empty() && widget && !DeclaredAttribute(widget, "text")) {
						error = RefuseOverride(node, block, str("<") + target.tag + str("> has no text for '") + str(AttrTextKey) + str("'"));
						ok = false;
					}
				});
				if (!ok) {
					return false;
				}
				if (!addressed) {
					error = RefuseOverride(node, block, "the template composes in no element with that id");
					return false;
				}
			}
			return true;
		}

		bool ValidateAliases(const UIDocumentAsset& document, const UIDocumentNode& node, str& error) {
			if (!node.templateName.empty() && !FindTemplate(document, node.templateName)) {
				error = Refuse(TagInstance, str("uses the template '") + node.templateName + str("' (this document doesn't declare it)"));
				return false;
			}
			for (const UIDocumentNode& child : node.children) {
				if (!ValidateAliases(document, child, error)) {
					return false;
				}
			}
			return true;
		}

		bool ValidateInstances(const UIDocumentAsset& document, const UIDocumentNode& node, str& error) {
			if (!node.templateName.empty() && !ValidateInstance(document, node, error)) {
				return false;
			}
			for (const UIDocumentNode& child : node.children) {
				if (!ValidateInstances(document, child, error)) {
					return false;
				}
			}
			return true;
		}

		void ApplyInlineStyle(std::string_view fragment, UI::Style& style, const UI::StyleVars& vars) {
			UI::StyleSheet sheet;
			str wrapped = str("*{") + str(fragment) + str("}");
			sheet.Parse(wrapped);
			if (sheet.GetRules().size() != 1) {
				SLT_LOG_WARN("UI", "UIDocument: inline style isn't a single declaration list, ignored: '{}'", str(fragment));
				return;
			}
			UI::StyleSheet::ApplyRule(style, sheet.GetRules()[0], &vars);
		}

		void RegisterFonts(const UIDocumentAsset& document, UI::Context& context) {
			for (const UIDocumentAssetRef& ref : document.assets) {
				Asset* asset = ref.asset.Get();
				if (asset && asset->GetAssetTypeId() == FontAsset::GetReflectType()->name) {
					context.SetStyleFont(ref.src, Handle<FontAsset>(static_cast<FontAsset*>(asset)));
				}
			}
		}

		void LoadSheets(const UIDocumentAsset& document, UI::Context& context) {
			for (const UIDocumentSheet& declared : document.sheets) {
				const StyleSheetAsset* sheet = declared.sheet.Get();
				if (!sheet) {
					continue;
				}
				for (const StyleSheetFont& entry : sheet->fonts) {
					context.SetStyleFont(entry.src, entry.font);
				}
				std::string_view key = sheet->virtualPath.empty() ? std::string_view(declared.src) : std::string_view(sheet->virtualPath);
				context.LoadStyleSheetOnce(StringHash(key), sheet->source);
			}
		}

		struct OverrideScope {
			const Vector<UIDocumentOverride>* overrides = nullptr;
			const OverrideScope* outer = nullptr;
		};

		struct ResolvedOverrides {
			const UIDocumentOverride* blocks[MAX_TEMPLATE_DEPTH] = {};
			u32 count = 0;
		};

		void CollectOverrides(const OverrideScope* scope, std::string_view id, ResolvedOverrides& out) {
			if (!scope || id.empty()) {
				return;
			}
			CollectOverrides(scope->outer, id, out);
			for (const UIDocumentOverride& block : *scope->overrides) {
				if (block.elementId == id && out.count < MAX_TEMPLATE_DEPTH) {
					out.blocks[out.count++] = &block;
				}
			}
		}

		struct IdentityLayer {
			const UIDocumentNode* instance = nullptr;
			const IdentityLayer* outer = nullptr;
		};

		struct DocumentChain {
			const UIDocumentAsset* document = nullptr;
			const DocumentChain* outer = nullptr;
		};

		bool AlreadyExpanding(const DocumentChain* chain, const UIDocumentAsset* document) {
			for (const DocumentChain* link = chain; link; link = link->outer) {
				if (link->document == document) {
					return true;
				}
			}
			return false;
		}

		struct InstantiateScope {
			const UIDocumentAsset* document = nullptr;
			const OverrideScope* overrides = nullptr;
			const IdentityLayer* identity = nullptr;
			const DocumentChain* expanding = nullptr;
			u32 depth = 0;
			Vector<UIInstantiateMapping>* mapping = nullptr;
		};

		Asset* FindReferencedAsset(const InstantiateScope& scope, std::string_view src) {
			for (const DocumentChain* link = scope.expanding; link; link = link->outer) {
				for (const UIDocumentAssetRef& ref : link->document->assets) {
					if (ref.src == src) {
						return ref.asset.Get();
					}
				}
			}
			return nullptr;
		}

		void RecordMapping(const InstantiateScope& scope, const UIDocumentNode& node, UI::ElementHandle element) {
			if (scope.mapping && element.IsValid()) {
				scope.mapping->push_back(UIInstantiateMapping{scope.document, &node, element});
			}
		}

		UI::ElementHandle InstantiateNode(const UIDocumentNode& node, UI::Context& context, UI::ElementHandle parent, const UIInstantiateDesc& desc, const InstantiateScope& scope);

		UI::ElementHandle InstantiateInstance(const UIDocumentNode& node, UI::Context& context, UI::ElementHandle parent, const UIInstantiateDesc& desc, const InstantiateScope& scope) {
			const UIDocumentAsset* inner = TemplateOf(*scope.document, node);
			if (!inner) {
				SLT_LOG_WARN("UI", "UIDocument: <{} {}=\"{}\"> didn't resolve to a document (ignored)", TagInstance, AttrTemplate, node.templateName);
				return {};
			}
			if (scope.depth >= MAX_TEMPLATE_DEPTH || AlreadyExpanding(scope.expanding, inner)) {
				SLT_LOG_WARN("UI", "UIDocument: <{} {}=\"{}\"> reaches a document already expanding, or nests past {} (ignored)",
							 TagInstance, AttrTemplate, node.templateName, MAX_TEMPLATE_DEPTH);
				return {};
			}

			RegisterFonts(*inner, context);
			LoadSheets(*inner, context);

			OverrideScope overrides{&node.overrides, scope.overrides};
			IdentityLayer identity{&node, scope.identity};
			DocumentChain expanding{inner, scope.expanding};
			InstantiateScope innerScope{inner, &overrides, &identity, &expanding, scope.depth + 1, scope.mapping};
			UI::ElementHandle element = InstantiateNode(inner->root, context, parent, desc, innerScope);
			RecordMapping(scope, node, element);
			return element;
		}

		UI::ElementHandle InstantiateNode(const UIDocumentNode& node, UI::Context& context, UI::ElementHandle parent, const UIInstantiateDesc& desc, const InstantiateScope& scope) {
			if (!node.templateName.empty()) {
				return InstantiateInstance(node, context, parent, desc, scope);
			}

			std::string_view id = node.id;
			for (const IdentityLayer* layer = scope.identity; layer; layer = layer->outer) {
				if (!layer->instance->id.empty()) {
					id = layer->instance->id;
				}
			}
			ResolvedOverrides overrides;
			CollectOverrides(scope.overrides, id, overrides);

			const UI::WidgetDesc* widget = UI::WidgetRegistry::Find(node.tag);

			UI::ElementHandle element;
			UI::ElementHandle contentParent;
			UI::ElementHandle textElement;
			str textFallback;
			if (widget) {
				Vector<UI::WidgetAttributeValue> values{GetMemoryResource(MemTag::Asset)};
				values.reserve(node.attributes.size());
				for (const UIDocumentAttribute& attribute : node.attributes) {
					values.push_back(UI::WidgetAttributeValue{attribute.name, attribute.value});
				}
				for (u32 i = 0; i < overrides.count; ++i) {
					for (const UIDocumentAttribute& attribute : overrides.blocks[i]->attributes) {
						bool replaced = false;
						for (UI::WidgetAttributeValue& value : values) {
							if (value.name == attribute.name) {
								value.value = attribute.value;
								replaced = true;
								break;
							}
						}
						if (!replaced) {
							values.push_back(UI::WidgetAttributeValue{attribute.name, attribute.value});
						}
					}
				}
				for (UI::WidgetAttributeValue& value : values) {
					const UI::WidgetAttribute* declared = DeclaredAttribute(widget, value.name);
					if (declared && declared->type == UI::WidgetAttributeType::Asset) {
						value.asset = FindReferencedAsset(scope, value.value);
					}
				}
				UI::WidgetResult built = widget->factory(context, parent, UI::WidgetArgs{desc, values, node.richText});
				if (!context.IsAlive(built.element)) {
					SLT_LOG_WARN("UI", "UIDocument: the factory for <{}> returned no element (subtree dropped)", node.tag);
					return {};
				}
				element = built.element;
				contentParent = built.contentParent.IsValid() ? built.contentParent : element;
				textElement = context.IsAlive(built.textElement) ? built.textElement : element;
				for (const UI::WidgetAttributeValue& value : values) {
					if (value.name == "text") {
						textFallback = value.value;
					}
				}
			} else {
				element = context.CreateElement();
				context.AppendChild(parent, element);
				contentParent = element;
				textElement = element;
			}

			RecordMapping(scope, node, element);

			if (!node.tag.empty()) {
				context.SetTypeTag(element, StringHash(node.tag));
			}
			if (!id.empty()) {
				context.SetId(element, StringHash(id));
			}
			for (u8 i = 0; i < node.classCount; ++i) {
				context.AddClass(element, StringHash(node.classes[i]));
			}
			for (const IdentityLayer* layer = scope.identity; layer; layer = layer->outer) {
				for (u8 i = 0; i < layer->instance->classCount; ++i) {
					context.AddClass(element, StringHash(layer->instance->classes[i]));
				}
			}

			bool styled = !node.style.empty();
			for (const IdentityLayer* layer = scope.identity; !styled && layer; layer = layer->outer) {
				styled = !layer->instance->style.empty();
			}
			if (styled) {
				UI::Style style;
				if (!node.style.empty()) {
					ApplyInlineStyle(node.style, style, context.GetStyleVars());
				}
				for (const IdentityLayer* layer = scope.identity; layer; layer = layer->outer) {
					if (!layer->instance->style.empty()) {
						ApplyInlineStyle(layer->instance->style, style, context.GetStyleVars());
					}
				}
				context.SetStyle(element, style);
			}

			std::string_view text = node.text;
			for (u32 i = 0; i < overrides.count; ++i) {
				if (!overrides.blocks[i]->text.empty()) {
					text = overrides.blocks[i]->text;
					break;
				}
			}
			if (!text.empty()) {
				context.SetRichText(element, node.richText);
				context.SetTextString(element, text);
			}
			std::string_view textKey = node.textKey;
			for (u32 i = 0; i < overrides.count; ++i) {
				if (!overrides.blocks[i]->textKey.empty()) {
					textKey = overrides.blocks[i]->textKey;
					break;
				}
			}
			if (!textKey.empty()) {
				context.SetRichText(textElement, node.richText);
				context.SetTextKey(textElement, textKey, widget ? std::string_view(textFallback) : text);
			}

			InstantiateScope childScope{scope.document, scope.overrides, nullptr, scope.expanding, scope.depth, scope.mapping};
			for (const UIDocumentNode& child : node.children) {
				InstantiateNode(child, context, contentParent, desc, childScope);
			}
			return element;
		}

		UI::ElementHandle InstantiateRoot(const UIDocumentAsset& document, UI::Context& context, UI::ElementHandle parent,
										  const UIInstantiateDesc& desc, Vector<UIInstantiateMapping>* mapping) {
			if (document.root.tag.empty()) {
				return {};
			}
			RegisterFonts(document, context);
			LoadSheets(document, context);

			DocumentChain expanding{&document, nullptr};
			InstantiateScope scope{&document, nullptr, nullptr, &expanding, 0, mapping};
			const UI::ElementHandle root = InstantiateNode(document.root, context, parent, desc, scope);
			if (context.IsAlive(root)) {
				UI::Style style = context.GetStyle(root);
				if (!style.font) {
					style.font = desc.font;
				}
				if (!style.fontSize) {
					style.fontSize = UI::Length::Pixel(desc.fontSizePx);
				}
				if (!style.color) {
					style.color = desc.textColor;
				}
				context.SetStyle(root, style);
			}
			return root;
		}

	} // anonymous namespace

	bool UIDocumentAsset::Parse(std::string_view source, str& error) {
		root = UIDocumentNode{};
		sheets.clear();
		templates.clear();
		assets.clear();

		LinearAllocator arena(source.size() + 64, MemTag::Asset);
		XML::XMLError xmlError;
		XML::XMLNode xmlRoot = XML::Parse(source, arena, xmlError);
		if (xmlError.Failed()) {
			error = xmlError.Describe();
			return false;
		}

		if (xmlRoot.name != TagRoot) {
			error = Refuse(xmlRoot.name, str("the document root must be <") + str(TagRoot) + str(">"));
			return false;
		}

		const XML::XMLAttribute* version = xmlRoot.FindAttribute(AttrVersion);
		if (!version) {
			error = Refuse(TagRoot, str("missing the required '") + str(AttrVersion) + str("' attribute"));
			return false;
		}
		if (version->value != std::to_string(FORMAT_VERSION)) {
			error = Refuse(TagRoot, str("version '") + str(version->value) + str("' isn't readable by this build (which reads ") + str(std::to_string(FORMAT_VERSION)) + str(")"));
			return false;
		}
		for (const XML::XMLAttribute& attribute : xmlRoot.attributes) {
			if (attribute.name != AttrVersion) {
				error = Refuse(TagRoot, str("unknown attribute '") + str(attribute.name) + str("'"));
				return false;
			}
		}

		auto discard = [this] {
			root = UIDocumentNode{};
			sheets.clear();
			templates.clear();
			return false;
		};

		const XML::XMLNode* rootElement = nullptr;
		for (const XML::XMLNode& child : xmlRoot.children) {
			if (child.name == TagSheet) {
				UIDocumentSheet declared;
				if (!ConvertSheet(child, declared, error)) {
					return discard();
				}
				for (const UIDocumentSheet& existing : sheets) {
					if (existing.src == declared.src) {
						error = Refuse(child, str("uses '") + declared.src + str("' a 2nd time"));
						return discard();
					}
				}
				sheets.push_back(std::move(declared));
				continue;
			}
			if (child.name == TagTemplate) {
				UIDocumentTemplate declared;
				if (!ConvertTemplate(child, declared, error)) {
					return discard();
				}
				for (const UIDocumentTemplate& existing : templates) {
					if (existing.name == declared.name) {
						error = Refuse(child, str("declares the name '") + declared.name + str("' a second time"));
						return discard();
					}
				}
				templates.push_back(std::move(declared));
				continue;
			}
			if (rootElement) {
				error = Refuse(TagRoot, str("more than one root element (") + Name(child) + str(" follows ") + Name(*rootElement) + str(")"));
				return discard();
			}
			rootElement = &child;
		}

		if (!rootElement) {
			error = Refuse(TagRoot, "the document has no root element");
			return discard();
		}

		if (!ConvertNode(*rootElement, root, error)) {
			return discard();
		}
		if (!ValidateAliases(*this, root, error)) {
			return discard();
		}
		return true;
	}

	str UIDocumentAsset::Write() const {
		if (root.tag.empty()) {
			return {};
		}

		std::deque<str> pool;
		XML::XMLNode xmlRoot;
		xmlRoot.name = TagRoot;
		pool.push_back(str(std::to_string(FORMAT_VERSION)));
		xmlRoot.attributes.push_back({AttrVersion, pool.back()});
		for (const UIDocumentSheet& declared : sheets) {
			XML::XMLNode node;
			node.name = TagSheet;
			node.attributes.push_back({AttrSrc, declared.src});
			xmlRoot.children.push_back(std::move(node));
		}
		for (const UIDocumentTemplate& declared : templates) {
			XML::XMLNode node;
			node.name = TagTemplate;
			node.attributes.push_back({AttrSrc, declared.src});
			node.attributes.push_back({AttrName, declared.name});
			xmlRoot.children.push_back(std::move(node));
		}
		xmlRoot.children.push_back(BuildXML(root, pool));
		return XML::Write(xmlRoot, true);
	}

	bool UIDocumentAsset::Import(const Path& sourcePath) {
		str text = FileSystem::ReadTextFile(sourcePath, MemTag::Asset);
		if (text.empty()) {
			SLT_LOG_ERROR("UI", "UIDocumentAsset: '{}' unreadable/empty", sourcePath.ToString());
			return false;
		}
		str error;
		if (!Parse(text, error)) {
			SLT_LOG_ERROR("UI", "UIDocumentAsset: '{}' rejected -> {}", sourcePath.ToString(), error);
			return false;
		}
		return true;
	}

	bool UIDocumentAsset::Export(const Path& destPath) const {
		str text = Write();
		if (text.empty()) {
			SLT_LOG_ERROR("UI", "UIDocumentAsset: nothing to write to '{}' (the document has no root element)", destPath.ToString());
			return false;
		}
		return FileSystem::WriteTextFile(destPath, text);
	}

	bool UIDocumentAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		str text = Write();
		if (text.empty()) {
			return false;
		}

		descriptor.Write(static_cast<u32>(sheets.size()));
		for (const UIDocumentSheet& declared : sheets) {
			const StyleSheetAsset* sheet = declared.sheet.Get();
			if (!sheet || !sheet->assetId.IsValid()) {
				SLT_LOG_ERROR("UI", "UIDocumentAsset: '{}' can't cook <sheet src=\"{}\">: it was never resolved",
							  virtualPath, declared.src);
				return false;
			}
			descriptor.WriteAssetRef(sheet->assetId);
		}
		descriptor.Write(static_cast<u32>(templates.size()));
		for (const UIDocumentTemplate& declared : templates) {
			const UIDocumentAsset* document = declared.document.Get();
			if (!document || !document->assetId.IsValid()) {
				SLT_LOG_ERROR("UI", "UIDocumentAsset: '{}' can't cook <template name=\"{}\">: it was never resolved",
							  virtualPath, declared.name);
				return false;
			}
			descriptor.WriteAssetRef(document->assetId);
		}
		descriptor.Write(static_cast<u32>(assets.size()));
		for (const UIDocumentAssetRef& ref : assets) {
			const Asset* asset = ref.asset.Get();
			if (!asset || !asset->assetId.IsValid()) {
				SLT_LOG_ERROR("UI", "UIDocumentAsset: '{}' can't cook '{}': it was never resolved", virtualPath, ref.src);
				return false;
			}
			descriptor.WriteAssetRef(asset->assetId);
		}

		payload.WriteBytes(text.data(), text.size());
		return true;
	}

	bool UIDocumentAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		size_t size = payload.Remaining();
		const u8* bytes = payload.ReadSpan(size);
		if (payload.Failed() || !bytes) {
			return false;
		}
		std::string_view text(reinterpret_cast<const char*>(bytes), size);

		str error;
		if (!Parse(text, error)) {
			SLT_LOG_ERROR("UI", "UIDocumentAsset: cooked '{}' rejected -> {}", virtualPath, error);
			return false;
		}

		if (!BindCookedReferences(*this, descriptor, error) || !BindCookedAssets(*this, descriptor, error)) {
			SLT_LOG_ERROR("UI", "UIDocumentAsset: cooked '{}' rejected -> {}", virtualPath, error);
			ReleaseTemplates(*this);
			return false;
		}
		return true;
	}

	bool UIDocumentAsset::ResolveAssetReferences(str& error) {
		Vector<AssetUse> uses{GetMemoryResource(MemTag::Asset)};
		CollectAssetUses(*this, uses);
		Vector<UIDocumentAssetRef> resolved{GetMemoryResource(MemTag::Asset)};
		for (size_t i = 0; i < uses.size(); ++i) {
			if (!IsFirstUse(uses, i)) {
				continue;
			}
			const AssetUse& use = uses[i];
			Asset* loaded = LoadReferencedAsset(use.src);
			if (!loaded) {
				ReleaseAssetRefs(resolved);
				error = Refuse(use.tag, str("uses '") + str(use.src) + str("' (didn't load)"));
				return false;
			}
			if (const AssetUse* refusing = FindRefusingUse(uses, use.src, *loaded)) {
				AssetDatabase::UnloadAsset(loaded);
				ReleaseAssetRefs(resolved);
				error = Refuse(refusing->tag, str("uses '") + str(use.src) + str("' (not a ") + SpellAssetTypes(*refusing->declared) + str(")"));
				return false;
			}
			resolved.push_back(UIDocumentAssetRef{str(use.src), Handle<Asset>(loaded)});
		}
		ReleaseAssetRefs(assets);
		assets = std::move(resolved);
		return true;
	}

	UI::ElementHandle UIDocumentAsset::Instantiate(UI::Context& context, UI::ElementHandle parent, const UIInstantiateDesc& desc) const {
		return InstantiateRoot(*this, context, parent, desc, nullptr);
	}

	UI::ElementHandle UIDocumentAsset::Instantiate(UI::Context& context, UI::ElementHandle parent, const UIInstantiateDesc& desc,
												   Vector<UIInstantiateMapping>& mapping) const {
		mapping.clear();
		return InstantiateRoot(*this, context, parent, desc, &mapping);
	}

	void UIDocumentAsset::OnUnload() {
		ReleaseTemplates(*this);
	}

	namespace {

		void MixVersion(u64& hash, u64 value) {
			constexpr u64 PRIME = 0x00000100000001b3ull;
			for (u32 shift = 0; shift < 64; shift += 8) {
				hash ^= (value >> shift) & 0xFF;
				hash *= PRIME;
			}
		}

		void MixContent(u64& hash, const UIDocumentAsset& document, u32 depth) {
			MixVersion(hash, document.GetInstanceId());
			MixVersion(hash, document.version);
			for (const UIDocumentSheet& declared : document.sheets) {
				const StyleSheetAsset* sheet = declared.sheet.Get();
				MixVersion(hash, sheet ? sheet->GetInstanceId() : 0);
				MixVersion(hash, sheet ? sheet->version : 0);
			}
			for (const UIDocumentTemplate& declared : document.templates) {
				const UIDocumentAsset* nested = declared.document.Get();
				if (nested && depth < 16) {
					MixContent(hash, *nested, depth + 1);
				} else {
					MixVersion(hash, 0);
				}
			}
			for (const UIDocumentAssetRef& ref : document.assets) {
				const Asset* asset = ref.asset.Get();
				const TextureAsset* texture = nullptr;
				if (asset && asset->GetAssetTypeId() == SpriteAsset::GetReflectType()->name) {
					texture = static_cast<const SpriteAsset*>(asset)->GetTexture();
				} else if (asset && asset->GetAssetTypeId() == TextureAsset::GetReflectType()->name) {
					texture = static_cast<const TextureAsset*>(asset);
				}
				MixVersion(hash, asset ? asset->GetInstanceId() : 0);
				MixVersion(hash, texture ? texture->version : 0);
			}
		}

	} // anonymous namespace

	u64 UIDocumentAsset::GetContentVersion() const {
		u64 hash = 0xcbf29ce484222325ull;
		MixContent(hash, *this, 0);
		return hash;
	}

	void UIDocumentAsset::AdoptReimport(Asset& fresh) {
		UIDocumentAsset& imported = static_cast<UIDocumentAsset&>(fresh);
		if (imported.Write() == Write()) {
			return;
		}
		std::swap(root, imported.root);
		std::swap(sheets, imported.sheets);
		std::swap(templates, imported.templates);
		std::swap(assets, imported.assets);
		++version;
	}

	Asset* UIDocumentImporter::Import(const Path& sourcePath) {
		UIDocumentAsset* asset = New<UIDocumentAsset>(MemTag::Asset);
		if (!asset->Import(sourcePath)) {
			Delete(asset);
			return nullptr;
		}
		return asset;
	}

	Asset* UIDocumentImporter::ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue&) {
		ImportChainScope chain(virtualPath.ToString());

		Asset* imported = Import(sourcePath);
		if (!imported) {
			return nullptr;
		}
		UIDocumentAsset* document = static_cast<UIDocumentAsset*>(imported);

		str error;
		if (!ResolveSheets(*document, error) || !ResolveTemplates(*document, error) || !ValidateInstances(*document, document->root, error) ||
			!document->ResolveAssetReferences(error)) {
			SLT_LOG_ERROR("UI", "UIDocumentAsset: '{}' rejected -> {}", virtualPath.ToString(), error);
			ReleaseTemplates(*document);
			Delete(document);
			return nullptr;
		}
		return document;
	}

	Asset* UIDocumentImporter::CreateAsset() {
		UIDocumentAsset* asset = New<UIDocumentAsset>(MemTag::Asset);
		asset->root.tag = "panel";
		return asset;
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::UIDocumentAsset)
