#include "AnimationClipOps.h"

#include "Shalltear/Core/Math/MathUtil.h"

#include <cmath>

namespace Shalltear {

	namespace AnimationClipOps {

		namespace {

			bool IsKey(const AnimationClip::Track& track, i32 key) {
				return key >= 0 && static_cast<size_t>(key) < AnimationImpl::KeyCount(track);
			}

			void EraseKeyAt(AnimationClip::Track& track, size_t key) {
				const size_t stride = AnimationValuesPerKey(track.interp);
				track.times.erase(track.times.begin() + static_cast<ptrdiff_t>(key));
				track.values.erase(track.values.begin() + static_cast<ptrdiff_t>(key * stride),
								   track.values.begin() + static_cast<ptrdiff_t>((key + 1) * stride));
				if (key < track.assets.size()) {
					track.assets.erase(track.assets.begin() + static_cast<ptrdiff_t>(key));
				}
			}

			size_t SlotFor(const AnimationClip::Track& track, f32 time) {
				const size_t count = AnimationImpl::KeyCount(track);
				size_t slot = 0;
				while (slot < count && track.times[slot] <= time) {
					++slot;
				}
				return slot;
			}

			void InsertEntries(AnimationClip::Track& track, size_t slot, const Vec4* entries, size_t stride) {
				const ptrdiff_t base = static_cast<ptrdiff_t>(slot * stride);
				for (size_t v = 0; v < stride; ++v) {
					track.values.insert(track.values.begin() + base + static_cast<ptrdiff_t>(v), entries[v]);
				}
			}

		} // anonymous namespace

		i32 FindTrack(const AnimationClip& clip, const TrackAddress& address) {
			for (size_t i = 0; i < clip.tracks.size(); ++i) {
				const AnimationClip::Track& track = clip.tracks[i];
				if (track.kind == address.kind && track.component == address.component &&
					track.componentType == address.componentType && track.objectPath == address.objectPath &&
					track.name == address.name) {
					return static_cast<i32>(i);
				}
			}
			return -1;
		}

		i32 AddTrack(AnimationClip& clip, const TrackAddress& address, AnimationInterpolation interp) {
			const i32 existing = FindTrack(clip, address);
			if (existing >= 0) {
				return existing;
			}
			AnimationClip::Track track;
			track.kind = address.kind;
			track.interp = interp;
			track.component = address.component;
			track.objectPath = str(address.objectPath);
			track.componentType = address.componentType;
			track.name = str(address.name);
			clip.tracks.push_back(std::move(track));
			++clip.generation;
			return static_cast<i32>(clip.tracks.size()) - 1;
		}

		bool RemoveTrack(AnimationClip& clip, i32 track) {
			if (track < 0 || static_cast<size_t>(track) >= clip.tracks.size()) {
				return false;
			}
			const bool cue = clip.tracks[static_cast<size_t>(track)].kind == AnimationTrackKind::Audio;
			clip.tracks.erase(clip.tracks.begin() + track);
			++clip.generation;
			if (cue) {
				clip.HoldCueAssets();
			}
			return true;
		}

		bool SetTrackName(AnimationClip& clip, i32 track, std::string_view name) {
			if (track < 0 || static_cast<size_t>(track) >= clip.tracks.size() || name.empty()) {
				return false;
			}
			AnimationClip::Track& curve = clip.tracks[static_cast<size_t>(track)];
			if (curve.name == name) {
				return false;
			}
			TrackAddress wanted;
			wanted.kind = curve.kind;
			wanted.objectPath = curve.objectPath;
			wanted.componentType = curve.componentType;
			wanted.name = name;
			wanted.component = curve.component;
			if (FindTrack(clip, wanted) >= 0) {
				return false;
			}
			curve.name = str(name);
			++clip.generation;
			return true;
		}

