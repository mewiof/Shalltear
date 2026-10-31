#include "BenchRunner.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <cstdio>
#include <unordered_map>
#include <vector>

using namespace Shalltear;

namespace {
	volatile u64 _sink = 0;
}

static void BenchVectorPushBack(BenchRunner& runner) {
	constexpr u64 OPS = 1'000'000;

	runner.Run("std::vector<u64> push_back", OPS, [](u64 ops) {
		std::vector<u64> v;
		for (u64 i = 0; i < ops; ++i) {
			v.push_back(i);
		}
		_sink += v.size();
	});

	runner.Run("Shalltear::Vector<u64> push_back (PMR)", OPS, [](u64 ops) {
		Vector<u64> v{GetMemoryResource(MemTag::Debug)};
		for (u64 i = 0; i < ops; ++i) {
			v.push_back(i);
		}
		_sink += v.size();
	});
}

static void BenchHashMapInsert(BenchRunner& runner) {
	constexpr u64 OPS = 100'000;

	runner.Run("std::unordered_map<u64,u64> insert", OPS, [](u64 ops) {
		std::unordered_map<u64, u64> m;
		for (u64 i = 0; i < ops; ++i) {
			m[i] = i;
		}
		_sink += m.size();
	});

	runner.Run("Shalltear::HashMap<u64,u64> insert (PMR)", OPS, [](u64 ops) {
		HashMap<u64, u64> m{GetMemoryResource(MemTag::Debug)};
		for (u64 i = 0; i < ops; ++i) {
			m[i] = i;
		}
		_sink += m.size();
	});
}

static void BenchAllocateFree(BenchRunner& runner) {
	constexpr u64 OPS = 200'000;

	runner.Run("operator new/delete 64B", OPS, [](u64 ops) {
		for (u64 i = 0; i < ops; ++i) {
			void* p = ::operator new(64);
			_sink += reinterpret_cast<uintptr_t>(p) & 1;
			::operator delete(p);
		}
	});

	runner.Run("Shalltear::Allocate/Free 64B (tagged)", OPS, [](u64 ops) {
		for (u64 i = 0; i < ops; ++i) {
			void* p = Allocate(64, MemTag::Debug);
			_sink += reinterpret_cast<uintptr_t>(p) & 1;
			Free(p);
		}
	});
}

static void BenchStringHash(BenchRunner& runner) {
	constexpr u64 OPS = 1'000'000;

	runner.Run("StringHash FNV-1a (24 chars)", OPS, [](u64 ops) {
		for (u64 i = 0; i < ops; ++i) {
			StringHash hash("Shalltear/Bench/String24");
			_sink += hash.GetValue() & 1;
		}
	});
}

void RunUIFrameBenchmarks(BenchRunner& runner);

int main() {
	std::printf("================ SHALLTEAR BENCHMARKS ================\n");

	BenchRunner runner;
	BenchVectorPushBack(runner);
	BenchHashMapInsert(runner);
	BenchAllocateFree(runner);
	BenchStringHash(runner);

	std::printf("======================================================\n");

	RunUIFrameBenchmarks(runner);

	return 0;
}
