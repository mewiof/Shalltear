#include "TestsPCH.h"

#include "TestHelperProcess.h"

#include "Shalltear/Core/Asset/ImportTier.h"

#include "ImportPipeline.h"
#include "Graphs.h"
#include "NodeShader.h"

namespace {

	const int _IMPORT_PIPELINE_TOKEN = 0;
	const int _NODE_SHADER_TOKEN = 0;
	const int _GRAPHS_TOKEN = 0;

	class ImportPipelineEnvironment : public ::testing::Environment {
	public:
		void SetUp() override {
			Shalltear::RegisterImportPipeline(&_IMPORT_PIPELINE_TOKEN);
			Shalltear::RegisterGraphs(&_GRAPHS_TOKEN);
			Shalltear::RegisterNodeShader(&_NODE_SHADER_TOKEN);
		}
	};

	class PendingTypeRegistrationEnvironment : public ::testing::Environment {
	public:
		void SetUp() override {
			Shalltear::TypeRegistry::RegisterPendingTypes();
		}
	};

} // anonymous namespace

void Shalltear::TestHelpers::RestoreNodeShader() {
	Shalltear::RegisterNodeShader(&_NODE_SHADER_TOKEN);
}

int main(int argc, char** argv) {
#ifdef _WIN32
	for (int arg = 1; arg < argc; ++arg) {
		if (std::strcmp(argv[arg], Shalltear::TestHelpers::WINDOW_HOST_FLAG) == 0) {
			return Shalltear::TestHelpers::RunWindowHost();
		}
		if (std::strcmp(argv[arg], Shalltear::TestHelpers::SHOW_STATE_FLAG) == 0) {
			return Shalltear::TestHelpers::RunShowStateProbe();
		}
		if (std::strcmp(argv[arg], Shalltear::TestHelpers::DATA_ASSET_RELOAD_FLAG) == 0) {
			return Shalltear::TestHelpers::RunDataAssetReloadProbe(arg + 1 < argc ? argv[arg + 1] : nullptr);
		}
		if (std::strcmp(argv[arg], Shalltear::TestHelpers::REFLECTION_STARTUP_FLAG) == 0) {
			return Shalltear::TestHelpers::RunReflectionStartupProbe();
		}
	}
#endif

	::testing::InitGoogleTest(&argc, argv);
	Shalltear::ImportTier::Install();
	::testing::AddGlobalTestEnvironment(new PendingTypeRegistrationEnvironment());
	::testing::AddGlobalTestEnvironment(new ImportPipelineEnvironment());
	return RUN_ALL_TESTS();
}
