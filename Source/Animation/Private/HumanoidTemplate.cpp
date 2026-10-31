#include "Shalltear/Animation/RetargetProfile.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include <algorithm>

namespace Shalltear {

	namespace Retarget {

		namespace {

			enum class Side : u8 {
				None = 0,
				Left = 1,
				Right = 2,
			};

			str Normalize(std::string_view name) {
				const size_t colon = name.find_last_of(":|");
				if (colon != std::string_view::npos) {
					name = name.substr(colon + 1);
				}
				str out;
				out.reserve(name.size());
				for (char c : name) {
					if (c >= 'A' && c <= 'Z') {
						out.push_back(static_cast<char>(c - 'A' + 'a'));
					} else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
						out.push_back(c);
					}
				}
				return out;
			}

			u32 ScoreName(std::string_view name, std::string_view core, Side side) {
				const char* longSpelling = side == Side::Left ? "left" : "right";
				const char* shortSpelling = side == Side::Left ? "l" : "r";

				str aliases[4];
				u32 count = 0;
				if (side == Side::None) {
					aliases[count++] = str(core);
				} else {
					aliases[count++] = str(longSpelling) + str(core);
					aliases[count++] = str(core) + str(longSpelling);
					aliases[count++] = str(shortSpelling) + str(core);
					aliases[count++] = str(core) + str(shortSpelling);
				}

				u32 best = 0;
				for (u32 index = 0; index < count; ++index) {
					const str& alias = aliases[index];
					if (name == alias) {
						best = std::max(best, 1000u + static_cast<u32>(alias.size()));
					} else if (name.size() > alias.size() &&
							   name.compare(name.size() - alias.size(), alias.size(), alias) == 0) {
						best = std::max(best, static_cast<u32>(alias.size()));
					}
				}
				return best;
			}

			i32 FindBone(const SkeletonAsset& skeleton, const Vector<str>& normalized, const Vector<u8>& claimed,
						 std::string_view cores, Side side) {
				size_t cursor = 0;
				while (cursor <= cores.size()) {
					const size_t bar = cores.find('|', cursor);
					const std::string_view core =
						cores.substr(cursor, bar == std::string_view::npos ? std::string_view::npos : bar - cursor);
					cursor = bar == std::string_view::npos ? cores.size() + 1 : bar + 1;
					if (core.empty()) {
						continue;
					}

					i32 bestBone = -1;
					u32 bestScore = 0;
					for (size_t bone = 0; bone < normalized.size(); ++bone) {
						if (claimed[bone]) {
							continue;
						}
						const u32 score = ScoreName(normalized[bone], core, side);
						if (score > bestScore) {
							bestScore = score;
							bestBone = static_cast<i32>(bone);
						}
					}
					if (bestBone >= 0) {
						return bestBone;
					}
				}
				(void)skeleton;
				return -1;
			}

			struct ChainRule {
				const char* role;
				const char* mirror;
				const char* first;
				const char* last;
				Side side;
				bool required;
			};

			constexpr ChainRule Humanoid[] = {
				{"hips", "", "hips|pelvis|hip", nullptr, Side::None, true},
				{"spine", "", "spine|spine1|torso", "upperchest|chest|spine3|spine2|spine1", Side::None, true},
				{"neck", "", "neck", "head", Side::None, false},

				{"leftShoulder", "rightShoulder", "shoulder|clavicle|collar", nullptr, Side::Left, false},
				{"rightShoulder", "leftShoulder", "shoulder|clavicle|collar", nullptr, Side::Right, false},
				{"leftArm", "rightArm", "upperarm|arm", "hand|wrist", Side::Left, true},
				{"rightArm", "leftArm", "upperarm|arm", "hand|wrist", Side::Right, true},

				{"leftLeg", "rightLeg", "upleg|thigh|upperleg|leg", "toebase|toe|ball|foot|ankle", Side::Left, true},
				{"rightLeg", "leftLeg", "upleg|thigh|upperleg|leg", "toebase|toe|ball|foot|ankle", Side::Right, true},

				{"leftThumb", "rightThumb", "handthumb1|thumb1|thumb01|thumb", "", Side::Left, false},
				{"leftIndex", "rightIndex", "handindex1|index1|index01|index", "", Side::Left, false},
				{"leftMiddle", "rightMiddle", "handmiddle1|middle1|middle01|middle", "", Side::Left, false},
				{"leftRing", "rightRing", "handring1|ring1|ring01|ring", "", Side::Left, false},
				{"leftPinky", "rightPinky", "handpinky1|pinky1|pinky01|littlefinger1|pinky", "", Side::Left, false},
				{"rightThumb", "leftThumb", "handthumb1|thumb1|thumb01|thumb", "", Side::Right, false},
				{"rightIndex", "leftIndex", "handindex1|index1|index01|index", "", Side::Right, false},
				{"rightMiddle", "leftMiddle", "handmiddle1|middle1|middle01|middle", "", Side::Right, false},
				{"rightRing", "leftRing", "handring1|ring1|ring01|ring", "", Side::Right, false},
				{"rightPinky", "leftPinky", "handpinky1|pinky1|pinky01|littlefinger1|pinky", "", Side::Right, false},
			};

