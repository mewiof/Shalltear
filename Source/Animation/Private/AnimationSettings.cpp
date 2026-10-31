#include "AnimationSettings.h"

namespace Shalltear {

	namespace AnimationImpl {

		namespace {

			const AnimationSettings _Fallback;

			const AnimationSettings* _declared = nullptr;

		} // anonymous namespace

		const AnimationSettings& GetSettings() { return _declared ? *_declared : _Fallback; }

		void SetDeclaredSettings(const AnimationSettings* declared) { _declared = declared; }

	} // namespace AnimationImpl

} // namespace Shalltear
