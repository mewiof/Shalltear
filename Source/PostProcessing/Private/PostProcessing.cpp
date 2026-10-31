#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/PostProcessing/AmbientOcclusionPass.h"
#include "Shalltear/PostProcessing/PostProcessPass.h"
#include "Shalltear/PostProcessing/PostProcessProfile.h"
#include "Shalltear/PostProcessing/PostProcessVolume.h"

namespace Shalltear {

	void RegisterPostProcessProfileImporter(const void* owner);
	void ReleasePostProcessVolumeList();

	namespace PostProcessingImpl {

		constexpr const char* PACKAGE_DISPLAY_NAME = "Post Processing";

		void RegisterPackage(const void* owner) {
			// static player drops types without these
			PostProcessVolume::GetReflectType();
			PostProcessProfile::GetReflectType();
			PostProcessPass::GetReflectType();
			AmbientOcclusionPass::GetReflectType();

			RegisterPostProcessProfileImporter(owner);

			SLT_LOG_INFO("PostProcessing", "{} up: volumes, profiles, and 2 pass rows", PACKAGE_DISPLAY_NAME);
		}

		void ShutdownPackage() {
			ReleasePostProcessVolumeList();
		}

	} // namespace PostProcessingImpl

} // namespace Shalltear

SLT_IMPLEMENT_PACKAGE_WITH_SHUTDOWN(PostProcessing,
									&Shalltear::PostProcessingImpl::RegisterPackage,
									&Shalltear::PostProcessingImpl::ShutdownPackage)
