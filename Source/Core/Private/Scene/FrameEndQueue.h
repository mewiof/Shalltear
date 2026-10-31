#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class FrameEndQueue {
	public:
		enum class Kind : u8 {
			Object,
			Component,
			Scene, // dies after its roots
		};

		struct Entry {
			Kind kind;
			u64 id;
		};

		void Push(Kind kind, u64 id) { _entries.push_back(Entry{kind, id}); }
		bool IsEmpty() const { return _entries.empty(); }

		void Reset() { _entries = Vector<Entry>{GetMemoryResource(MemTag::Scene)}; }

		void Drain();
	private:
		Vector<Entry> _entries{GetMemoryResource(MemTag::Scene)};
	};

} // namespace Shalltear
