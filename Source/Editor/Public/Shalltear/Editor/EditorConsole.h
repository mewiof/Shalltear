#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {

	struct ConsoleEntry {
		LogLevel level = LogLevel::Info;
		str category;
		str message;
		str file;
		str function;
		u32 line = 0;
		u32 column = 0;
		u64 sequence = 0;

		bool clickable = false;
	};

	struct ConsoleOrigin {
		str file;
		u32 line = 0;
		u32 column = 0;
	};

	struct ConsoleSummary {
		ConsoleEntry newest;
		ConsoleEntry newestUnseenError;
		u32 unseenErrors = 0;
		u32 unseenWarnings = 0;
	};

	class SHALLTEAR_EDITOR_API EditorConsole {
	public:
		static void Initialize();
		static void Terminate();
		static bool IsInitialized();

		static void Push(LogLevel level, std::string_view category, std::string_view message,
						 const ConsoleOrigin& origin);

		static u64 ReadSince(u64 sinceSequence, Vector<ConsoleEntry>& out);

		static void MarkSeenThrough(u64 sequence);

		static ConsoleSummary GetSummary(LogLevel messageFloor);

		static void Clear();
		static u32 GetBufferedCount();
		static u64 GetDroppedCount();
	};

} // namespace Shalltear