		void SliceRange(const AnimationClip& source, f32 start, f32 end, AnimationClip& out) {
			constexpr f32 KEY_EPSILON = 1e-5f;

			out.skeletonGuid = source.skeletonGuid;
			out.frameRate = source.frameRate;
			out.length = end - start;
			out.tracks.clear();
			out.tracks.reserve(source.tracks.size());

			for (const AnimationClip::Track& from : source.tracks) {
				AnimationClip::Track track;
				track.kind = from.kind;
				track.interp = from.interp;
				track.component = from.component;
				track.objectPath = from.objectPath;
				track.componentType = from.componentType;
				track.name = from.name;
				const size_t stride = AnimationValuesPerKey(from.interp);

				auto pushSampled = [&](f32 at) {
					Vec4 value{};
					Vec4 tangent{};
					AnimationImpl::SampleForInsert(from, at, value, tangent);
					track.times.push_back(at - start);
					if (stride == 3) {
						track.values.push_back(tangent);
					}
					track.values.push_back(value);
					if (stride == 3) {
						track.values.push_back(tangent);
					}
				};

				if (from.kind == AnimationTrackKind::Audio) {
					for (size_t k = 0; k < from.times.size(); ++k) {
						const f32 t = from.times[k];
						if (t < start - KEY_EPSILON || t > end + KEY_EPSILON) {
							continue;
						}
						track.times.push_back(t - start);
						track.values.push_back(k < from.values.size() ? from.values[k] : Vec4{1.0f, 0.0f, 0.0f, 0.0f});
						track.assets.push_back(k < from.assets.size() ? from.assets[k] : Guid{});
					}
					out.tracks.push_back(std::move(track));
					continue;
				}

				bool hasStartKey = false;
				for (f32 t : from.times) {
					if (std::fabs(t - start) <= KEY_EPSILON) {
						hasStartKey = true;
						break;
					}
				}
				if (!hasStartKey && !from.times.empty()) {
					pushSampled(start);
				}

				const size_t count = AnimationImpl::KeyCount(from);
				for (size_t k = 0; k < count; ++k) {
					const f32 t = from.times[k];
					if (t < start - KEY_EPSILON || t > end + KEY_EPSILON) {
						continue;
					}
					track.times.push_back(t - start);
					for (size_t v = 0; v < stride; ++v) {
						track.values.push_back(from.values[k * stride + v]);
					}
				}

				const bool hasEndKey = !track.times.empty() &&
									   std::fabs(track.times.back() - (end - start)) <= KEY_EPSILON;
				if (!hasEndKey && !from.times.empty()) {
					pushSampled(end);
				}
				out.tracks.push_back(std::move(track));
			}
			++out.generation;
		}

		i32 KeyAt(const AnimationClip::Track& track, f32 time, f32 tolerance) {
			const size_t count = AnimationImpl::KeyCount(track);
			i32 best = -1;
			f32 bestDistance = tolerance;
			for (size_t i = 0; i < count; ++i) {
				const f32 distance = std::fabs(track.times[i] - time);
				if (distance <= bestDistance) {
					bestDistance = distance;
					best = static_cast<i32>(i);
				}
			}
			return best;
		}

		i32 AddKey(AnimationClip& clip, i32 track, f32 time) {
			if (track < 0 || static_cast<size_t>(track) >= clip.tracks.size() || !Math::IsFinite(time)) {
				return -1;
			}
			AnimationClip::Track& curve = clip.tracks[static_cast<size_t>(track)];
			constexpr f32 SAME_KEY = 1e-3f;
			const i32 existing = KeyAt(curve, time, SAME_KEY);
			if (existing >= 0) {
				return existing;
			}
			Vec4 value{};
			Vec4 tangent{};
			if (curve.kind != AnimationTrackKind::Event && curve.kind != AnimationTrackKind::Method &&
				curve.kind != AnimationTrackKind::Audio) {
				AnimationImpl::SampleForInsert(curve, time, value, tangent);
			}
			if (curve.kind == AnimationTrackKind::Audio) {
				value.x = 1.0f;
			}
			const size_t stride = AnimationValuesPerKey(curve.interp);
			const Vec4 entries[3] = {tangent, value, tangent};
			const Vec4* first = stride == 3u ? entries : entries + 1;
			const size_t slot = SlotFor(curve, time);
			curve.times.insert(curve.times.begin() + static_cast<ptrdiff_t>(slot), time);
			InsertEntries(curve, slot, first, stride);
			if (curve.kind == AnimationTrackKind::Audio) {
				curve.assets.insert(curve.assets.begin() + static_cast<ptrdiff_t>(slot), Guid{});
			}
			return static_cast<i32>(slot);
		}

