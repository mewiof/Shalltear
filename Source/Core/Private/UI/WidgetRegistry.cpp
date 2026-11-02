#include "Shalltear/Core/UI/WidgetRegistry.h"

#include "Shalltear/Core/Graphics/SpriteAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/UIDocument.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Module/EngineStateTable.h"
#include "UI/UIInternal.h"

#include <charconv>
#include <vector>

namespace Shalltear::UI {

	namespace {

		bool ParseBool(std::string_view text, bool& out) {
			if (text == "true") {
				out = true;
				return true;
			}
			if (text == "false") {
				out = false;
				return true;
			}
			return false;
		}

		bool ParseNumber(std::string_view text, f64& out) {
			if (text.empty()) {
				return false;
			}
			f64 parsed = 0.0;
			const char* end = text.data() + text.size();
			std::from_chars_result result = std::from_chars(text.data(), end, parsed);
			if (result.ec != std::errc{} || result.ptr != end) {
				return false;
			}
			out = parsed;
			return true;
		}

		constexpr std::string_view ButtonClasses[] = {"slt-button"};
		constexpr WidgetAttribute ButtonAttributes[] = {
			{"text", WidgetAttributeType::String},
		};

		WidgetResult MakeButton(Context& context, ElementHandle parent, const WidgetArgs& args) {
			Widgets::ButtonDesc desc;
			desc.label = args.Get("text");
			desc.font = args.desc.font;
			desc.fontSizePx = args.desc.fontSizePx;
			return {Widgets::Button(context, parent, desc), {}};
		}

		constexpr std::string_view CheckboxClasses[] = {"slt-checkbox"};
		constexpr WidgetAttribute CheckboxAttributes[] = {
			{"value", WidgetAttributeType::Bool},
		};

		WidgetResult MakeCheckbox(Context& context, ElementHandle parent, const WidgetArgs& args) {
			Widgets::CheckboxParts parts = Widgets::Checkbox(context, parent, args.GetBool("value"), {});
			return {parts.box, {}};
		}

		constexpr std::string_view SliderClasses[] = {"slt-slider"};
		constexpr WidgetAttribute SliderAttributes[] = {
			{"min", WidgetAttributeType::Number},
			{"max", WidgetAttributeType::Number},
			{"value", WidgetAttributeType::Number},
		};

		WidgetResult MakeSlider(Context& context, ElementHandle parent, const WidgetArgs& args) {
			Widgets::SliderDesc desc;
			desc.minValue = static_cast<f32>(args.GetNumber("min", 0.0));
			desc.maxValue = static_cast<f32>(args.GetNumber("max", 1.0));
			desc.value = static_cast<f32>(args.GetNumber("value", 0.0));
			return {Widgets::Slider(context, parent, desc).root, {}};
		}

		constexpr std::string_view ScrollViewClasses[] = {"slt-scroll-root"};

		WidgetResult MakeScrollView(Context& context, ElementHandle parent, const WidgetArgs&) {
			Widgets::ScrollViewParts parts = Widgets::ScrollView(context, parent);
			return {parts.root, parts.viewport};
		}

		constexpr std::string_view ImageSourceTypes[] = {"TextureAsset", "SpriteAsset"};
		constexpr std::string_view ScaleModes[] = {"scale-to-fit", "stretch-to-fill", "scale-and-crop", "sliced", "tiled"};
		constexpr std::string_view FillMethods[] = {"none", "horizontal", "vertical", "radial-90", "radial-180", "radial-360"};
		constexpr std::string_view FillOrigins[] = {"bottom", "top", "left", "right", "bottom-left", "bottom-right", "top-left", "top-right"};
		constexpr ImageScaleMode ScaleModeValues[] = {ImageScaleMode::ScaleToFit, ImageScaleMode::StretchToFill, ImageScaleMode::ScaleAndCrop,
													  ImageScaleMode::Sliced, ImageScaleMode::Tiled};
		constexpr ImageFillMethod FillMethodValues[] = {ImageFillMethod::None, ImageFillMethod::Horizontal, ImageFillMethod::Vertical,
														ImageFillMethod::Radial90, ImageFillMethod::Radial180, ImageFillMethod::Radial360};
		constexpr ImageFillOrigin FillOriginValues[] = {ImageFillOrigin::Bottom, ImageFillOrigin::Top, ImageFillOrigin::Left,
														ImageFillOrigin::Right, ImageFillOrigin::BottomLeft, ImageFillOrigin::BottomRight,
														ImageFillOrigin::TopLeft, ImageFillOrigin::TopRight};

		constexpr WidgetAttribute ImageAttributes[] = {
			{"src", WidgetAttributeType::Asset, {}, ImageSourceTypes},
			{"scale-mode", WidgetAttributeType::Choice, ScaleModes, {}, "scale-to-fit"},
			{"tint", WidgetAttributeType::Color, {}, {}, "#ffffff"},
			{"fill-method", WidgetAttributeType::Choice, FillMethods, {}, "none"},
			{"fill-origin", WidgetAttributeType::Choice, FillOrigins, {}, "bottom"},
			{"fill-amount", WidgetAttributeType::Number, {}, {}, "1"},
			{"fill-clockwise", WidgetAttributeType::Bool, {}, {}, "true"},
		};

