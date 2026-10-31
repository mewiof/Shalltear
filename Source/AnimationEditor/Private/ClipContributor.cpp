#include "ClipContributor.h"

#include "ModelAnimationSettings.h"

#include "Shalltear/ImportPipeline/ModelContributors.h"

#include "AnimationClipOps.h"
#include "Retarget.h"
#include "RetargetPoseOps.h"

#include "Shalltear/Animation/AnimationClip.h"
#include "Shalltear/Animation/RetargetProfile.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/Memory.h"

namespace Shalltear {

	namespace AnimationEditor {

		namespace {

			constexpr const char* CONTRIBUTOR_ID = "dev.shalltear.animation.clips";

			// bump when products below change
			constexpr u32 CONTRIBUTOR_VERSION = 13;

			constexpr std::string_view ProductKindClips = "Clips";

			constexpr std::string_view ProductKindRigs = "Rigs";

			AnimationTrackKind KindOf(ModelChannel channel) {
				switch (channel) {
				case ModelChannel::Rotation:
					return AnimationTrackKind::BoneRotation;
				case ModelChannel::Scale:
					return AnimationTrackKind::BoneScale;
				case ModelChannel::Translation:
				default:
					return AnimationTrackKind::BonePosition;
				}
			}

			AnimationInterpolation InterpolationOf(ModelInterpolation interp) {
				switch (interp) {
				case ModelInterpolation::Step:
					return AnimationInterpolation::Step;
				case ModelInterpolation::CubicSpline:
					return AnimationInterpolation::CubicSpline;
				case ModelInterpolation::Linear:
				default:
					return AnimationInterpolation::Linear;
				}
			}

			AnimationClip* BuildClip(const ModelParseResult& parsed, const ModelTakeRange& take, const Guid& skeletonGuid) {
				AnimationClip* clip = New<AnimationClip>(MemTag::Asset);
				clip->skeletonGuid = skeletonGuid;
				clip->length = take.end > take.begin ? take.end - take.begin : 0.0f;

				clip->frameRate = 0.0f;

				clip->loop = AnimationLoopMode::None;

				clip->tracks.reserve(take.tracks.size());
				for (i32 trackIndex : take.tracks) {
					if (trackIndex < 0 || static_cast<size_t>(trackIndex) >= parsed.tracks.size()) {
						continue;
					}
					const ModelTrack& source = parsed.tracks[static_cast<size_t>(trackIndex)];
					if (source.node < 0 || static_cast<size_t>(source.node) >= parsed.nodes.size()) {
						continue;
					}

					AnimationClip::Track track;
					track.kind = KindOf(source.channel);
					track.interp = InterpolationOf(source.interp);
					track.name = parsed.nodes[static_cast<size_t>(source.node)].name;

					if (source.channel == ModelChannel::MorphWeight) {
						const i32 meshIndex = parsed.nodes[static_cast<size_t>(source.node)].mesh;
						const ModelMesh* mesh = (meshIndex >= 0 && static_cast<size_t>(meshIndex) < parsed.meshes.size())
													? &parsed.meshes[static_cast<size_t>(meshIndex)]
													: nullptr;
						if (mesh == nullptr || source.blendChannel < 0 ||
							static_cast<size_t>(source.blendChannel) >= mesh->blendChannels.size()) {
							continue;
						}
						track.kind = AnimationTrackKind::BlendShape;
						track.name = mesh->blendChannels[static_cast<size_t>(source.blendChannel)].name;
					}

					track.times.reserve(source.times.size());

					for (f32 time : source.times) {
						track.times.push_back(time - take.begin);
					}
					track.values = source.values;
					clip->tracks.push_back(std::move(track));
				}
				return clip;
			}

			std::string_view RigNameOf(std::string_view skeletonKey) {
				const size_t slash = skeletonKey.find_last_of('/');
				return slash == std::string_view::npos ? skeletonKey : skeletonKey.substr(slash + 1);
			}