			void WalkUp(const SkeletonAsset& skeleton, i32 first, i32 last, Vector<i16>& out) {
				out.clear();
				i32 walk = last;
				for (u32 guard = 0; walk >= 0 && guard <= skeleton.parents.size(); ++guard) {
					out.push_back(static_cast<i16>(walk));
					if (walk == first) {
						std::reverse(out.begin(), out.end());
						return;
					}
					walk = skeleton.parents[static_cast<size_t>(walk)];
				}
				out.clear();
			}

			void WalkDown(const SkeletonAsset& skeleton, i32 first, Vector<i16>& out) {
				out.clear();
				const size_t boneCount = skeleton.parents.size();
				i32 walk = first;
				for (u32 guard = 0; walk >= 0 && guard <= boneCount; ++guard) {
					out.push_back(static_cast<i16>(walk));
					i32 onlyChild = -1;
					u32 children = 0;
					for (size_t bone = 0; bone < boneCount; ++bone) {
						if (skeleton.parents[bone] == static_cast<i16>(walk)) {
							onlyChild = static_cast<i32>(bone);
							++children;
						}
					}
					walk = children == 1 ? onlyChild : -1;
				}
			}

		} // anonymous namespace

		RetargetProfile* AutoMapHumanoid(Handle<SkeletonAsset> skeletonHandle) {
			const SkeletonAsset* skeleton = skeletonHandle.Get();
			if (!skeleton || skeleton->parents.empty()) {
				return nullptr;
			}
			const size_t boneCount = skeleton->parents.size();

			Vector<str> normalized{GetMemoryResource(MemTag::Asset)};
			normalized.reserve(boneCount);
			for (size_t bone = 0; bone < boneCount; ++bone) {
				normalized.push_back(Normalize(bone < skeleton->boneNames.size() ? skeleton->boneNames[bone]
																				 : std::string_view{}));
			}

			Vector<u8> claimed{GetMemoryResource(MemTag::Asset)};
			claimed.assign(boneCount, 0);

			RetargetProfile* profile = New<RetargetProfile>(MemTag::Asset);
			profile->skeletonGuid = skeleton->assetId;
			profile->skeleton = skeletonHandle;

			Vector<i16> run{GetMemoryResource(MemTag::Asset)};
			u32 missingRequired = 0;
			for (const ChainRule& rule : Humanoid) {
				const i32 first = FindBone(*skeleton, normalized, claimed, rule.first, rule.side);
				if (first < 0) {
					missingRequired += rule.required ? 1u : 0u;
					continue;
				}
				claimed[static_cast<size_t>(first)] = 1;

				if (rule.last == nullptr) {
					run.assign(1, static_cast<i16>(first));
				} else if (*rule.last == '\0') {
					WalkDown(*skeleton, first, run);
				} else {
					const i32 found = FindBone(*skeleton, normalized, claimed, rule.last, rule.side);
					WalkUp(*skeleton, first, found >= 0 ? found : first, run);
				}
				if (run.empty()) {
					missingRequired += rule.required ? 1u : 0u;
					SLT_LOG_TRACE("Animation", "auto-map: role '{}' matched 2 bones that aren't on one limb (dropped)", rule.role);
					continue;
				}

				RetargetProfile::Chain chain;
				chain.role = str(rule.role);
				chain.mirror = str(rule.mirror);
				chain.bones.assign(run.begin(), run.end());
				for (i16 bone : chain.bones) {
					claimed[static_cast<size_t>(bone)] = 1;
				}
				profile->chains.push_back(std::move(chain));
			}

			if (missingRequired > 0) {
				Delete(profile);
				return nullptr;
			}

			profile->referencePose.assign(skeleton->restPose.begin(), skeleton->restPose.end());
			return profile;
		}

	} // namespace Retarget

} // namespace Shalltear
