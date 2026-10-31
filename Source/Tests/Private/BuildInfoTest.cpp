#include "TestsPCH.h"

#include "Shalltear/Core/BuildInfo.h"

#include "Shalltear/Editor/CppToolchain.h"
#include "Shalltear/Editor/EngineVersionOps.h"

using namespace Shalltear;

TEST(BuildInfoTest, VersionStringIsRealSemanticVersion) {
	const char* version = BuildInfo::GetVersionString();
	ASSERT_NE(version, nullptr);

	std::string_view v(version);
	ASSERT_FALSE(v.empty());
	EXPECT_TRUE(v.find_first_not_of("0123456789.") == std::string_view::npos) << version;
	EXPECT_TRUE(v[0] >= '0' && v[0] <= '9');
}

TEST(BuildInfoTest, GitCommitIsPresent) {
	const char* commit = BuildInfo::GetGitCommit();
	ASSERT_NE(commit, nullptr);

	std::string_view c(commit);
	ASSERT_FALSE(c.empty());
	if (c != "unknown") {
		EXPECT_TRUE(c.find_first_not_of("0123456789abcdef") == std::string_view::npos) << commit;
	}
}

TEST(BuildInfoTest, BuildConfigAndTimestampArePresent) {
	const char* config = BuildInfo::GetBuildConfig();
	ASSERT_NE(config, nullptr);
	EXPECT_GT(std::string_view(config).size(), 0u);

	const char* timestamp = BuildInfo::GetBuildTimestamp();
	ASSERT_NE(timestamp, nullptr);
	EXPECT_EQ(std::string_view(timestamp).size(), 20u);
}

TEST(BuildInfoTest, EngineHashStableFromIdentity) {
	u64 hash = BuildInfo::GetEngineHash();
	EXPECT_NE(hash, 0u);
	EXPECT_EQ(hash, BuildInfo::GetEngineHash());

	str identity = str(BuildInfo::GetVersionString()) + "|" + BuildInfo::GetGitCommit();
	EXPECT_EQ(hash, StringHash(identity).GetValue());
}

TEST(BuildInfoTest, HumanLabelIsNotMachineIdentity) {
	const str label = EngineVersionOps::DisplayLabel();
	const str tooling = BuildInfo::GetVersionString();

	EXPECT_FALSE(label.empty());
	EXPECT_NE(label, tooling);
	EXPECT_EQ(label.find(tooling), str::npos) << label;

	EXPECT_EQ(CppToolchain::GetEngineIdentity(), tooling + "|" + BuildInfo::GetGitCommit());
}

TEST(BuildInfoTest, ReleaseLabelDoesNotCarryCommit) {
	const str release = EngineVersionOps::ReleaseLabel();
	const str commit = BuildInfo::GetGitCommit();

	EXPECT_FALSE(release.empty());
	EXPECT_EQ(release.find(commit), str::npos) << release;

	EXPECT_NE(EngineVersionOps::DisplayLabel().find(commit), str::npos);
}
