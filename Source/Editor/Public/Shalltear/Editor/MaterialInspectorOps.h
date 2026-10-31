#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/ShaderReflection.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <string_view>

namespace Shalltear {

	class ShaderAsset;

} // namespace Shalltear

namespace Shalltear::MaterialInspectorOps {

	enum class RowKind : u8 {
		Number,
		Slider,
		Toggle,
		Color,
		HDRColor,
		Vector,
		Texture,
		Keyword,
		KeywordEnum,
		Blend,
		Cull,
		AlphaClip,
		SortingPriority,
		Unsupported
	};

	struct KeywordEntry {
		str keyword;
		str name;
	};

	struct Row {
		str name;
		RowKind kind = RowKind::Unsupported;
		UniformMemberType member = UniformMemberType::Unsupported;
		f32 minValue = 0.0f;
		f32 maxValue = 1.0f;
		str keywordMember;
		str label;
		Vector<KeywordEntry> entries{GetMemoryResource(MemTag::Editor)};

		std::string_view DisplayName() const { return label.empty() ? std::string_view{name} : std::string_view{label}; }
	};

	struct Group {
		str name;
		bool builtIn = false;
		Vector<Row> rows{GetMemoryResource(MemTag::Editor)};
	};

	inline constexpr std::string_view ParametersGroup = "Parameters";
	inline constexpr std::string_view TexturesGroup = "Textures";
	inline constexpr std::string_view KeywordsGroup = "Keywords";
	inline constexpr std::string_view SurfaceOptionsGroup = "Surface Options";
	inline constexpr std::string_view AdvancedOptionsGroup = "Advanced Options";

	struct ShaderChoice {
		Guid guid;
		str displayName;
		str virtualPath;
		bool engine = false;
	};

	SHALLTEAR_EDITOR_API void CollectShaderChoices(Vector<ShaderChoice>& out);

	// pragmas exist only once a non HLSL source is generated
	using ShaderSourceFn = bool (*)(std::string_view source);
	SHALLTEAR_EDITOR_API bool RegisterShaderSource(std::string_view extension, ShaderSourceFn isMaterialShader, const void* owner);
	SHALLTEAR_EDITOR_API u32 UnregisterShaderSourcesOwnedBy(const void* owner);
	SHALLTEAR_EDITOR_API void TerminateShaderSources();

	SHALLTEAR_EDITOR_API void BuildLayout(const ShaderAsset& shader, Vector<Group>& out);

} // namespace Shalltear::MaterialInspectorOps
