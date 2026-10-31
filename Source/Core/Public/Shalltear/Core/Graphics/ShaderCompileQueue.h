#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	class ShaderAsset;

	using ShaderCompileTicket = u64;

	enum class ShaderCompileStatus : u8 {
		Pending,
		Succeeded,
		Failed,
		Unknown
	};

	struct ShaderCompileStats {
		u64 submitted = 0;
		u64 finished = 0;
		u64 compilesOnCompileThreads = 0;
		u64 compilesElsewhere = 0;
	};

	namespace ShaderCompileQueue {

		// file must exist until collected
		SHALLTEAR_CORE_API ShaderCompileTicket Submit(const Path& virtualOrPhysical, std::span<const str> keywords = {});

		SHALLTEAR_CORE_API ShaderCompileStatus Collect(ShaderCompileTicket ticket, ShaderAsset*& out, str* messages = nullptr);

		SHALLTEAR_CORE_API void Cancel(ShaderCompileTicket ticket);

		SHALLTEAR_CORE_API ShaderCompileStats GetStats();

		SHALLTEAR_CORE_API void Terminate();

	} // namespace ShaderCompileQueue

} // namespace Shalltear