			bool SeedStanceFromBindPose(const ModelParseResult& parsed, std::span<const i32> nodeToBone,
										const SkeletonAsset& skeleton, Vector<BoneTransform>& outPose) {
				const ModelSkinFrame frame = FindSkinFrame(parsed, nodeToBone);
				if (frame.skin < 0 || static_cast<size_t>(frame.skin) >= parsed.skins.size()) {
					return false;
				}
				const ModelSkin& skin = parsed.skins[static_cast<size_t>(frame.skin)];

				Vector<i16> jointToBone{GetMemoryResource(MemTag::Asset)};
				jointToBone.reserve(skin.joints.size());
				for (i32 jointNode : skin.joints) {
					const i32 bone = jointNode >= 0 && static_cast<size_t>(jointNode) < nodeToBone.size()
										 ? nodeToBone[static_cast<size_t>(jointNode)]
										 : -1;
					jointToBone.push_back(static_cast<i16>(bone));
				}

				return RetargetPoseOps::SampleBindPose(skeleton,
													   std::span<const i16>(jointToBone.data(), jointToBone.size()),
													   std::span<const Mat4>(skin.inverseBind.data(), skin.inverseBind.size()),
													   frame.meshToRig, outPose);
			}

			bool SeedStanceFromModel(const Guid& sourceGuid, const Path& modelVirtualPath,
									 const SkeletonAsset& skeleton, Vector<BoneTransform>& outPose) {
				const Path sourcePath = AssetDatabase::LookupPath(sourceGuid);
				if (sourcePath.ToString().empty()) {
					SLT_LOG_WARN("Animation", "'{}': Rig Pose is Copy From Model with asset {} (unknown to the database, using the file's stance)",
								 modelVirtualPath.ToString(), sourceGuid.ToString());
					return false;
				}
				if (sourcePath == modelVirtualPath) {
					SLT_LOG_WARN("Animation", "'{}': Rig Pose is Copy From Model with this same model (using the file's stance)",
								 modelVirtualPath.ToString());
					return false;
				}

				const ModelParseResult parsed = ParseReferencedModel(sourceGuid);
				if (!parsed.success) {
					SLT_LOG_WARN("Animation", "'{}': the stance source '{}' failed to parse: {} (using the file's stance)",
								 modelVirtualPath.ToString(), sourcePath.ToString(), parsed.error);
					return false;
				}
				SkeletonAsset* sourceRig = BuildNodeSkeleton(parsed);
				if (!sourceRig) {
					SLT_LOG_WARN("Animation", "'{}': the stance source '{}' has no nodes to take a stance from (using the file's stance)",
								 modelVirtualPath.ToString(), sourcePath.ToString());
					return false;
				}

				const ModelAnimationSettings sourceSettings = LoadModelAnimationSettings(sourcePath);
				Vector<BoneTransform> sourcePose{GetMemoryResource(MemTag::Asset)};
				sourcePose.assign(sourceRig->restPose.begin(), sourceRig->restPose.end());
				if (sourceSettings.rigPose == ModelRigPose::BindPose) {
					Vector<i16> identity{GetMemoryResource(MemTag::Asset)};
					Vector<i32> nodeToBone{GetMemoryResource(MemTag::Asset)};
					nodeToBone.reserve(parsed.nodes.size());
					for (size_t node = 0; node < parsed.nodes.size(); ++node) {
						nodeToBone.push_back(static_cast<i32>(node));
					}
					if (!SeedStanceFromBindPose(parsed, std::span<const i32>(nodeToBone.data(), nodeToBone.size()),
												*sourceRig, sourcePose)) {
						SLT_LOG_WARN("Animation", "'{}': the stance source '{}' is on Sample Bind-pose and has no bind pose (node transforms copied)",
									 modelVirtualPath.ToString(), sourcePath.ToString());
					}
				} else if (sourceSettings.rigPose == ModelRigPose::CopyFrom) {
					SLT_LOG_WARN("Animation", "'{}': the stance source '{}' is on Copy From Model too (not followed, node transforms copied)",
								 modelVirtualPath.ToString(), sourcePath.ToString());
				}

				const bool copied = RetargetPoseOps::CopyReferencePoseByName(
					*sourceRig, std::span<const BoneTransform>(sourcePose.data(), sourcePose.size()), skeleton, outPose);
				if (!copied) {
					SLT_LOG_WARN("Animation", "'{}': the stance source '{}' has no bone of this rig (using the file's stance)",
								 modelVirtualPath.ToString(), sourcePath.ToString());
				}
				Delete(sourceRig);
				return copied;
			}

