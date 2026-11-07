#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Element.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class Asset;
	struct UIInstantiateDesc;

	namespace UI {

		class Context;

		enum class WidgetAttributeType : u8 {
			String,
			Bool,
			Number,
			Choice,
			// hex, #rgb to #rrggbbaa
			Color,
			// project://path, or project://path#name for a product
			Asset,
		};

		struct WidgetAttribute {
			std::string_view name;
			WidgetAttributeType type = WidgetAttributeType::String;
			std::span<const std::string_view> choices;
			std::span<const std::string_view> assetTypes;
			std::string_view defaultValue;
		};

		struct WidgetAttributeValue {
			std::string_view name;
			std::string_view value;
			// set for Asset attributes only
			Asset* asset = nullptr;
		};

		struct SHALLTEAR_CORE_API WidgetArgs {
			const UIInstantiateDesc& desc;
			std::span<const WidgetAttributeValue> attributes;
			bool richText = false;

			std::string_view Get(std::string_view name, std::string_view fallback = {}) const;
			bool GetBool(std::string_view name, bool fallback = false) const;
			f64 GetNumber(std::string_view name, f64 fallback = 0.0) const;
			Color GetColor(std::string_view name, Color fallback) const;
			Asset* GetAsset(std::string_view name) const;
		};

		struct WidgetResult {
			ElementHandle element;
			ElementHandle contentParent;
		};

		using WidgetFactoryFn = WidgetResult (*)(Context& context, ElementHandle parent, const WidgetArgs& args);

		// at import, after literal checks. Return false to refuse the document
		using WidgetValidateFn = bool (*)(std::span<const WidgetAttributeValue> attributes, u32 childCount, str& error);

		struct WidgetDesc {
			std::string_view tag;
			WidgetFactoryFn factory = nullptr;
			std::span<const WidgetAttribute> attributes;

			std::span<const std::string_view> builtInClasses;

			std::string_view category;

			WidgetValidateFn validate = nullptr;
		};

		class SHALLTEAR_CORE_API WidgetRegistry {
		public:
			static void Shutdown();

			static bool Register(const WidgetDesc& desc, const void* owner = nullptr);

			static void GetAll(Vector<const WidgetDesc*>& out);

			static const WidgetDesc* Find(std::string_view tag);

			// before the image unmaps (descs point into it)
			static u32 UnregisterOwnedBy(const void* owner);

			static bool IsValidLiteral(const WidgetAttribute& attribute, std::string_view text);
		};

	} // namespace UI

} // namespace Shalltear
