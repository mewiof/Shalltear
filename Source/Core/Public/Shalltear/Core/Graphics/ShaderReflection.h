#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	enum class UniformMemberType : u8 {
		Unsupported = 0,
		Float,
		Vec2,
		Vec3,
		Vec4,
		Int,
		Bool,
	};

	SHALLTEAR_CORE_API std::string_view UniformMemberTypeName(UniformMemberType type);

	SHALLTEAR_CORE_API u32 UniformMemberFloatCount(UniformMemberType type);

	struct UniformMember {
		str name;
		UniformMemberType type = UniformMemberType::Unsupported;
		u32 offset = 0;
		u32 sizeBytes = 0;
	};

	struct ShaderMemberDefault {
		str name;
		f32 numbers[4] = {};
		u32 count = 0;
	};

	enum class ShaderTextureFallback : u8 {
		White = 0,
		Black,
		Normal,
		Transparent,
		Gray
	};

	struct ShaderTextureDefault {
		str name;
		ShaderTextureFallback fallback = ShaderTextureFallback::White;
	};

	struct ShaderMemberRange {
		str name;
		f32 minValue = 0.0f;
		f32 maxValue = 1.0f;
	};

	enum class ShaderHint : u8 {
		None = 0,
		Color,
		HDR,
		Toggle,
		Hidden
	};

	struct ShaderMemberHint {
		str name;
		ShaderHint hint = ShaderHint::None;
	};

	struct ShaderMemberLabel {
		str name;
		str label;
	};

	struct ShaderMemberGroup {
		str name;
		Vector<str> members{GetMemoryResource(MemTag::Asset)};
	};

	struct ShaderKeywordBinding {
		str keyword;
		str member;
		i32 value = -1;
		str entry;
	};

	struct UniformBlockLayout {
		u32 set = 0;
		u32 binding = 0;
		u32 sizeBytes = 0;
		Vector<UniformMember> members{GetMemoryResource(MemTag::Asset)};
	};

	struct ShaderResourceInfo {
		u32 set = 0;
		u32 binding = 0;
		RHI::DescriptorType type = RHI::DescriptorType::UniformBuffer;
		str name;
		RHI::TextureDimension dimension = RHI::TextureDimension::Texture2D;
	};

	struct SpirvReflection {
		bool valid = false;
		str error;

		RHI::ShaderStage stage = RHI::ShaderStage::None;
		Vector<RHI::DescriptorBindingDesc> bindings{GetMemoryResource(MemTag::Asset)};
		u32 pushConstantSize = 0;
		Vec3I threadGroupSize{};

		Vector<UniformBlockLayout> uniformBlocks{GetMemoryResource(MemTag::Asset)};
		Vector<ShaderResourceInfo> resources{GetMemoryResource(MemTag::Asset)};
	};

	SHALLTEAR_CORE_API SpirvReflection ReflectSpirv(std::span<const u8> blob);

	SHALLTEAR_CORE_API bool MergeReflections(const SpirvReflection& vertex, const SpirvReflection& pixel,
											 Vector<RHI::DescriptorBindingDesc>& outBindings, u32& outPushConstantSize, str& outError);

	SHALLTEAR_CORE_API void MergeMaterialInterfaces(const SpirvReflection& vertex, const SpirvReflection& pixel,
													Vector<UniformBlockLayout>& outBlocks,
													Vector<ShaderResourceInfo>& outResources);

} // namespace Shalltear
