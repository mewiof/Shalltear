#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Types.h"

namespace SampleDependentPackageImpl {

	using namespace Shalltear;

	constexpr const char* PACKAGE_MARKER = "SHALLTEAR_DEPENDENT_PACKAGE_PROBE";

	void RegisterPackage(const void* owner) {
		(void)owner;
		SLT_LOG_INFO("SampleDependentPackage", "{} registered", PACKAGE_MARKER);
	}

} // namespace SampleDependentPackageImpl

SLT_IMPLEMENT_PACKAGE(SampleDependentPackage, &SampleDependentPackageImpl::RegisterPackage)
