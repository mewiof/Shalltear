#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Scene/Component.h"

namespace Shalltear {

	enum class SkyMode : u8 {
		Cubemap = 0,
		Gradient,
		Color,
		None,
		Material
	};

	enum class AmbientMode : u8 {
		Sky = 0,
		Color
	};

	enum class SkyUpdateMode : u8 {
		OnChanged = 0,
		OnDemand,
		Realtime
	};

	enum class FogMode : u8 {
		Linear = 0,
		Exponential,
		ExponentialSquared
	};

	enum class ExposureMode : u8 {
		Fixed = 0,
		PhysicalCamera
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::SkyMode)
SLT_ENUM_VALUE(Cubemap)
SLT_ENUM_VALUE(Gradient)
SLT_ENUM_VALUE(Color)
SLT_ENUM_VALUE(None)
SLT_ENUM_VALUE(Material)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::AmbientMode)
SLT_ENUM_VALUE(Sky)
SLT_ENUM_VALUE(Color)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::SkyUpdateMode)
SLT_ENUM_VALUE(OnChanged)
SLT_ENUM_VALUE(OnDemand)
SLT_ENUM_VALUE(Realtime)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::FogMode)
SLT_ENUM_VALUE(Linear)
SLT_ENUM_VALUE(Exponential)
SLT_ENUM_VALUE(ExponentialSquared)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::ExposureMode)
SLT_ENUM_VALUE(Fixed)
SLT_ENUM_VALUE(PhysicalCamera)
SLT_END_ENUM()

namespace Shalltear {

