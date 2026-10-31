#pragma shalltear_template sky

#pragma shalltear_default zenith 0.2 0.38 0.75 1
#pragma shalltear_default horizon 0.62 0.72 0.85 1
#pragma shalltear_default ground 0.16 0.15 0.14 1
#pragma shalltear_default sunset 1 0.45 0.15 1
#pragma shalltear_default haze 0.35
#pragma shalltear_default glow 0.25
#pragma shalltear_default glowFocus 0.8
#pragma shalltear_default night 0.05

#pragma shalltear_range haze 0.02 1
#pragma shalltear_range glow 0 4
#pragma shalltear_range glowFocus 0 0.98
#pragma shalltear_range night 0 1

#pragma shalltear_hint zenith hdr
#pragma shalltear_hint horizon hdr
#pragma shalltear_hint ground hdr
#pragma shalltear_hint sunset hdr

[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {
	float4 zenith;
	float4 horizon;
	float4 ground;
	float4 sunset;
	float haze;
	float glow;
	float glowFocus;
	float night;
};

float3 SkyAboveHorizon(float3 direction, float3 sun, float daylight, float dusk) {
	float band = exp(-max(direction.y, 0.0) / max(haze, 0.001));
	float3 color = lerp(zenith.rgb, horizon.rgb, band);
	float lengths = sqrt(dot(direction.xz, direction.xz) * dot(sun.xz, sun.xz));
	float facing = lengths > 1.0e-6 ? dot(direction.xz, sun.xz) / lengths * 0.5 + 0.5 : 0.5;
	color = lerp(color, sunset.rgb, saturate(dusk * band * facing * facing));
	return color * lerp(night, 1.0, daylight);
}

float3 SunGlow(float3 direction, ShalltearSkyInput input) {
	if (!input.sunEnabled) {
		return float3(0.0, 0.0, 0.0);
	}
	float3 color = input.sunColor;
	float peak = max(color.r, max(color.g, color.b));
	float focus = clamp(glowFocus, 0.0, 0.98);
	float spread = max(1.0 + focus * focus - 2.0 * focus * dot(direction, input.sunDirectionWS), 1.0e-4);
	float lobe = (1.0 - focus * focus) / (spread * sqrt(spread)) * 0.0795775;
	return color / peak * (glow * lobe * smoothstep(-0.1, 0.05, input.sunDirectionWS.y));
}

void ShalltearSkyFunction(ShalltearSkyInput input, inout ShalltearSkyOutput sky) {
	float3 direction = input.directionWS;
	float3 sun = input.sunDirectionWS;
	float daylight = smoothstep(-0.15, 0.2, sun.y);
	float dusk = (1.0 - smoothstep(0.0, 0.4, sun.y)) * smoothstep(-0.2, 0.0, sun.y);

	if (direction.y >= 0.0) {
		sky.color = SkyAboveHorizon(direction, sun, daylight, dusk) + SunGlow(direction, input) + ShalltearSkySunDisc(input);
		return;
	}
	float across = length(direction.xz);
	float3 level = across > 1.0e-6 ? float3(direction.x / across, 0.0, direction.z / across) : float3(0.0, 0.0, 1.0);
	float3 edge = SkyAboveHorizon(level, sun, daylight, dusk) + SunGlow(level, input);
	float3 below = ground.rgb * lerp(night, 1.0, daylight);
	sky.color = lerp(below, edge, smoothstep(-0.03, 0.0, direction.y));
}
