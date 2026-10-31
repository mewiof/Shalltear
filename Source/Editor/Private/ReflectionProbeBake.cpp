#include "Shalltear/Editor/ReflectionProbeBake.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/CubeFace.h"
#include "Shalltear/Core/Graphics/ReflectionProbe.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"

#include "Shalltear/Editor/EditorAssetWatch.h"
#include "Shalltear/Editor/EditorCommands.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace Shalltear {

	namespace {

		constexpr std::string_view BAKE_SUFFIX = "-reflectionHDR";
		constexpr std::string_view BAKE_EXTENSION = ".hdr";

		str FileSafe(std::string_view name) {
			str out(name);
			for (char& c : out) {
				if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
					c = '_';
				}
			}
			return out.empty() ? str("Reflection Probe") : out;
		}

		struct ProbeUseQuery {
			const ReflectionProbe* except = nullptr;
			u64 cube = 0;
			bool found = false;
		};

		bool AnotherProbeUses(const ReflectionProbe& probe, u64 cube) {
			ProbeUseQuery query{&probe, cube, false};
			for (const Handle<Scene>& sceneHandle : SceneSystem::GetLoadedScenes()) {
				const Scene* scene = sceneHandle.Get();
				if (!scene || query.found) {
					continue;
				}
				scene->ForEachObject(
					[](SceneObject& object, void* user) {
						ProbeUseQuery& q = *static_cast<ProbeUseQuery*>(user);
						for (u32 index = 0; index < object.GetComponentCount() && !q.found; ++index) {
							const Component* component = object.GetComponentAt(index).Get();
							if (component && component != q.except &&
								TypeRegistry::IsDerivedFrom(component->GetType(), ReflectionProbe::GetReflectType())) {
								q.found = static_cast<const ReflectionProbe*>(component)->GetBakedCubemap().GetInstanceId() == q.cube;
							}
						}
					},
					&query);
			}
			return query.found;
		}

		void ToRGBE(Vec3 color, u8* out) {
			auto clean = [](f32 value) { return std::isfinite(value) ? Math::Clamp(value, 0.0f, 65504.0f) : 0.0f; };
			color = Vec3(clean(color.x), clean(color.y), clean(color.z));
			const f32 peak = Math::Max(color.x, Math::Max(color.y, color.z));
			if (peak < 1e-32f) {
				out[0] = out[1] = out[2] = out[3] = 0;
				return;
			}
			int exponent = 0;
			const f32 mantissa = std::frexp(peak, &exponent);
			const f32 scale = mantissa * 256.0f / peak;
			out[0] = static_cast<u8>(Math::Min(color.x * scale + 0.5f, 255.0f));
			out[1] = static_cast<u8>(Math::Min(color.y * scale + 0.5f, 255.0f));
			out[2] = static_cast<u8>(Math::Min(color.z * scale + 0.5f, 255.0f));
			out[3] = static_cast<u8>(exponent + 128);
		}

		struct InFlight {
			Handle<ReflectionProbe> probe;
			ViewTargetId targets[CUBE_FACE_COUNT]{};
			u32 faceSize = 0;
			Path path;
		};

		struct BakeState {
			Vector<Handle<ReflectionProbe>> queue{GetMemoryResource(MemTag::Editor)};
			bool active = false;
			InFlight inFlight;
		};

		BakeState* _state = nullptr;

		void ReleaseTargets(InFlight& bake) {
			for (ViewTargetId& target : bake.targets) {
				if (target != INVALID_VIEW_TARGET) {
					RenderSystem::DestroyViewTarget(target);
					target = INVALID_VIEW_TARGET;
				}
			}
		}

		bool Start(Handle<ReflectionProbe> probeHandle) {
			const str why = ReflectionProbeBake::WhyNotBakeable(probeHandle);
			if (!why.empty()) {
				SLT_LOG_WARN("Editor", "Reflection probe not baked: {}", why);
				return false;
			}
			ReflectionProbe* probe = probeHandle.Get();
			InFlight bake;
			bake.probe = probeHandle;
			bake.faceSize = static_cast<u32>(RenderSystem::GetReflectionProbeResolution());
			bake.path = ReflectionProbeBakeOps::BakePath(probeHandle);
			const Vec3 eye = probe->GetOwner()->GetWorldPosition();
			const Mat4 projection = Mat4::PerspectiveReverseZ(90.0f, 1.0f, probe->GetNearPlane(), probe->GetFarPlane());
			const i32 edge = static_cast<i32>(bake.faceSize);
			for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
				bake.targets[face] = RenderSystem::CreateViewTarget({edge, edge});
				if (bake.targets[face] == INVALID_VIEW_TARGET) {
					ReleaseTargets(bake);
					SLT_LOG_ERROR("Editor", "Reflection probe not baked: no view target of {}x{}", edge, edge);
					return false;
				}
				ViewDesc desc;
				desc.view = CubeFaceView(face, eye);
				desc.projection = projection;
				desc.cameraPosition = eye;
				desc.clearColor = Color::Black();
				desc.radianceCapture = true;
				RenderSystem::SubmitView(bake.targets[face], desc);
			}
			_state->inFlight = bake;
			_state->active = true;
			return true;
		}

		void Finish(InFlight& bake) {
			const u64 faceBytes = static_cast<u64>(bake.faceSize) * bake.faceSize * 8;
			Vector<u8> faces{GetMemoryResource(MemTag::Editor)};
			faces.reserve(static_cast<size_t>(faceBytes * CUBE_FACE_COUNT));
			Vector<u8> one{GetMemoryResource(MemTag::Editor)};
			bool complete = true;
			for (ViewTargetId target : bake.targets) {
				Vec2I size{};
				if (!RenderSystem::ReadbackViewTargetRadiance(target, one, size) || one.size() != faceBytes) {
					complete = false;
					break;
				}
				faces.insert(faces.end(), one.begin(), one.end());
			}
			ReleaseTargets(bake);

			ReflectionProbe* probe = bake.probe.Get();
			if (!probe) {
				SLT_LOG_INFO("Editor", "A reflection probe was destroyed before its bake was read. Nothing written");
				return;
			}
			if (!complete) {
				SLT_LOG_ERROR("Editor", "Reflection probe not baked: a face wasn't captured");
				return;
			}

			const Vector<u8> encoded = ReflectionProbeBakeOps::EncodeStrip({faces.data(), faces.size()}, bake.faceSize);
			Buffer file(encoded.size(), MemTag::Editor);
			std::memcpy(file.data, encoded.data(), encoded.size());
			const bool known = AssetDatabase::LookupGuid(bake.path).IsValid();
			const str directory = bake.path.GetDirectory();
			FileSystem::CreateDirectory(Path(directory));
			if (encoded.empty() || !FileSystem::WriteFile(bake.path, file)) {
				SLT_LOG_ERROR("Editor", "Reflection probe not baked: '{}' couldn't be written", bake.path.ToString());
				return;
			}
			EditorAssetWatch::NotifyChanged(bake.path);

			const Guid guid = AssetDatabase::EnsureAssetIdentity(bake.path);
			if (!guid.IsValid()) {
				SLT_LOG_ERROR("Editor", "Reflection probe not baked: '{}' isn't an asset", bake.path.ToString());
				return;
			}
			if (known && !AssetDatabase::ReimportAsset(bake.path)) {
				SLT_LOG_ERROR("Editor", "Reflection probe baked into '{}' (the cube couldn't be re-imported)", bake.path.ToString());
				return;
			}
			Asset* asset = AssetDatabase::LoadAssetByGuid(guid);
			TextureAsset* cube = asset && asset->GetAssetTypeId() == StringHash("TextureAsset") ? static_cast<TextureAsset*>(asset) : nullptr;
			if (!cube || !cube->isCube) {
				SLT_LOG_ERROR("Editor", "Reflection probe baked into '{}' (didn't import as a cube). Check its import settings",
							  bake.path.ToString());
				if (asset) {
					AssetDatabase::UnloadAsset(asset);
				}
				return;
			}
			const u64 before = probe->GetBakedCubemap().GetInstanceId();
			const u64 after = cube->GetInstanceId();
			if (before == after) {
				AssetDatabase::UnloadAsset(asset);
			} else {
				UndoSystem::Push(MakeUnique<SetRawFieldCommand>(EditTarget(bake.probe), StringHash("_bakedCubemap"), before, after, 8,
																"Bake Reflection Probe"));
			}
			SLT_LOG_INFO("Editor", "Baked reflection probe '{}' into '{}'", probe->GetOwner()->name, bake.path.ToString());
		}

	} // anonymous namespace

	namespace ReflectionProbeBakeOps {

		Path BakePath(Handle<ReflectionProbe> probeHandle) {
			const ReflectionProbe* probe = probeHandle.Get();
			const SceneObject* owner = probe ? probe->GetOwner().Get() : nullptr;
			if (!owner) {
				return {};
			}
			const Path scenePath = EditorSceneOps::GetScenePath(probe->GetScene());
			const str& sceneKey = scenePath.ToString();
			if (sceneKey.empty()) {
				return {};
			}
			const str extension = scenePath.GetExtension();
			const str folder = sceneKey.substr(0, sceneKey.size() - extension.size());

			if (const TextureAsset* current = probe->GetBakedCubemap().Get()) {
				const Path currentPath = AssetDatabase::LookupPath(current->assetId);
				const str& currentKey = currentPath.ToString();
				const bool inFolder = currentPath.GetDirectory() == folder;
				const bool isBake = currentKey.ends_with(str(BAKE_SUFFIX) + str(BAKE_EXTENSION)) ||
									currentKey.find(str(BAKE_SUFFIX) + " ") != str::npos;
				if (inFolder && isBake && !AnotherProbeUses(*probe, current->GetInstanceId())) {
					return currentPath;
				}
			}

			const str stem = folder + "/" + FileSafe(owner->name) + str(BAKE_SUFFIX);
			str candidate = stem + str(BAKE_EXTENSION);
			for (u32 n = 1; FileSystem::Exists(Path(candidate)); ++n) {
				candidate = std::format("{} {}{}", stem, n, BAKE_EXTENSION);
			}
			return Path(candidate);
		}

		Vector<u8> EncodeStrip(std::span<const u8> faces, u32 faceSize) {
			Vector<u8> out{GetMemoryResource(MemTag::Editor)};
			const u64 faceBytes = static_cast<u64>(faceSize) * faceSize * 8;
			const u32 width = faceSize * CUBE_FACE_COUNT;
			if (faceSize == 0 || faces.size() != faceBytes * CUBE_FACE_COUNT || width < 8 || width > 0x7FFF) {
				return out;
			}
			const str header = std::format("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {} +X {}\n", faceSize, width);
			out.insert(out.end(), header.begin(), header.end());

			const u16* halves = reinterpret_cast<const u16*>(faces.data());
			Vector<u8> planes{GetMemoryResource(MemTag::Editor)};
			planes.resize(static_cast<size_t>(width) * 4);
			for (u32 y = 0; y < faceSize; ++y) {
				for (u32 face = 0; face < CUBE_FACE_COUNT; ++face) {
					for (u32 x = 0; x < faceSize; ++x) {
						const u16* texel = halves + ((static_cast<u64>(face) * faceSize + y) * faceSize + x) * 4;
						u8 rgbe[4];
						ToRGBE(Vec3(Math::HalfToFloat(texel[0]), Math::HalfToFloat(texel[1]), Math::HalfToFloat(texel[2])), rgbe);
						const u32 column = face * faceSize + x;
						for (u32 channel = 0; channel < 4; ++channel) {
							planes[channel * width + column] = rgbe[channel];
						}
					}
				}
				out.insert(out.end(), {2, 2, static_cast<u8>(width >> 8), static_cast<u8>(width & 0xFF)});
				// literal runs only, max 128 bytes
				for (u32 channel = 0; channel < 4; ++channel) {
					const u8* plane = planes.data() + static_cast<size_t>(channel) * width;
					for (u32 at = 0; at < width;) {
						const u32 count = Math::Min(width - at, 128u);
						out.push_back(static_cast<u8>(count));
						out.insert(out.end(), plane + at, plane + at + count);
						at += count;
					}
				}
			}
			return out;
		}

	} // namespace ReflectionProbeBakeOps

	namespace ReflectionProbeBake {

		void Initialize() {
			if (!_state) {
				_state = New<BakeState>(MemTag::Editor);
			}
		}

		void Terminate() {
			if (!_state) {
				return;
			}
			ReleaseTargets(_state->inFlight);
			Delete(_state);
			_state = nullptr;
		}

		str WhyNotBakeable(Handle<ReflectionProbe> probeHandle) {
			const ReflectionProbe* probe = probeHandle.Get();
			if (!probe || !probe->GetOwner().IsValid()) {
				return "the probe is gone";
			}
			if (!RenderSystem::IsRenderingAvailable()) {
				return "nothing can render here";
			}
			if (!(probe->GetNearPlane() > 0.0f) || !(probe->GetFarPlane() > probe->GetNearPlane())) {
				return "its clipping planes need 0 < Near < Far";
			}
			if (ReflectionProbeBakeOps::BakePath(probeHandle).ToString().empty()) {
				return "save the scene before baking";
			}
			return {};
		}

		bool Request(Handle<ReflectionProbe> probe) {
			if (!_state) {
				return false;
			}
			const str why = WhyNotBakeable(probe);
			if (!why.empty()) {
				SLT_LOG_WARN("Editor", "Reflection probe not baked: {}", why);
				return false;
			}
			const bool queued = std::find(_state->queue.begin(), _state->queue.end(), probe) != _state->queue.end();
			const bool inFlight = _state->active && _state->inFlight.probe == probe;
			if (!queued && !inFlight) {
				_state->queue.push_back(probe);
			}
			return true;
		}

		void Drain() {
			if (!_state) {
				return;
			}
			if (_state->active) {
				_state->active = false;
				Finish(_state->inFlight);
			}
			while (!_state->active && !_state->queue.empty()) {
				const Handle<ReflectionProbe> next = _state->queue.front();
				_state->queue.erase(_state->queue.begin());
				Start(next);
			}
		}

		u32 GetPendingCount() {
			if (!_state) {
				return 0;
			}
			return static_cast<u32>(_state->queue.size()) + (_state->active ? 1u : 0u);
		}

	} // namespace ReflectionProbeBake

} // namespace Shalltear
