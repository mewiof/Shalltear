#pragma once

#include "Platform/EditorPlatform.h"

#include "Shalltear/Core/Memory.h"

namespace Shalltear::EditorPlatform {

	// double quotes = 1 argument, \" = literal quote
	Vector<str> SplitCommandLine(const str& commandLine);

	bool SpawnArguments(const Vector<str>& arguments, const SpawnOptions& options, SpawnedProcess& out, str& outError);

} // namespace Shalltear::EditorPlatform
