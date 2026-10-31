#include "ImportTierInternal.h"

namespace Shalltear {

	namespace {

		const ImportTierTable* _installed = nullptr;

	} // anonymous namespace

	namespace ImportTierInternal {

		const ImportTierTable* Get() {
			return _installed;
		}

		void Set(const ImportTierTable* table) {
			_installed = table;
		}

	} // namespace ImportTierInternal

} // namespace Shalltear