			// caller owns the result
			RetargetProfile* MapRigProfile(const ModelContribution& contribution, const ModelAnimationSettings& settings,
										   Handle<SkeletonAsset> skeleton) {
				RetargetProfile* profile = Retarget::AutoMapHumanoid(skeleton);
				if (!profile) {
					return nullptr;
				}

				if (settings.rigPose == ModelRigPose::CopyFrom) {
					if (SeedStanceFromModel(settings.stanceSource, contribution.modelVirtualPath, *skeleton.Get(),
											profile->referencePose)) {
						SLT_LOG_TRACE("Animation", "'{}': the retarget profile's stance was copied from another model's rig",
									  contribution.modelVirtualPath.ToString());
					}
				} else if (settings.rigPose == ModelRigPose::BindPose) {
					if (!SeedStanceFromBindPose(contribution.parsed, contribution.nodeToBone, *skeleton.Get(),
												profile->referencePose)) {
						SLT_LOG_WARN("Animation", "'{}': Rig Pose is Sample Bind-pose and this model has no bind pose for its rig (using the file's stance)",
									 contribution.modelVirtualPath.ToString());
					} else {
						SLT_LOG_TRACE("Animation", "'{}': the retarget profile's stance was sampled from the file's bind pose",
									  contribution.modelVirtualPath.ToString());
					}
				}
				return profile;
			}

			void PublishRigProfile(const ModelContribution& contribution, RetargetProfile* profile,
								   std::string_view skeletonKey) {
				const u32 chains = static_cast<u32>(profile->chains.size());
				const str key = contribution.MakeProductKey(ProductKindRigs, RigNameOf(skeletonKey));
				const Guid guid = contribution.context.AddProduct(key, profile);
				if (!guid.IsValid()) {
					SLT_LOG_WARN("Animation", "'{}': couldn't add the retarget profile '{}' to the import",
								 contribution.modelVirtualPath.ToString(), key);
					return;
				}
				SLT_LOG_TRACE("Animation", "'{}': auto-mapped a humanoid rig, {} chain(s) as '{}'",
							  contribution.modelVirtualPath.ToString(), chains, key);
			}

			Guid ContributeUnskinnedRig(const ModelContribution& contribution, const ModelAnimationSettings& settings,
										RetargetProfile*& outProfile) {
				SkeletonAsset* skeleton = BuildNodeSkeleton(contribution.parsed);
				if (!skeleton) {
					return Guid{};
				}
				RetargetProfile* profile = MapRigProfile(contribution, settings, Handle<SkeletonAsset>(skeleton));
				if (!profile) {
					Delete(skeleton);
					return Guid{};
				}

				const str key = contribution.MakeProductKey(ProductKindSkeletons,
															ModelRootName(contribution.modelVirtualPath));
				const Guid guid = contribution.context.AddProduct(key, skeleton);
				if (!guid.IsValid()) {
					SLT_LOG_WARN("Animation", "'{}': couldn't add the rig '{}' to the import",
								 contribution.modelVirtualPath.ToString(), key);
					Delete(profile);
					return Guid{};
				}

				profile->skeletonGuid = guid;
				SLT_LOG_TRACE("Animation", "'{}': no skin -> the file's {} node(s) are its rig, minted as '{}'",
							  contribution.modelVirtualPath.ToString(), contribution.parsed.nodes.size(), key);
				PublishRigProfile(contribution, profile, key);
				outProfile = profile;
				return guid;
			}

			const ModelTakeRange* TakeForRange(const ModelParseResult& parsed, const ModelClipRange& range) {
				for (const ModelTakeRange& take : parsed.takes) {
					if (range.start >= take.begin && range.start < take.end) {
						return &take;
					}
				}
				return parsed.takes.empty() ? nullptr : &parsed.takes.front();
			}

