#include "AudioSettings.h"

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			AudioSettings _fallback;

			AudioSettings* _declared = nullptr;

		} // anonymous namespace

		AudioSettings& GetSettings() { return _declared ? *_declared : _fallback; }

		AudioSettings* GetDeclaredSettings() { return _declared; }

		void SetDeclaredSettings(AudioSettings* declared) { _declared = declared; }

	} // namespace AudioImpl

} // namespace Shalltear
