#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Scene/Behavior.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/VisualEffects/VisualEffect.h"
#include "Shalltear/VisualEffects/VisualEffectListener.h"

#include <span>
#include <string_view>

using namespace Shalltear;

namespace VisualEffectsScriptModuleImpl {

	class ParticleEventProbe : public Behavior, public VisualEffectListener {
	public:
		SLT_REFLECT_COMPONENT(ParticleEventProbe, Behavior)
		SLT_END_REFLECT()

		u32 heard = 0;
		f32 counted = 0.0f;
		str lastName;
		Vec4 lastPosition{};

		void OnVisualEffectOutputEvent(VisualEffect& effect, std::string_view name, const VisualEffectEventAttributes& attributes) override {
			(void)effect;
			++heard;
			lastName = str(name);
			attributes.Find("position", lastPosition);
			Vec4 count;
			counted += attributes.Find("spawnCount", count) ? count.x : 0.0f;
		}
	};

} // namespace VisualEffectsScriptModuleImpl

SLT_REGISTER_TYPE(VisualEffectsScriptModuleImpl::ParticleEventProbe)

extern "C" __declspec(dllexport) u32 VisualEffectsScriptModuleCount(VisualEffect* effect, const char* system) {
	return effect && system ? effect->GetParticleCount(system) : 0u;
}

extern "C" __declspec(dllexport) u32 VisualEffectsScriptModuleReadPositions(VisualEffect* effect, const char* system, f32* xyz, u32 capacity) {
	if (!effect || !system || !xyz) {
		return 0;
	}
	Vec3 positions[64];
	const u32 read = effect->GetParticles(system, "position", std::span<Vec3>(positions, capacity < 64 ? capacity : 64));
	for (u32 i = 0; i < read; ++i) {
		xyz[3 * i] = positions[i].x;
		xyz[3 * i + 1] = positions[i].y;
		xyz[3 * i + 2] = positions[i].z;
	}
	return read;
}

extern "C" __declspec(dllexport) u32 VisualEffectsScriptModuleHeard(Component* probe, char* name, u32 room, f32* position, f32* counted) {
	const auto* heard = dynamic_cast<const VisualEffectsScriptModuleImpl::ParticleEventProbe*>(probe);
	if (!heard || !name || room == 0 || !position || !counted) {
		return 0;
	}
	const size_t length = heard->lastName.size() < room - 1 ? heard->lastName.size() : room - 1;
	for (size_t i = 0; i < length; ++i) {
		name[i] = heard->lastName[i];
	}
	name[length] = '\0';
	position[0] = heard->lastPosition.x;
	position[1] = heard->lastPosition.y;
	position[2] = heard->lastPosition.z;
	*counted = heard->counted;
	return heard->heard;
}

SLT_IMPLEMENT_MODULE(VisualEffectsScriptModule)