		i32 MoveKey(AnimationClip& clip, i32 track, i32 key, f32 time) {
			if (track < 0 || static_cast<size_t>(track) >= clip.tracks.size() || !Math::IsFinite(time)) {
				return -1;
			}
			AnimationClip::Track& curve = clip.tracks[static_cast<size_t>(track)];
			if (!IsKey(curve, key)) {
				return -1;
			}
			const size_t from = static_cast<size_t>(key);
			if (curve.times[from] == time) {
				return key;
			}
			const size_t stride = AnimationValuesPerKey(curve.interp);
			Vec4 lifted[3]{};
			for (size_t v = 0; v < stride && v < 3; ++v) {
				lifted[v] = curve.values[from * stride + v];
			}
			const bool cue = curve.kind == AnimationTrackKind::Audio && from < curve.assets.size();
			const Guid asset = cue ? curve.assets[from] : Guid{};
			EraseKeyAt(curve, from);
			const size_t slot = SlotFor(curve, time);
			curve.times.insert(curve.times.begin() + static_cast<ptrdiff_t>(slot), time);
			InsertEntries(curve, slot, lifted, stride);
			if (cue) {
				curve.assets.insert(curve.assets.begin() + static_cast<ptrdiff_t>(slot), asset);
			}
			return static_cast<i32>(slot);
		}

		bool SetKeyValue(AnimationClip& clip, i32 track, i32 key, const Vec4& value) {
			if (track < 0 || static_cast<size_t>(track) >= clip.tracks.size()) {
				return false;
			}
			AnimationClip::Track& curve = clip.tracks[static_cast<size_t>(track)];
			if (!IsKey(curve, key)) {
				return false;
			}
			const size_t stride = AnimationValuesPerKey(curve.interp);
			curve.values[static_cast<size_t>(key) * stride + (stride == 3u ? 1u : 0u)] = value;
			return true;
		}

		bool RemoveKey(AnimationClip& clip, i32 track, i32 key) {
			if (track < 0 || static_cast<size_t>(track) >= clip.tracks.size()) {
				return false;
			}
			AnimationClip::Track& curve = clip.tracks[static_cast<size_t>(track)];
			if (!IsKey(curve, key)) {
				return false;
			}
			const bool cue = curve.kind == AnimationTrackKind::Audio;
			EraseKeyAt(curve, static_cast<size_t>(key));
			if (cue) {
				clip.HoldCueAssets();
			}
			return true;
		}

		bool SetKeyAsset(AnimationClip& clip, i32 track, i32 key, const Guid& asset) {
			if (track < 0 || static_cast<size_t>(track) >= clip.tracks.size()) {
				return false;
			}
			AnimationClip::Track& curve = clip.tracks[static_cast<size_t>(track)];
			if (curve.kind != AnimationTrackKind::Audio || !IsKey(curve, key) || static_cast<size_t>(key) >= curve.assets.size()) {
				return false;
			}
			curve.assets[static_cast<size_t>(key)] = asset;
			clip.HoldCueAssets();
			return true;
		}

