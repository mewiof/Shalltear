#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Module/ModuleSystem.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct EditorLinkedPackage {
		const char* name;
		void (*registerFn)(const void* owner);
	};

	const EditorLinkedPackage* GetEditorLinkedPackages(size_t& outCount);

	class PackageHost {
	public:
		void Initialize();

		u32 LoadEditorExtensions();

		void Terminate();

		const Vector<str>& GetLoadedNames() const { return _names; }

		static PackageHost* GetActive();

		const Vector<str>& GetSessionEnabledNames() const { return _sessionEnabled; }

		const Vector<str>& GetGamePackageNames() const { return _gamePackages; }
	private:
		Vector<ModuleHandle> _modules;

		Vector<const void*> _extensionBases;
		Vector<str> _names;
		Vector<str> _sessionEnabled;
		Vector<str> _gamePackages;
	};

} // namespace Shalltear
