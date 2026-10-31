#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Element.h"

#include <span>
#include <string_view>

namespace Shalltear {

	struct UIInstantiateDesc;

	namespace UI {

		class Context;

		enum class WidgetAttributeType : u8 {
			String,
			Bool,
			Number,
		};

		struct WidgetAttribute {
			std::string_view name;
			WidgetAttributeType type = WidgetAttributeType::String;
		};

		struct WidgetAttributeValue {
			std::string_view name;
			std::string_view value;
		};

		struct SHALLTEAR_CORE_API WidgetArgs {
			const UIInstantiateDesc& desc;
			std::span<const WidgetAttributeValue> attributes;

			std::string_view Get(std::string_view name, std::string_view fallback = {}) const;
			bool GetBool(std::string_view name, bool fallback = false) const;
			f64 GetNumber(std::string_view name, f64 fallback = 0.0) const;
		};

		struct WidgetResult {
			ElementHandle element;
			ElementHandle contentParent;
		};

		using WidgetFactoryFn = WidgetResult (*)(Context& context, ElementHandle parent, const WidgetArgs& args);

		struct WidgetDesc {
			std::string_view tag;
			WidgetFactoryFn factory = nullptr;
			std::span<const WidgetAttribute> attributes;

			std::span<const std::string_view> builtInClasses;

			std::string_view category;
		};

		class SHALLTEAR_CORE_API WidgetRegistry {
		public:
			static void Shutdown();

			static bool Register(const WidgetDesc& desc, const void* owner = nullptr);

			static void GetAll(Vector<const WidgetDesc*>& out);

			static const WidgetDesc* Find(std::string_view tag);

			// before the image unmaps (descs point into it)
			static u32 UnregisterOwnedBy(const void* owner);

			static bool IsValidLiteral(WidgetAttributeType type, std::string_view text);
		};

	} // namespace UI

} // namespace Shalltear