		template <typename T, size_t N>
		T Choose(const std::string_view (&spellings)[N], const T (&values)[N], std::string_view text, T fallback) {
			for (size_t i = 0; i < N; ++i) {
				if (spellings[i] == text) {
					return values[i];
				}
			}
			return fallback;
		}

		ImageFillOrigin DefaultOrigin(ImageFillMethod method) {
			switch (method) {
			case ImageFillMethod::Horizontal:
				return ImageFillOrigin::Left;
			case ImageFillMethod::Radial90:
				return ImageFillOrigin::BottomLeft;
			default:
				return ImageFillOrigin::Bottom;
			}
		}

		ImageFill ReadFill(std::span<const WidgetAttributeValue> attributes) {
			WidgetArgs args{UIInstantiateDesc{}, attributes};
			ImageFill fill;
			fill.method = Choose(FillMethods, FillMethodValues, args.Get("fill-method"), ImageFillMethod::None);
			fill.origin = Choose(FillOrigins, FillOriginValues, args.Get("fill-origin"), DefaultOrigin(fill.method));
			fill.amount = static_cast<f32>(args.GetNumber("fill-amount", 1.0));
			fill.clockwise = args.GetBool("fill-clockwise", true);
			return fill;
		}

		bool ValidateImage(std::span<const WidgetAttributeValue> attributes, u32 childCount, str& error) {
			if (childCount > 0) {
				error = "takes no child elements";
				return false;
			}
			WidgetArgs args{UIInstantiateDesc{}, attributes};
			const ImageFill fill = ReadFill(attributes);
			if (fill.amount < 0.0f || fill.amount > 1.0f) {
				error = str("attribute 'fill-amount' wants 0-1, not '") + str(args.Get("fill-amount")) + str("'");
				return false;
			}
			if (!IsValidFillOrigin(fill.method, fill.origin)) {
				error = str("fill-origin '") + str(args.Get("fill-origin")) + str("' doesn't fit fill-method '") + str(args.Get("fill-method")) + str("'");
				return false;
			}
			return true;
		}

		WidgetResult MakeImage(Context& context, ElementHandle parent, const WidgetArgs& args) {
			ElementHandle element = context.CreateElement();
			context.AppendChild(parent, element);
			ImageDesc image;
			if (Asset* source = args.GetAsset("src")) {
				if (source->GetAssetTypeId() == SpriteAsset::GetReflectType()->name) {
					image.sprite = Handle<SpriteAsset>(static_cast<SpriteAsset*>(source));
				} else if (source->GetAssetTypeId() == TextureAsset::GetReflectType()->name) {
					image.texture = Handle<TextureAsset>(static_cast<TextureAsset*>(source));
				}
			}
			image.scaleMode = Choose(ScaleModes, ScaleModeValues, args.Get("scale-mode"), ImageScaleMode::ScaleToFit);
			image.fill = ReadFill(args.attributes);
			context.SetImage(element, image);
			context.SetTint(element, args.GetColor("tint", Color::White()));
			return {element, {}};
		}

		constexpr std::string_view CategoryContainers = "Containers";
		constexpr std::string_view CategoryControls = "Controls";

		constexpr WidgetDesc BuiltIns[] = {
			{"button", &MakeButton, ButtonAttributes, ButtonClasses, CategoryControls},
			{"checkbox", &MakeCheckbox, CheckboxAttributes, CheckboxClasses, CategoryControls},
			{"slider", &MakeSlider, SliderAttributes, SliderClasses, CategoryControls},
			{"scroll-view", &MakeScrollView, {}, ScrollViewClasses, CategoryContainers},
			{"image", &MakeImage, ImageAttributes, {}, CategoryControls, &ValidateImage},
		};

		struct WidgetEntry {
			str tag;
			WidgetDesc desc;
			const void* owner = nullptr;
			bool hasShadow = false;
			WidgetDesc shadow;
		};

		struct WidgetRegistryData {
			std::vector<WidgetEntry> widgets;
		};

		WidgetRegistryData* _boundData = nullptr;

		WidgetRegistryData& GetData() noexcept {
			if (_boundData) {
				return *_boundData;
			}
			static WidgetRegistryData data;
			return data;
		}

		WidgetEntry* FindEntry(WidgetRegistryData& data, std::string_view tag) {
			for (WidgetEntry& entry : data.widgets) {
				if (entry.tag == tag) {
					return &entry;
				}
			}
			return nullptr;
		}

		const WidgetDesc* FindBuiltIn(std::string_view tag) {
			for (const WidgetDesc& desc : BuiltIns) {
				if (desc.tag == tag) {
					return &desc;
				}
			}
			return nullptr;
		}

	} // anonymous namespace

