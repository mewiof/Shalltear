#include "Shalltear/PostProcessing/LensFlarePass.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/Light.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/RHIDevice.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include "Shalltear/PostProcessing/LensFlare.h"

#include "LensFlareOps.h"
#include "LensFlareShader.h"

#include <cstring>

namespace Shalltear {

	namespace {

		// must match ShalltearLensFlarePush in LensFlare.hlsl
		struct LensFlarePush {
			u64 base;
			u32 quadOffset;
			f32 invAspect;
		};
		static_assert(sizeof(LensFlarePush) == 16, "push block is an address + 2 words");

		// must match ShalltearFlareSource in LensFlare.hlsl
		struct FlareSource {
			f32 screenDepth[4];
			f32 radius[2];
			u32 samples;
			u32 offScreenVisible;
		};
		static_assert(sizeof(FlareSource) == 32, "shader indexes sources by 32 B");

		// must match ShalltearFlareQuad in LensFlare.hlsl
		struct FlareQuad {
			f32 centerHalf[4];
			f32 color[4];
			f32 shape[4];
			f32 rotation;
			u32 source;
			u32 kindFlags;
			u32 gradient;
		};
		static_assert(sizeof(FlareQuad) == 64, "shader indexes quads by 64 B");

		// must match SHALLTEAR_FLARE_* in LensFlare.hlsl
		constexpr u32 FLAG_INVERSE = 0x100u;
		constexpr u32 FLAG_ANGULAR = 0x200u;
		constexpr u32 FLAG_PREMULTIPLIED = 0x400u;
		constexpr u32 GRADIENT_STEPS = 16;

		constexpr std::string_view TEXTURE_SLOT = "flareTexture";

		RHI::BlendMode ToBlendMode(LensFlareBlend blend) {
			switch (blend) {
			case LensFlareBlend::Premultiplied:
				return RHI::BlendMode::Premultiplied;
			case LensFlareBlend::Lerp:
				return RHI::BlendMode::Alpha;
			case LensFlareBlend::Additive:
				break;
			}
			return RHI::BlendMode::Additive;
		}

		LensFlareOps::FlareInput MakeInput(const LensFlare& flare, const SceneObject& owner) {
			LensFlareOps::FlareInput input;
			input.position = owner.GetWorldPosition();
			input.forward = owner.TransformDirection(Vec3::Forward());
			if (const Light* light = owner.GetComponent<Light>().Get()) {
				input.hasLight = true;
				input.lightType = light->GetLightType();
				input.lightColor = light->GetColor();
				input.spotAngleDeg = light->GetSpotAngleDeg();
				input.innerSpotAngleDeg = light->GetInnerSpotAngleDeg();
			}
			input.intensity = flare.intensity;
			input.scale = flare.scale;
			input.attenuationByLightShape = flare.attenuationByLightShape;
			input.attenuationDistance = flare.attenuationDistance;
			input.attenuationDistanceCurve = &flare.attenuationDistanceCurve;
			input.scaleDistance = flare.scaleDistance;
			input.scaleDistanceCurve = &flare.scaleDistanceCurve;
			input.screenAttenuationCurve = &flare.screenAttenuationCurve;
			input.occlusion = flare.occlusion;
			input.occlusionRadius = flare.occlusionRadius;
			input.sampleCount = flare.sampleCount;
			input.occlusionOffset = flare.occlusionOffset;
			input.allowOffScreen = flare.allowOffScreen;
			return input;
		}

		const SceneObject* DrawnOwner(const LensFlare& flare) {
			const SceneObject* owner = flare.GetOwner().Get();
			const Scene* scene = owner ? owner->GetScene().Get() : nullptr;
			if (!scene || scene->GetKind() != SceneKind::World) {
				return nullptr;
			}
			const LensFlareAsset* asset = flare.flare.Get();
			return asset && !asset->elements.empty() ? owner : nullptr;
		}

	} // anonymous namespace

	struct LensFlarePass::Materials {
		struct Entry {
			u64 textureId = 0;
			Handle<TextureAsset> texture;
			RHI::BlendMode blend = RHI::BlendMode::Additive;
			MaterialAsset* material = nullptr;
		};
		Vector<Entry> entries{GetMemoryResource(MemTag::Graphics)};

		~Materials() {
			for (Entry& entry : entries) {
				Delete(entry.material);
			}
		}

		void Prune() {
			for (size_t i = 0; i < entries.size();) {
				if (entries[i].textureId != 0 && !entries[i].texture.Get()) {
					Delete(entries[i].material);
					entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
				} else {
					++i;
				}
			}
		}

