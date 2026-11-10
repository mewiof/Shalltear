#pragma once

#include "Shalltear/Core/Window.h"

namespace WindowTestSupport {

	inline Shalltear::WindowDesc HiddenDesc(Shalltear::Vec2I size = {320, 240}) {
		Shalltear::WindowDesc desc;
		desc.title = "WindowTest";
		desc.clientSize = size;
		desc.flags = Shalltear::WindowFlags::Resizable | Shalltear::WindowFlags::Hidden;
		return desc;
	}

} // namespace WindowTestSupport

class WindowTest : public ::testing::Test {
protected:
	void SetUp() override {
		Shalltear::WindowSystem::Initialize();
	}
	void TearDown() override {
		Shalltear::WindowSystem::Terminate();
	}
};
