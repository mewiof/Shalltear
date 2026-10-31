#include "TestsPCH.h"

#include "Graphics/PresentPolicy.h"

#include "Shalltear/Core/Window.h"

using namespace Shalltear;

TEST(PresentPolicyTest, VsyncOnIsAlwaysFifo) {
	EXPECT_EQ(RHI::ChoosePresentPolicy(true, false), RHI::PresentPolicy::Fifo);
	EXPECT_EQ(RHI::ChoosePresentPolicy(true, true), RHI::PresentPolicy::Fifo);
}

TEST(PresentPolicyTest, VsyncOffTakesMailbox) {
	EXPECT_EQ(RHI::ChoosePresentPolicy(false, true), RHI::PresentPolicy::Mailbox);
}

TEST(PresentPolicyTest, VsyncOffFallsBackToFifo) {
	EXPECT_EQ(RHI::ChoosePresentPolicy(false, false), RHI::PresentPolicy::Fifo);
}

class PresentDefaultTest : public ::testing::Test {
protected:
	void SetUp() override { WindowSystem::Initialize(); }
	void TearDown() override { WindowSystem::Terminate(); }
};

TEST_F(PresentDefaultTest, AWindowIsBornAskingForVsync) {
	WindowDesc desc;
	desc.title = "PresentDefaultTest";
	desc.clientSize = {320, 240};
	desc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(desc);
	ASSERT_TRUE(window.IsValid());
	EXPECT_TRUE(window.Get()->GetVSyncHint());

	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}
