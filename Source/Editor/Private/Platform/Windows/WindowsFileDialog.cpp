#include "Platform/EditorPlatform.h"

#include "Shalltear/Core/StringUtil.h"
#include "Shalltear/Editor/EditorDialogs.h"

#include <filesystem>
#include <format>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

namespace Shalltear::EditorPlatform {

	namespace {

		void CollectPaths(IFileOpenDialog* dialog, Vector<str>& out) {
			IShellItemArray* items = nullptr;
			if (FAILED(dialog->GetResults(&items))) {
				return;
			}
			DWORD count = 0;
			items->GetCount(&count);
			for (DWORD i = 0; i < count; ++i) {
				IShellItem* item = nullptr;
				if (FAILED(items->GetItemAt(i, &item))) {
					continue;
				}
				PWSTR path = nullptr;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
					out.push_back(StringUtil::WideToUTF8(path));
					::CoTaskMemFree(path);
				}
				item->Release();
			}
			items->Release();
		}

		HRESULT ShowDialog(const EditorDialogs::FileDialogDesc& desc, Vector<str>& outPaths) {
			IFileOpenDialog* dialog = nullptr;
			HRESULT result = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
			if (FAILED(result)) {
				return result;
			}
			FILEOPENDIALOGOPTIONS options = 0;
			dialog->GetOptions(&options);
			options |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
			if (desc.multiple) {
				options |= FOS_ALLOWMULTISELECT;
			}
			dialog->SetOptions(options);
			if (!desc.title.empty()) {
				dialog->SetTitle(StringUtil::UTF8ToWide(desc.title).c_str());
			}
			if (!desc.physicalFolder.empty()) {
				const std::wstring folderPath = std::filesystem::path(StringUtil::UTF8ToWide(desc.physicalFolder)).make_preferred().wstring();
				IShellItem* folder = nullptr;
				if (SUCCEEDED(::SHCreateItemFromParsingName(folderPath.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
					dialog->SetFolder(folder);
					folder->Release();
				}
			}
			const HWND owner = desc.owner.system == NativeWindowSystem::Win32 ? static_cast<HWND>(desc.owner.window) : nullptr;
			result = dialog->Show(owner);
			if (SUCCEEDED(result)) {
				CollectPaths(dialog, outPaths);
			}
			dialog->Release();
			return result;
		}

	} // anonymous namespace

	bool RunOpenFileDialog(const EditorDialogs::FileDialogDesc& desc, Vector<str>& outPaths, str& outError) {
		const HRESULT initialized = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (FAILED(initialized)) {
			outError = std::format("COM could not start on this thread ({:#010x})", static_cast<u32>(initialized));
			return false;
		}
		const HRESULT result = ShowDialog(desc, outPaths);
		::CoUninitialize();
		if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
			outError = std::format("file dialog failed ({:#010x})", static_cast<u32>(result));
			return false;
		}
		return true;
	}

} // namespace Shalltear::EditorPlatform
