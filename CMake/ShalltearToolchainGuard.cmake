# presence is editor's job -> compatibility is package's

function(shalltear_enforce_toolchain target_name)
	execute_process(
		COMMAND ${CMAKE_CXX_COMPILER} -dumpmachine
		OUTPUT_VARIABLE theirTarget
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
		RESULT_VARIABLE dumpmachineResult)
	if(NOT dumpmachineResult EQUAL 0 OR NOT theirTarget)
		message(FATAL_ERROR
			"Shalltear: '${target_name}' is being built with ${CMAKE_CXX_COMPILER}, which does not answer "
			"-dumpmachine. Shalltear is Clang-only.")
	endif()
	if(SHALLTEAR_COMPILER_TARGET AND NOT theirTarget STREQUAL SHALLTEAR_COMPILER_TARGET)
		message(FATAL_ERROR
			"Shalltear: '${target_name}' is being built for ${theirTarget}, but this SDK was built for "
			"${SHALLTEAR_COMPILER_TARGET}. A module exchanges C++ objects with the engine, so the two must "
			"target the same ABI. Mismatch here LOADS then misbehaves instead of failing.\n"
			"  Fix: configure with the engine's own compiler. The one whose -dumpmachine says "
			"${SHALLTEAR_COMPILER_TARGET}.")
	endif()

	if(NOT CMAKE_CXX_COMPILER_ID STREQUAL SHALLTEAR_COMPILER_ID)
		message(FATAL_ERROR
			"Shalltear: '${target_name}' is being built with ${CMAKE_CXX_COMPILER_ID}, but this SDK was built with "
			"${SHALLTEAR_COMPILER_ID}. A module exchanges C++ objects with the engine, so the compilers must match.\n"
			"  Fix: configure with -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++ -DCMAKE_C_COMPILER=x86_64-w64-mingw32-clang")
	endif()

	# major only because minor releases keep ABI
	string(REGEX MATCH "^[0-9]+" theirMajor "${CMAKE_CXX_COMPILER_VERSION}")
	string(REGEX MATCH "^[0-9]+" ourMajor "${SHALLTEAR_COMPILER_VERSION}")
	if(NOT theirMajor STREQUAL ourMajor)
		message(FATAL_ERROR
			"Shalltear: '${target_name}' is being built with ${SHALLTEAR_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}, "
			"but this SDK was built with ${SHALLTEAR_COMPILER_ID} ${SHALLTEAR_COMPILER_VERSION}. "
			"Major versions must match.\n"
			"  Fix: install ${SHALLTEAR_COMPILER_ID} ${ourMajor}.x, or rebuild the engine with your compiler.")
	endif()
endfunction()

# both pairings fail at runtime across engine builds, this refuses early
function(shalltear_enforce_engine_identity target_name)
	if(NOT SHALLTEAR_EXPECT_ENGINE_IDENTITY)
		return()
	endif()

	set(packageIdentity "${SHALLTEAR_VERSION}|${SHALLTEAR_GIT_COMMIT}")
	if(packageIdentity STREQUAL SHALLTEAR_EXPECT_ENGINE_IDENTITY)
		return()
	endif()

	message(FATAL_ERROR
		"Shalltear: '${target_name}' resolved the Shalltear package at\n"
		"    ${SHALLTEAR_PACKAGE_ROOT}\n"
		"  which is engine build ${packageIdentity}, but the engine driving this build is "
		"${SHALLTEAR_EXPECT_ENGINE_IDENTITY}. A binary built against this package is loaded by, or reads content "
		"cooked by, that engine (must be 1 build), the mismatch refused at launch rather "
		"than here.\n"
		"  If an engine build ${SHALLTEAR_EXPECT_ENGINE_IDENTITY} still EXISTS, the package above is the stale "
		"half: rebuild it (or point Shalltear_ROOT at that engine's package).\n"
		"  If it does NOT, the EXPECTATION is the problem. It's a cache entry of this build tree, written by "
		"whatever configured it + re-read by every argument-less re-run (including what ninja spends) "
		"to regenerate build files. Reconfigure\n"
		"    ${CMAKE_BINARY_DIR}\n"
		"  passing the current -DSHALLTEAR_EXPECT_ENGINE_IDENTITY, or delete its CMakeCache.txt and let the tool "
		"that owns the tree configure it again.")
endfunction()
