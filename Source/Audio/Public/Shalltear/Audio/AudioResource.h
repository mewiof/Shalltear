#pragma once

#include "Shalltear/Audio/AudioAPI.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Reflection/Reflect.h"

namespace Shalltear {

	class SHALLTEAR_AUDIO_API AudioResource : public Asset {
	public:
		SLT_REFLECT_CLASS(AudioResource)
		SLT_REFLECT_PARENT(Asset)
		builder.SetFlags(::Shalltear::TypeFlagAbstract);
		SLT_END_REFLECT()

		~AudioResource() override = default;
	protected:
		AudioResource() = default;
	};

} // namespace Shalltear
