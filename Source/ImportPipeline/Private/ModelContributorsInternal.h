#pragma once

#include "Shalltear/ImportPipeline/ModelContributors.h"

namespace Shalltear {

	namespace ModelContributors {

		SHALLTEAR_IMPORTPIPELINE_API u64 Fingerprint();

		SHALLTEAR_IMPORTPIPELINE_API bool AnyWantsTracks();

		SHALLTEAR_IMPORTPIPELINE_API void Invoke(const ModelContribution& prototype);

	} // namespace ModelContributors

} // namespace Shalltear
