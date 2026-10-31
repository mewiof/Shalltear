#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/CppToolchain.h"
#include "Shalltear/Editor/EditorAPI.h"

#include <string_view>

namespace Shalltear {

	enum class CompilerDiagnosticSeverity : u8 {
		Note = 0,
		Warning,
		Error,
	};

	struct CompilerDiagnostic {
		CompilerDiagnosticSeverity severity = CompilerDiagnosticSeverity::Error;
		str file;
		u32 line = 0;
		u32 column = 0;
		str message;
		str raw;
		bool hasBody = false;
	};

	namespace CompilerDiagnosticParser {

		SHALLTEAR_EDITOR_API bool ParseDiagnosticLine(std::string_view line, CompilerDiagnostic& out);

		SHALLTEAR_EDITOR_API bool AppendContinuation(CompilerDiagnostic& open, std::string_view line);

		SHALLTEAR_EDITOR_API str StripAnsi(std::string_view line);

	} // namespace CompilerDiagnosticParser

	enum class CompileState : u8 {
		Idle = 0,
		Configuring,
		Compiling,
		Succeeded,
		Failed,
	};

	struct CompileControlText {
		str status;
		str tooltip;
	};

	SHALLTEAR_EDITOR_API CompileControlText DescribeCompileState(CompileState state, u32 errorCount, u32 warningCount);

	struct CompileRequest {
		str projectPath;
		bool recompile = false;
		Vector<str> enabledPackages;
	};

	class SHALLTEAR_EDITOR_API CompileService {
	public:
		CompileService();
		~CompileService();

		CompileService(const CompileService&) = delete;
		CompileService& operator=(const CompileService&) = delete;

		static CompileService* GetActive();

		void RefreshToolchain();
		const CppToolchain::ToolchainInfo& GetToolchain() const { return _toolchain; }

		bool RequestCompile(const CompileRequest& request);

		// only path that kills a compile
		void Cancel();

		void PumpMainThread();

		CompileState GetState() const { return _state; }
		bool IsBusy() const { return _state == CompileState::Configuring || _state == CompileState::Compiling; }
		u32 GetErrorCount() const { return _errorCount; }
		u32 GetWarningCount() const { return _warningCount; }
		f64 GetLastDurationSeconds() const { return _lastDurationSeconds; }
		const Vector<CompilerDiagnostic>& GetDiagnostics() const { return _diagnostics; }

		const str& GetProjectPath() const { return _projectPath; }
	private:
		struct Impl;

		void BeginConfigure();
		void BeginCompile();
		void ConsumeOutput();
		void FinishStep(i32 exitCode);
		void PublishState(CompileState state);

		Impl* _impl = nullptr;

		CompileState _state = CompileState::Idle;
		str _projectPath;
		str _configName;
		Vector<str> _enabledPackages{GetMemoryResource(MemTag::Editor)};
		bool _recompileRequested = false;
		bool _rerunQueued = false;
		u32 _errorCount = 0;
		u32 _warningCount = 0;
		u64 _startTicks = 0;
		f64 _lastDurationSeconds = 0.0;

		CppToolchain::ToolchainInfo _toolchain;
		Vector<CompilerDiagnostic> _diagnostics{GetMemoryResource(MemTag::Editor)};
	};

} // namespace Shalltear
