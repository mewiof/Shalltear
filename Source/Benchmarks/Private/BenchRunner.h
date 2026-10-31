#pragma once

#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/Types.h"

#include <cstdio>
#include <functional>
#include <vector>

namespace Shalltear {

	class BenchRunner {
	public:
		void Run(const char* name, u64 ops, const std::function<void(u64)>& body) {
			constexpr int SAMPLES = 5;

			body(ops);

			f64 bestSeconds = 1e300;
			for (int i = 0; i < SAMPLES; ++i) {
				Stopwatch watch;
				watch.Start();
				body(ops);
				watch.Stop();
				if (watch.GetElapsedSeconds() < bestSeconds) {
					bestSeconds = watch.GetElapsedSeconds();
				}
			}

			f64 nsPerOp = (bestSeconds * 1e9) / static_cast<f64>(ops);
			std::printf("  %-40s %12.2f ns/op  (%llu ops)\n", name, nsPerOp, static_cast<unsigned long long>(ops));
			_results.push_back({name, nsPerOp});
		}

		struct Result {
			const char* name;
			f64 nsPerOp;
		};

		const std::vector<Result>& GetResults() const { return _results; }
	private:
		std::vector<Result> _results;
	};

} // namespace Shalltear
