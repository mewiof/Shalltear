#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	enum class ShaderStageKind : u8 {
		Vertex = 0,
		Pixel,
		Compute,
		Count
	};

	struct ShaderIncludeDependency {
		str physicalPath;
		u64 contentHash = 0;
	};

	enum class ShaderCompileMode : u8 {
		Optimized = 0,
		Debug = 1,
		Unoptimized = 2
	};

	struct ShaderCompileResult {
		bool success = false;
		Vector<u8> spirv{GetMemoryResource(MemTag::Asset)};
		str errors;
		Vector<ShaderIncludeDependency> includes{GetMemoryResource(MemTag::Asset)};
	};

	class ShaderCompiler {
	public:
		static bool IsAvailable();

		static ShaderCompileResult Compile(
			std::string_view source,
			const Path& sourcePhysicalPath,
			ShaderStageKind stage,
			std::span<const str> defines,
			ShaderCompileMode mode);

		static u64 GetCompilerStamp();

		static void MarkCompileThread();
		static void ReleaseThreadCompiler();
		static void GetCompileCounts(u64& onCompileThreads, u64& elsewhere);

		class ScopedMessages {
		public:
			explicit ScopedMessages(str& sink);
			~ScopedMessages();
			ScopedMessages(const ScopedMessages&) = delete;
			ScopedMessages& operator=(const ScopedMessages&) = delete;
		private:
			str* _outer = nullptr;
		};
	};

} // namespace Shalltear