		MaterialAsset* Find(ShaderAsset& shader, TextureAsset* texture, RHI::BlendMode blend) {
			const u64 textureId = texture ? texture->GetInstanceId() : 0;
			for (Entry& entry : entries) {
				if (entry.textureId == textureId && entry.blend == blend) {
					return entry.material;
				}
			}
			MaterialAsset* material = New<MaterialAsset>(MemTag::Graphics);
			material->shader = Handle<ShaderAsset>(&shader);
			Vector<str> slots{GetMemoryResource(MemTag::Graphics)};
			shader.GetMaterialTextureSlots(slots);
			material->SyncToLayout(shader.GetMaterialParamLayout(), {slots.data(), slots.size()},
								   {shader.memberDefaults.data(), shader.memberDefaults.size()});
			material->blend = blend;
			if (texture) {
				for (MaterialAsset::TextureParam& param : material->textures) {
					if (param.name == TEXTURE_SLOT) {
						param.texture = texture->assetId;
						param.resolved = Handle<TextureAsset>(texture);
					}
				}
			}
			entries.push_back(Entry{textureId, Handle<TextureAsset>(texture), blend, material});
			return material;
		}
	};

	LensFlarePass::LensFlarePass() {
		point = PassPoint::BeforePost;
		_materials = MakeUnique<Materials>();
	}

	LensFlarePass::~LensFlarePass() = default;

	PassRequests LensFlarePass::GetRequests() const {
		const u32 count = LensFlare::GetEnabledCount();
		for (u32 index = 0; index < count; ++index) {
			const LensFlare* flare = LensFlare::GetEnabled(index);
			if (flare && flare->occlusion && DrawnOwner(*flare)) {
				return PassRequests::SceneDepth;
			}
		}
		return PassRequests::None;
	}

	bool LensFlarePass::Cook(CookWriter& descriptor, CookWriter& payload) const {
		if (!RenderPass::Cook(descriptor, payload)) {
			return false;
		}
		descriptor.NoteAssetRef(LensFlareImpl::LENS_FLARE_SHADER);
		return true;
	}

	void LensFlarePass::Record(PassContext& context) {
		const u32 flareCount = LensFlare::GetEnabledCount();
		if (flareCount == 0) {
			return;
		}

		if (!_shaderResolved) {
			_shaderResolved = true;
			if (Asset* asset = AssetDatabase::LoadAssetByGuid(LensFlareImpl::LENS_FLARE_SHADER)) {
				if (asset->GetAssetTypeId() == StringHash("ShaderAsset")) {
					_shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(asset));
				} else {
					AssetDatabase::UnloadAsset(asset);
				}
			}
		}
		ShaderAsset* shader = _shader.Get();
		if (!shader) {
			if (!_warnedNoShader) {
				_warnedNoShader = true;
				SLT_LOG_WARN("PostProcessing", "'{}' can't resolve engine://Shaders/LensFlare.hlsl (row draws nothing)", virtualPath);
			}
			return;
		}

		const Vec2I extent = context.GetExtent();
		if (extent.x <= 0 || extent.y <= 0) {
			return;
		}
		const f32 aspect = static_cast<f32>(extent.x) / static_cast<f32>(extent.y);

		LensFlareOps::ViewInput view;
		view.view = context.GetView();
		view.projection = context.GetProjection();
		view.cameraPosition = context.GetCameraPosition();
		view.extent = extent;

		_materials->Prune();

		Vector<FlareSource> sources{GetMemoryResource(MemTag::Graphics)};
		Vector<FlareQuad> quads{GetMemoryResource(MemTag::Graphics)};
		Vector<MaterialAsset*> quadMaterials{GetMemoryResource(MemTag::Graphics)};
		Vector<Color> gradients{GetMemoryResource(MemTag::Graphics)};
		Vector<u32> gradientOf{GetMemoryResource(MemTag::Graphics)};
		Vector<LensFlareOps::Quad> expanded{GetMemoryResource(MemTag::Graphics)};