		Vec4 AutoTangent(const AnimationClip::Track& track, i32 key) {
			const size_t count = AnimationImpl::KeyCount(track);
			if (count < 2 || !IsKey(track, key)) {
				return Vec4{};
			}
			const size_t i = static_cast<size_t>(key);
			const size_t before = i == 0 ? i : i - 1;
			const size_t after = i + 1 >= count ? i : i + 1;
			const Vec4 low = AnimationImpl::KeyValue(track, before);
			const Vec4 high = AnimationImpl::KeyValue(track, after);
			const Vec4 self = AnimationImpl::KeyValue(track, i);

			const bool interior = i > 0 && i + 1 < count;
			Vec4 slope;
			for (u32 c = 0; c < 4; ++c) {
				(&slope.x)[c] = Math::AutoSlope(track.times[before], (&low.x)[c], (&self.x)[c], track.times[after], (&high.x)[c], interior, true);
			}
			return slope;
		}

		bool SetInterpolation(AnimationClip& clip, i32 track, AnimationInterpolation interp) {
			if (track < 0 || static_cast<size_t>(track) >= clip.tracks.size()) {
				return false;
			}
			AnimationClip::Track& curve = clip.tracks[static_cast<size_t>(track)];
			if (curve.interp == interp) {
				return false;
			}
			const size_t from = AnimationValuesPerKey(curve.interp);
			const size_t to = AnimationValuesPerKey(interp);
			if (from == to) {
				curve.interp = interp;
				return true;
			}

			const size_t count = AnimationImpl::KeyCount(curve);
			// before stride changes (KeyValue reads interp)
			Vector<Vec4> lifted{GetMemoryResource(MemTag::Asset)};
			lifted.reserve(count);
			for (size_t k = 0; k < count; ++k) {
				lifted.push_back(AnimationImpl::KeyValue(curve, k));
			}
			curve.interp = interp;
			curve.values.clear();
			curve.values.reserve(count * to);
			for (size_t k = 0; k < count; ++k) {
				if (to == 3u) {
					curve.values.push_back(Vec4{});
					curve.values.push_back(lifted[k]);
					curve.values.push_back(Vec4{});
				} else {
					curve.values.push_back(lifted[k]);
				}
			}
			if (to == 3u) {
				for (size_t k = 0; k < count; ++k) {
					const Vec4 slope = AutoTangent(curve, static_cast<i32>(k));
					curve.values[k * 3u] = slope;
					curve.values[k * 3u + 2u] = slope;
				}
			}
			return true;
		}

		bool SetKeyTangents(AnimationClip& clip, i32 track, i32 key, const Vec4& in, const Vec4& out) {
			if (track < 0 || static_cast<size_t>(track) >= clip.tracks.size()) {
				return false;
			}
			AnimationClip::Track& curve = clip.tracks[static_cast<size_t>(track)];
			if (AnimationValuesPerKey(curve.interp) != 3u || !IsKey(curve, key)) {
				return false;
			}
			curve.values[static_cast<size_t>(key) * 3u] = in;
			curve.values[static_cast<size_t>(key) * 3u + 2u] = out;
			return true;
		}

		f32 DerivedLength(const AnimationClip& clip) {
			f32 last = 0.0f;
			for (const AnimationClip::Track& track : clip.tracks) {
				const size_t count = AnimationImpl::KeyCount(track);
				if (count > 0 && track.times[count - 1] > last) {
					last = track.times[count - 1];
				}
			}
			return last;
		}

		bool RecomputeLength(AnimationClip& clip) {
			const f32 derived = DerivedLength(clip);
			if (clip.length == derived) {
				return false;
			}
			clip.length = derived;
			return true;
		}

		f32 SnapTime(const AnimationClip& clip, f32 time) {
			if (!(clip.frameRate > 0.0f) || !Math::IsFinite(time)) {
				return time;
			}
			return std::round(time * clip.frameRate) / clip.frameRate;
		}

	} // namespace AnimationClipOps

} // namespace Shalltear
