#include "Platform/Posix/PosixSpawn.h"

#include "Shalltear/Editor/EditorDialogs.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string_view>
#include <thread>

namespace Shalltear::EditorPlatform {

	namespace {

		enum class ToolRun : u8 {
			Missing,
			Closed,
			Failed,
		};

		void CollectPaths(std::string_view output, Vector<str>& out) {
			while (!output.empty()) {
				const size_t end = std::min(output.find('\n'), output.size());
				std::string_view line = output.substr(0, end);
				output.remove_prefix(std::min(end + 1, output.size()));
				if (!line.empty() && line.back() == '\r') {
					line.remove_suffix(1);
				}
				std::error_code ec;
				if (!line.empty() && line.front() == '/' && std::filesystem::is_regular_file(std::filesystem::path(line), ec)) {
					out.emplace_back(line);
				}
			}
		}

		// stderr merged into stdout (only existing file paths taken)
		ToolRun RunTool(const Vector<str>& arguments, Vector<str>& outPaths, str& outError) {
			SpawnOptions options;
			options.captureOutput = true;
			SpawnedProcess spawned;
			str error;
			if (!SpawnArguments(arguments, options, spawned, error)) {
				return ToolRun::Missing;
			}
			str output;
			char chunk[4096];
			for (size_t read = 0; (read = ReadProcessOutput(spawned.output, chunk, sizeof(chunk))) > 0;) {
				output.append(chunk, read);
			}
			CloseProcessOutput(spawned.output);
			i32 exitCode = 0;
			while (!TryWaitProcess(spawned.process, exitCode)) {
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
			ReleaseProcess(spawned.process);
			if (exitCode == 127) {
				return ToolRun::Missing;
			}
			if (exitCode != 0 && exitCode != 1) {
				outError = std::format("{} exited with code {}", arguments[0], exitCode);
				return ToolRun::Failed;
			}
			if (exitCode == 0) {
				CollectPaths(output, outPaths);
			}
			return ToolRun::Closed;
		}

	} // anonymous namespace

	bool RunOpenFileDialog(const EditorDialogs::FileDialogDesc& desc, Vector<str>& outPaths, str& outError) {
		const char* home = std::getenv("HOME");
		const str folder = !desc.physicalFolder.empty() ? desc.physicalFolder : str(home ? home : "");

		Vector<str> zenity{GetMemoryResource(MemTag::Editor)};
		zenity.push_back("zenity");
		zenity.push_back("--file-selection");
		zenity.push_back("--title=" + desc.title);
		if (desc.multiple) {
			zenity.push_back("--multiple");
			zenity.push_back("--separator=\n");
		}
		if (!folder.empty()) {
			zenity.push_back("--filename=" + folder + "/");
		}

		Vector<str> kdialog{GetMemoryResource(MemTag::Editor)};
		kdialog.push_back("kdialog");
		kdialog.push_back("--title");
		kdialog.push_back(desc.title);
		kdialog.push_back("--getopenfilename");
		kdialog.push_back(folder.empty() ? str(".") : folder);
		if (desc.multiple) {
			kdialog.push_back("--multiple");
			kdialog.push_back("--separate-output");
		}

		for (const Vector<str>* arguments : {&zenity, &kdialog}) {
			const ToolRun run = RunTool(*arguments, outPaths, outError);
			if (run != ToolRun::Missing) {
				return run == ToolRun::Closed;
			}
		}
		outError = "neither zenity nor kdialog is installed";
		return false;
	}

} // namespace Shalltear::EditorPlatform