		for (u32 index = 0; index < flareCount; ++index) {
			const LensFlare* flare = LensFlare::GetEnabled(index);
			const SceneObject* owner = flare ? DrawnOwner(*flare) : nullptr;
			if (!owner) {
				continue;
			}
			LensFlareOps::ProjectedFlare projected;
			if (!LensFlareOps::Project(view, MakeInput(*flare, *owner), projected)) {
				continue;
			}

			const u32 sourceIndex = static_cast<u32>(sources.size());
			FlareSource& source = sources.emplace_back();
			source.screenDepth[0] = projected.screen.x;
			source.screenDepth[1] = projected.screen.y;
			source.screenDepth[2] = projected.depth;
			source.screenDepth[3] = projected.depthSign;
			source.radius[0] = projected.occlusionRadius.x;
			source.radius[1] = projected.occlusionRadius.y;
			source.samples = projected.samples;
			source.offScreenVisible = projected.offScreenVisible ? 1u : 0u;

			for (const LensFlareElement& element : flare->flare.Get()->elements) {
				TextureAsset* texture = nullptr;
				f32 textureAspect = 0.0f;
				if (element.shape == LensFlareShape::Image) {
					texture = element.texture.Get();
					if (!texture) {
						continue;
					}
					if (texture->size.y > 0) {
						textureAspect = static_cast<f32>(texture->size.x) / static_cast<f32>(texture->size.y);
					}
				}

				expanded.clear();
				LensFlareOps::ExpandElement(element, projected, aspect, textureAspect, expanded);
				if (expanded.empty()) {
					continue;
				}

				u32 gradient = 0;
				if (element.colorMode != LensFlareColorMode::Constant) {
					gradient = static_cast<u32>(gradientOf.size()) + 1;
					gradientOf.push_back(static_cast<u32>(gradients.size()));
					for (u32 step = 0; step < GRADIENT_STEPS; ++step) {
						gradients.push_back(element.tintGradient.Evaluate(static_cast<f32>(step) / static_cast<f32>(GRADIENT_STEPS - 1)));
					}
				}

				u32 kindFlags = static_cast<u32>(element.shape);
				if (element.inverse) {
					kindFlags |= FLAG_INVERSE;
				}
				if (element.colorMode == LensFlareColorMode::Angular) {
					kindFlags |= FLAG_ANGULAR;
				}
				if (element.blend == LensFlareBlend::Premultiplied) {
					kindFlags |= FLAG_PREMULTIPLIED;
				}
				const f32 shapeSize = element.shape == LensFlareShape::Ring ? Math::Clamp(element.ringThickness, 0.01f, 1.0f)
																			: static_cast<f32>(Math::Clamp(element.sideCount, 3, 32));

				MaterialAsset* material = _materials->Find(*shader, texture, ToBlendMode(element.blend));
				for (const LensFlareOps::Quad& from : expanded) {
					FlareQuad& quad = quads.emplace_back();
					quad.centerHalf[0] = from.center.x;
					quad.centerHalf[1] = from.center.y;
					quad.centerHalf[2] = from.halfSize.x;
					quad.centerHalf[3] = from.halfSize.y;
					quad.color[0] = from.color.r;
					quad.color[1] = from.color.g;
					quad.color[2] = from.color.b;
					quad.color[3] = from.color.a;
					quad.shape[0] = Math::Clamp01(element.gradient);
					quad.shape[1] = LensFlareOps::FalloffExponent(element.falloff);
					quad.shape[2] = shapeSize;
					quad.shape[3] = Math::Clamp01(element.roundness);
					quad.rotation = from.rotation;
					quad.source = sourceIndex;
					quad.kindFlags = kindFlags;
					quad.gradient = gradient;
					quadMaterials.push_back(material);
				}
			}
		}

		if (quads.empty()) {
			return;
		}
		RHI::Device* device = RenderSystem::GetDevice();
		if (!device) {
			return;
		}

		const u64 sourceBytes = sources.size() * sizeof(FlareSource);
		const u64 quadBytes = quads.size() * sizeof(FlareQuad);
		const u64 gradientBytes = gradients.size() * sizeof(f32) * 4;
		const RHI::TransientAllocation allocation = device->AllocateTransient(sourceBytes + quadBytes + gradientBytes, 16);
		if (!allocation.IsValid()) {
			return;
		}
		u8* cpu = static_cast<u8*>(allocation.cpu);
		std::memcpy(cpu, sources.data(), sourceBytes);
		const u64 gradientStart = sourceBytes + quadBytes;
		for (FlareQuad& quad : quads) {
			if (quad.gradient != 0) {
				quad.gradient = static_cast<u32>(gradientStart + gradientOf[quad.gradient - 1] * sizeof(f32) * 4);
			}
		}
		std::memcpy(cpu + sourceBytes, quads.data(), quadBytes);
		f32* tones = reinterpret_cast<f32*>(cpu + gradientStart);
		for (const Color& tone : gradients) {
			*tones++ = tone.r;
			*tones++ = tone.g;
			*tones++ = tone.b;
			*tones++ = tone.a;
		}

		LensFlarePush push{};
		push.base = device->GetBufferDeviceAddress(allocation.buffer) + allocation.offset;
		push.invAspect = 1.0f / aspect;
		for (size_t first = 0; first < quads.size();) {
			size_t last = first + 1;
			while (last < quads.size() && quadMaterials[last] == quadMaterials[first]) {
				++last;
			}
			push.quadOffset = static_cast<u32>(sourceBytes + first * sizeof(FlareQuad));
			context.DrawProcedural(Handle<MaterialAsset>(quadMaterials[first]),
								   std::span<const u8>(reinterpret_cast<const u8*>(&push), sizeof(push)),
								   6, static_cast<u32>(last - first));
			first = last;
		}
	}

} // namespace Shalltear