	class SHALLTEAR_CORE_API Environment : public Component {
	public:
		SLT_REFLECT_COMPONENT(Environment, Component)
		SLT_REFLECT_FIELD(_isGlobal)
		SLT_REFLECT_FIELD(_size)
		SLT_REFLECT_FIELD(_blendDistance)
		SLT_REFLECT_FIELD(_weight, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(_priority)
		SLT_REFLECT_FIELD(_skyMode)
		SLT_REFLECT_FIELD(_skyCubemap)
		SLT_REFLECT_FIELD(_skyMaterial)
		SLT_REFLECT_FIELD(_skyUpdateMode)
		SLT_REFLECT_FIELD(_skyIntensity, SLT_RANGE(0.0f, 8.0f))
		SLT_REFLECT_FIELD(_skyColor)
		SLT_REFLECT_FIELD(_equatorColor)
		SLT_REFLECT_FIELD(_groundColor)
		SLT_REFLECT_FIELD(_sunDiscSizeDeg, SLT_RANGE(0.0f, 30.0f))
		SLT_REFLECT_FIELD(_sunTint)
		SLT_REFLECT_FIELD(_sunIntensityMultiplier, SLT_RANGE(0.0f, 8.0f))
		SLT_REFLECT_FIELD(_ambientMode)
		SLT_REFLECT_FIELD(_ambientColor)
		SLT_REFLECT_FIELD(_ambientIntensity, SLT_RANGE(0.0f, 8.0f))
		SLT_REFLECT_FIELD(_reflectionIntensity, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(_reflectionResolution)
		SLT_REFLECT_FIELD(_fog)
		SLT_REFLECT_FIELD(_fogColor)
		SLT_REFLECT_FIELD(_fogMode)
		SLT_REFLECT_FIELD(_fogDensity)
		SLT_REFLECT_FIELD(_fogStart)
		SLT_REFLECT_FIELD(_fogEnd)
		SLT_REFLECT_FIELD(_exposureMode)
		SLT_REFLECT_FIELD(_fixedExposure)
		SLT_REFLECT_FIELD(_exposureCompensation)
		SLT_END_REFLECT()

		Environment() = default;
		~Environment() override;

		bool GetIsGlobal() const { return _isGlobal; }
		void SetIsGlobal(bool isGlobal) { _isGlobal = isGlobal; }

		// full extents
		Vec3 GetSize() const { return _size; }
		void SetSize(const Vec3& size) { _size = size; }

		f32 GetBlendDistance() const { return _blendDistance; }
		void SetBlendDistance(f32 distance) { _blendDistance = distance; }

		f32 GetWeight() const { return _weight; }
		void SetWeight(f32 weight) { _weight = weight; }

		f32 GetPriority() const { return _priority; }
		void SetPriority(f32 priority) { _priority = priority; }

		SkyMode GetSkyMode() const { return _skyMode; }
		void SetSkyMode(SkyMode mode) { _skyMode = mode; }

		Handle<TextureAsset> GetSkyCubemap() const { return _skyCubemap; }
		void SetSkyCubemap(Handle<TextureAsset> cubemap) { _skyCubemap = cubemap; }

		Handle<MaterialAsset> GetSkyMaterial() const { return _skyMaterial; }
		void SetSkyMaterial(Handle<MaterialAsset> material) { _skyMaterial = material; }

		SkyUpdateMode GetSkyUpdateMode() const { return _skyUpdateMode; }
		void SetSkyUpdateMode(SkyUpdateMode mode) { _skyUpdateMode = mode; }

		void RequestSkyUpdate() { ++_skyUpdateRequests; }
		u32 GetSkyUpdateRequests() const { return _skyUpdateRequests; }

		f32 GetSkyIntensity() const { return _skyIntensity; }
		void SetSkyIntensity(f32 intensity) { _skyIntensity = intensity; }

		Color GetSkyColor() const { return _skyColor; }
		void SetSkyColor(const Color& color) { _skyColor = color; }

		Color GetEquatorColor() const { return _equatorColor; }
		void SetEquatorColor(const Color& color) { _equatorColor = color; }

		Color GetGroundColor() const { return _groundColor; }
		void SetGroundColor(const Color& color) { _groundColor = color; }

		// angular diameter
		f32 GetSunDiscSizeDeg() const { return _sunDiscSizeDeg; }
		void SetSunDiscSizeDeg(f32 degrees) { _sunDiscSizeDeg = degrees; }

		Color GetSunTint() const { return _sunTint; }
		void SetSunTint(const Color& tint) { _sunTint = tint; }

		f32 GetSunIntensityMultiplier() const { return _sunIntensityMultiplier; }
		void SetSunIntensityMultiplier(f32 multiplier) { _sunIntensityMultiplier = multiplier; }

		AmbientMode GetAmbientMode() const { return _ambientMode; }
		void SetAmbientMode(AmbientMode mode) { _ambientMode = mode; }

		Color GetAmbientColor() const { return _ambientColor; }
		void SetAmbientColor(const Color& color) { _ambientColor = color; }

		f32 GetAmbientIntensity() const { return _ambientIntensity; }
		void SetAmbientIntensity(f32 intensity) { _ambientIntensity = intensity; }

		f32 GetReflectionIntensity() const { return _reflectionIntensity; }
		void SetReflectionIntensity(f32 intensity) { _reflectionIntensity = intensity; }

		i32 GetReflectionResolution() const { return _reflectionResolution; }
		void SetReflectionResolution(i32 resolution) { _reflectionResolution = resolution; }

		bool GetFog() const { return _fog; }
		void SetFog(bool fog) { _fog = fog; }

		Color GetFogColor() const { return _fogColor; }
		void SetFogColor(const Color& color) { _fogColor = color; }

		FogMode GetFogMode() const { return _fogMode; }
		void SetFogMode(FogMode mode) { _fogMode = mode; }

		f32 GetFogDensity() const { return _fogDensity; }
		void SetFogDensity(f32 density) { _fogDensity = density; }

		f32 GetFogStart() const { return _fogStart; }
		void SetFogStart(f32 distance) { _fogStart = distance; }

		f32 GetFogEnd() const { return _fogEnd; }
		void SetFogEnd(f32 distance) { _fogEnd = distance; }

		ExposureMode GetExposureMode() const { return _exposureMode; }
		void SetExposureMode(ExposureMode mode) { _exposureMode = mode; }

		// EV100
		f32 GetFixedExposure() const { return _fixedExposure; }
		void SetFixedExposure(f32 ev100) { _fixedExposure = ev100; }

		f32 GetExposureCompensation() const { return _exposureCompensation; }
		void SetExposureCompensation(f32 stops) { _exposureCompensation = stops; }
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		bool _isGlobal = true;
		Vec3 _size{10.0f, 10.0f, 10.0f};
		f32 _blendDistance = 0.0f;
		f32 _weight = 1.0f;
		f32 _priority = 0.0f;
		SkyMode _skyMode = SkyMode::Gradient;
		Handle<TextureAsset> _skyCubemap;
		Handle<MaterialAsset> _skyMaterial;
		SkyUpdateMode _skyUpdateMode = SkyUpdateMode::OnChanged;
		u32 _skyUpdateRequests = 0;
		f32 _skyIntensity = 1.0f;
		Color _skyColor{0.25f, 0.42f, 0.78f, 1.0f};
		Color _equatorColor{0.62f, 0.70f, 0.80f, 1.0f};
		Color _groundColor{0.10f, 0.09f, 0.08f, 1.0f};
		f32 _sunDiscSizeDeg = 1.0f;
		Color _sunTint = Color::White();
		f32 _sunIntensityMultiplier = 1.0f;
		AmbientMode _ambientMode = AmbientMode::Sky;
		Color _ambientColor{0.2f, 0.2f, 0.2f, 1.0f};
		f32 _ambientIntensity = 1.0f;
		f32 _reflectionIntensity = 1.0f;
		i32 _reflectionResolution = 128;
		bool _fog = false;
		Color _fogColor{0.62f, 0.70f, 0.80f, 1.0f};
		FogMode _fogMode = FogMode::ExponentialSquared;
		f32 _fogDensity = 0.01f;
		f32 _fogStart = 0.0f;
		f32 _fogEnd = 300.0f;
		ExposureMode _exposureMode = ExposureMode::Fixed;
		f32 _fixedExposure = 16.0f;
		f32 _exposureCompensation = 0.0f;
	};

} // namespace Shalltear
