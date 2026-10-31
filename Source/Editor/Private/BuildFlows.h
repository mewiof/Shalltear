#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

namespace Shalltear::BuildFlows {

	void GetUnsavedWork(Vector<str>& out);

	void RequestBuild(UI::Context& context, const Vector<str>& profiles, bool runAfterBuild);

	void PumpMainThread(UI::Context* context);

	bool IsRunPending();

} // namespace Shalltear::BuildFlows