			void ContributeClips(const ModelContribution& contribution, const ModelAnimationSettings& settings,
								 const Guid& skeletonGuid, bool importerMintedTheRig, const RetargetProfile* rigProfile) {
				const ModelParseResult& parsed = contribution.parsed;

				if (settings.type == ModelAnimationType::None) {
					SLT_LOG_TRACE("Animation", "'{}': Animation Type is None. No clips produced",
								  contribution.modelVirtualPath.ToString());
					return;
				}

				if (importerMintedTheRig) {
					contribution.AddRootComponent(StringHash("Animator"));
				}

				if (parsed.takes.empty()) {
					return;
				}

				u32 produced = 0;
				u32 tracks = 0;

				auto publish = [&](AnimationClip* clip, std::string_view name) {
					tracks += static_cast<u32>(clip->tracks.size());
					const str key = contribution.MakeProductKey(ProductKindClips, name);
					const Guid guid = contribution.context.AddProduct(key, clip);
					if (!guid.IsValid()) {
						SLT_LOG_WARN("Animation", "'{}': couldn't add clip '{}' to the import",
									 contribution.modelVirtualPath.ToString(), key);
						return;
					}
					++produced;
				};

				if (settings.clips.empty()) {
					for (const ModelTakeRange& take : parsed.takes) {
						publish(BuildClip(parsed, take, skeletonGuid), take.name);
					}
				} else {
					for (const ModelClipRange& range : settings.clips) {
						const ModelTakeRange* take = TakeForRange(parsed, range);
						if (!take) {
							continue;
						}
						AnimationClip* whole = BuildClip(parsed, *take, skeletonGuid);
						ModelClipRange local = range;
						local.start -= take->begin;
						local.end -= take->begin;
						auto* sliced = New<AnimationClip>(MemTag::Asset);
						AnimationClipOps::SliceRange(*whole, local.start, local.end, *sliced);
						sliced->loop = range.loop;
						Delete(whole);

						if (range.mirror) {
							AnimationClip* mirrored =
								rigProfile != nullptr ? Retarget::MirrorClip(*sliced, *rigProfile) : nullptr;
							if (!mirrored) {
								SLT_LOG_WARN("Animation", "'{}': clip '{}' asks for a mirror and this model's rig {} (no clip produced)",
											 contribution.modelVirtualPath.ToString(), range.name,
											 rigProfile != nullptr ? "has no chain to mirror"
																   : "didn't auto-map (no chain has a side)");
								Delete(sliced);
								continue;
							}
							Delete(sliced);
							sliced = mirrored;
						}
						publish(sliced, range.name);
					}
				}

				if (produced > 0 && tracks == 0) {
					SLT_LOG_WARN("Animation", "'{}': {} clip(s) produced, none has a curve (no readable channels in the file's takes)",
								 contribution.modelVirtualPath.ToString(), produced);
					return;
				}
				SLT_LOG_TRACE("Animation", "'{}': {} clip(s), {} curve(s), rig {}",
							  contribution.modelVirtualPath.ToString(), produced, tracks,
							  skeletonGuid.IsValid() ? skeletonGuid.ToString() : str("none"));
			}

			void Contribute(const ModelContribution& contribution) {
				const ModelAnimationSettings settings = ReadModelAnimationSettings(contribution.settings);

				Guid skeletonGuid;
				std::string_view skeletonKey;
				for (const ModelProduct& product : contribution.products) {
					if (product.type == SkeletonAsset::GetReflectType()->name) {
						skeletonGuid = product.guid;
						skeletonKey = product.key;
						break;
					}
				}

				const bool importerMintedTheRig = skeletonGuid.IsValid();

				Asset* heldSkeleton = nullptr;
				RetargetProfile* rigProfile = nullptr;

				if (importerMintedTheRig) {
					heldSkeleton = AssetDatabase::LoadAssetByGuid(skeletonGuid);
					if (heldSkeleton) {
						rigProfile = MapRigProfile(contribution, settings,
												   Handle<SkeletonAsset>(static_cast<SkeletonAsset*>(heldSkeleton)));
						if (rigProfile) {
							PublishRigProfile(contribution, rigProfile, skeletonKey);
						}
					}
				} else {
					skeletonGuid = ContributeUnskinnedRig(contribution, settings, rigProfile);
				}

				ContributeClips(contribution, settings, skeletonGuid, importerMintedTheRig, rigProfile);

				if (heldSkeleton) {
					AssetDatabase::UnloadAsset(heldSkeleton);
				}
			}

			const void* _owner = nullptr;

		} // anonymous namespace

		bool RegisterClipContributor(const void* owner) {
			ModelContributorDesc desc;
			desc.id = StringHash(CONTRIBUTOR_ID);
			desc.version = CONTRIBUTOR_VERSION;
			desc.wantsTracks = true;
			desc.contribute = &Contribute;
			desc.owner = owner;

			if (!ModelContributors::Register(desc)) {
				SLT_LOG_ERROR("Animation", "couldn't register the clip contributor");
				return false;
			}
			_owner = owner;
			return true;
		}

		void UnregisterClipContributor(const void* owner) {
			ModelContributors::UnregisterOwnedBy(owner != nullptr ? owner : _owner);
			_owner = nullptr;
		}

	} // namespace AnimationEditor

} // namespace Shalltear
