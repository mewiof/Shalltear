#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include <string_view>
#include <vector>

namespace Shalltear {

	// strings view the source text (escaped ones live in the arena)
	class SHALLTEAR_CORE_API JSONValue {
	public:
		enum class Type : u8 {
			Null,
			Bool,
			Number,
			String,
			Array,
			Object
		};

		Type type = Type::Null;
		bool boolValue = false;
		bool singlePrecision = false;
		f64 numberValue = 0.0;
		std::string_view stringValue;

		bool IsNull() const { return type == Type::Null; }
		bool IsBool() const { return type == Type::Bool; }
		bool IsNumber() const { return type == Type::Number; }
		bool IsString() const { return type == Type::String; }
		bool IsArray() const { return type == Type::Array; }
		bool IsObject() const { return type == Type::Object; }

		bool AsBool(bool fallback = false) const { return IsBool() ? boolValue : fallback; }
		f64 AsNumber(f64 fallback = 0.0) const { return IsNumber() ? numberValue : fallback; }
		i64 AsInt(i64 fallback = 0) const { return IsNumber() ? static_cast<i64>(numberValue) : fallback; }
		std::string_view AsString(std::string_view fallback = {}) const { return IsString() ? stringValue : fallback; }

		bool HasKey(std::string_view key) const { return Find(key) != nullptr; }
		const JSONValue* Find(std::string_view key) const;

		const JSONValue& operator[](std::string_view key) const;

		size_t Size() const { return _children.size(); }
		const JSONValue& operator[](size_t index) const;

		size_t MemberCount() const { return _keys.size(); }
		std::string_view KeyAt(size_t index) const { return index < _keys.size() ? _keys[index] : std::string_view{}; }
		const JSONValue& ChildAt(size_t index) const;

		static JSONValue MakeNull() { return JSONValue{}; }
		static JSONValue MakeBool(bool value);
		static JSONValue MakeNumber(f64 value);
		static JSONValue MakeFloat(f32 value);
		static JSONValue MakeString(std::string_view value);
		static JSONValue MakeArray() { return WithType(Type::Array); }
		static JSONValue MakeObject() { return WithType(Type::Object); }

		void Push(JSONValue value);

		void AddMember(std::string_view key, JSONValue value);
	private:
		static JSONValue WithType(Type t);

		std::vector<std::string_view> _keys;
		std::vector<JSONValue> _children;

		friend class JSONParser;
	};

	SHALLTEAR_CORE_API JSONValue JSONParse(std::string_view text, LinearAllocator& arena);

	SHALLTEAR_CORE_API str JSONSerialize(const JSONValue& value, bool pretty = true);

	constexpr u32 SLT_JSON_MAX_DEPTH = 32;

} // namespace Shalltear
