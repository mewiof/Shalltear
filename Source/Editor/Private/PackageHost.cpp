#include "PackageHost.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"

#include "Shalltear/Editor/EditorExtensions.h"
#include "Shalltear/Editor/PackageOps.h"
#include "Shalltear/Editor/ScriptProjectOps.h"

#include <algorithm>
#include <filesystem>

namespace Shalltear {

	namespace {

		bool RegisterLinkedPackage(std::string_view name) {
			size_t count = 0;
			const EditorLinkedPackage* table = GetEditorLinkedPackages(count);
			for (size_t index = 0; index < count; ++index) {
				if (name == table[index].name) {
					table[index].registerFn(&table[index]);
					return true;
				}
			}
			return false;
		}

		PackageHost* _activePackageHost = nullptr;

	} // anonymous namespace

	PackageHost* PackageHost::GetActive() {
		return _activePackageHost;
	}

	void PackageHost::Initialize() {
		if (!_activePackageHost) {
			_activePackageHost = this;
		}

		Vector<PackageOps::PackageManifest> catalogue;
		Vector<str> diagnostics;
		PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);
		for (const str& diagnostic : diagnostics) {
			SLT_LOG_WARN("Editor", "package catalogue: {}", diagnostic);
		}
		if (catalogue.empty()) {
			return;
		}

		// must match the player's link line resolution
		PackageOps::UpgradeStoredSet(catalogue);
		Vector<str> stored;
		PackageOps::ReadEnabledSet(stored);
		Vector<PackageOps::ResolvedPackage> resolution;
		PackageOps::ResolveEnabledSet(catalogue, stored, resolution);

		for (const PackageOps::ResolvedPackage& resolved : resolution) {
			_sessionEnabled.push_back(resolved.name);
		}

		str executablePath = ScriptProjectOps::GetEditorExecutablePath();
		std::filesystem::path binaryRoot = std::filesystem::path(executablePath).parent_path();

		for (const PackageOps::ResolvedPackage& resolved : resolution) {
			const PackageOps::PackageManifest* manifest = PackageOps::FindPackage(catalogue, resolved.name);
			if (!manifest) {
				continue;
			}
			if (manifest->kind == PackageOps::PackageKind::Game) {
				continue;
			}

			str binaryName = PackageOps::GetPackageBinaryName(*manifest);
			if (binaryName.empty()) {
				continue;
			}
			std::filesystem::path binary = binaryRoot / (binaryName + ".dll");
			if (!std::filesystem::exists(binary)) {
				if (RegisterLinkedPackage(resolved.name)) {
					_names.push_back(resolved.name);
					SLT_LOG_INFO("Editor", "package {} {} linked ({})",
								 resolved.name, manifest->version, PackageOps::KindName(manifest->kind));
					continue;
				}
				SLT_LOG_INFO("Editor", "package {} has no loadable binary here ({})",
							 resolved.name, binary.generic_string());
				continue;
			}

			ModuleHandle handle = ModuleSystem::Load(Path(binary.generic_string()), ModuleLoadPolicy::InPlace);
			if (!ModuleSystem::IsLoaded(handle)) {
				SLT_LOG_ERROR("Editor", "package {} didn't load", resolved.name);
				continue;
			}

			_modules.push_back(handle);
			_names.push_back(resolved.name);
			SLT_LOG_INFO("Editor", "package {} {} loaded ({})",
						 resolved.name, manifest->version, PackageOps::KindName(manifest->kind));

			if (const void* base = ModuleSystem::GetNativeHandle(handle)) {
				_extensionBases.push_back(base);
			}
		}

		Vector<PackageOps::ResolvedPackage> loaded;
		for (const PackageOps::ResolvedPackage& resolved : resolution) {
			if (std::find(_names.begin(), _names.end(), resolved.name) != _names.end()) {
				loaded.push_back(resolved);
			}
		}
		PackageOps::SelectGamePackages(catalogue, loaded, _gamePackages);
	}

	u32 PackageHost::LoadEditorExtensions() {
		u32 loaded = 0;
		for (const void* base : _extensionBases) {
			if (EditorExtensions::Load(base)) {
				++loaded;
			}
		}
		return loaded;
	}

	void PackageHost::Terminate() {
		// editor registries are swept before ModuleSystem unmaps the images
		for (const void* base : _extensionBases) {
			EditorExtensions::Unload(base);
		}
		_extensionBases.clear();

		_modules.clear();
		_names.clear();
		_sessionEnabled.clear();
		_gamePackages.clear();
		if (_activePackageHost == this) {
			_activePackageHost = nullptr;
		}
	}

} // namespace Shalltear