	std::string_view WidgetArgs::Get(std::string_view name, std::string_view fallback) const {
		for (const WidgetAttributeValue& attribute : attributes) {
			if (attribute.name == name) {
				return attribute.value;
			}
		}
		return fallback;
	}

	bool WidgetArgs::GetBool(std::string_view name, bool fallback) const {
		bool parsed = fallback;
		ParseBool(Get(name), parsed);
		return parsed;
	}

	f64 WidgetArgs::GetNumber(std::string_view name, f64 fallback) const {
		f64 parsed = fallback;
		ParseNumber(Get(name), parsed);
		return parsed;
	}

	Color WidgetArgs::GetColor(std::string_view name, Color fallback) const {
		Color parsed = fallback;
		if (!Color::TryParseHex(Get(name), parsed)) {
			return fallback;
		}
		return parsed;
	}

	Asset* WidgetArgs::GetAsset(std::string_view name) const {
		for (const WidgetAttributeValue& attribute : attributes) {
			if (attribute.name == name) {
				return attribute.asset;
			}
		}
		return nullptr;
	}

	void WidgetRegistry::Shutdown() {
		WidgetRegistryData& data = GetData();
		data.widgets.clear();
		data.widgets.shrink_to_fit();
	}

	bool WidgetRegistry::Register(const WidgetDesc& desc, const void* owner) {
		if (desc.tag.empty() || !desc.factory) {
			SLT_LOG_WARN("UI", "WidgetRegistry::Register: ignoring a widget with no tag or no factory");
			return false;
		}
		WidgetRegistryData& data = GetData();

		for (WidgetEntry& entry : data.widgets) {
			if (entry.tag == desc.tag) {
				if (owner && !entry.owner && !entry.hasShadow) {
					entry.hasShadow = true;
					entry.shadow = entry.desc;
				} else if (!owner) {
					entry.hasShadow = false;
				}
				entry.desc = desc;
				entry.owner = owner;
				return true;
			}
		}
		data.widgets.push_back(WidgetEntry{str(desc.tag), desc, owner, false, {}});
		return true;
	}

	const WidgetDesc* WidgetRegistry::Find(std::string_view tag) {
		if (tag.empty()) {
			return nullptr;
		}
		if (WidgetEntry* entry = FindEntry(GetData(), tag)) {
			return &entry->desc;
		}
		return FindBuiltIn(tag);
	}

	void WidgetRegistry::GetAll(Vector<const WidgetDesc*>& out) {
		out.clear();
		WidgetRegistryData& data = GetData();
		for (const WidgetDesc& builtIn : BuiltIns) {
			const WidgetEntry* entry = FindEntry(data, builtIn.tag);
			out.push_back(entry ? &entry->desc : &builtIn);
		}
		for (const WidgetEntry& entry : data.widgets) {
			if (!FindBuiltIn(entry.tag)) {
				out.push_back(&entry.desc);
			}
		}
	}

	u32 WidgetRegistry::UnregisterOwnedBy(const void* owner) {
		if (!owner) {
			return 0;
		}
		WidgetRegistryData& data = GetData();
		u32 removed = 0;
		for (size_t i = data.widgets.size(); i > 0; --i) {
			WidgetEntry& entry = data.widgets[i - 1];
			if (entry.owner != owner) {
				continue;
			}
			++removed;
			if (entry.hasShadow) {
				entry.desc = entry.shadow;
				entry.owner = nullptr;
				entry.hasShadow = false;
				entry.shadow = {};
				continue;
			}
			data.widgets.erase(data.widgets.begin() + static_cast<ptrdiff_t>(i - 1));
		}
		return removed;
	}

	bool WidgetRegistry::IsValidLiteral(const WidgetAttribute& attribute, std::string_view text) {
		switch (attribute.type) {
		case WidgetAttributeType::Bool: {
			bool value = false;
			return ParseBool(text, value);
		}
		case WidgetAttributeType::Number: {
			f64 value = 0.0;
			return ParseNumber(text, value);
		}
		case WidgetAttributeType::Choice:
			for (std::string_view choice : attribute.choices) {
				if (choice == text) {
					return true;
				}
			}
			return false;
		case WidgetAttributeType::Color: {
			Color value;
			return !text.empty() && text[0] == '#' && Color::TryParseHex(text, value);
		}
		case WidgetAttributeType::Asset: {
			const size_t fragment = text.rfind('#');
			return !text.empty() && fragment != 0 && fragment != text.size() - 1;
		}
		case WidgetAttributeType::String:
		default:
			return true;
		}
	}

} // namespace Shalltear::UI

namespace Shalltear::ModuleInternal {

	void CaptureWidgetRegistryEngineState(EngineStateTable& table) {
		table.widgetRegistry = &UI::GetData();
	}

	void BindWidgetRegistryEngineState(const EngineStateTable& table) {
		UI::_boundData = static_cast<UI::WidgetRegistryData*>(table.widgetRegistry);
	}

} // namespace Shalltear::ModuleInternal
