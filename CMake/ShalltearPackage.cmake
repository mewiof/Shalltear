
function(shalltear_add_package PACKAGE_NAME)
	cmake_parse_arguments(SLT_PKG "" "" "SOURCES" ${ARGN})

	if(NOT SLT_PKG_SOURCES)
		message(FATAL_ERROR "shalltear_add_package(${PACKAGE_NAME}): SOURCES is required and must not be empty.")
	endif()

	set(manifestFile "${CMAKE_CURRENT_SOURCE_DIR}/package.json")
	if(NOT EXISTS "${manifestFile}")
		message(FATAL_ERROR
			"shalltear_add_package(${PACKAGE_NAME}): no package.json beside this CMakeLists.txt.")
	endif()

	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifestFile}")
	file(READ "${manifestFile}" manifestJson)
	string(JSON manifestId ERROR_VARIABLE manifestError GET "${manifestJson}" name)
	if(manifestError)
		message(FATAL_ERROR "shalltear_add_package(${PACKAGE_NAME}): ${manifestFile} has no readable `name`: ${manifestError}")
	endif()

	if(SHALLTEAR_BUILD_SHARED)
		add_library(${PACKAGE_NAME} SHARED ${SLT_PKG_SOURCES})
	else()
		# SLT_PACKAGE_STATIC switches the entry point and stays private
		add_library(${PACKAGE_NAME} STATIC ${SLT_PKG_SOURCES})
		target_compile_definitions(${PACKAGE_NAME} PRIVATE SLT_PACKAGE_STATIC)
	endif()

	target_link_libraries(${PACKAGE_NAME} PRIVATE ShalltearCore)
	set_target_properties(${PACKAGE_NAME} PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		EXPORT_NAME ${PACKAGE_NAME})

	set(publicDir "${CMAKE_CURRENT_SOURCE_DIR}/Public")
	if(IS_DIRECTORY "${publicDir}")
		string(TOUPPER ${PACKAGE_NAME} packageNameUpper)
		target_compile_definitions(${PACKAGE_NAME} PRIVATE SHALLTEAR_${packageNameUpper}_BUILD)
		target_include_directories(${PACKAGE_NAME} PUBLIC
			$<BUILD_INTERFACE:${publicDir}>
			$<INSTALL_INTERFACE:${SHALLTEAR_INSTALL_INCLUDE}>)

		# before the packaging transform
		install(DIRECTORY "${publicDir}/" DESTINATION ${SHALLTEAR_INSTALL_INCLUDE} FILES_MATCHING PATTERN "*.h")

		set_property(GLOBAL APPEND PROPERTY SHALLTEAR_PACKAGE_PUBLIC_DIRS "${publicDir}")

		if(EXISTS "${publicDir}/Shalltear/${PACKAGE_NAME}.h")
			set_target_properties(${PACKAGE_NAME} PROPERTIES
				SHALLTEAR_UMBRELLA "Shalltear/${PACKAGE_NAME}.h"
				EXPORT_PROPERTIES SHALLTEAR_UMBRELLA)
		endif()
	endif()

	# dev and SDK may share one discovery root
	set(stagedManifest "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Packages/${PACKAGE_NAME}/package.json")
	add_custom_command(
		OUTPUT "${stagedManifest}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${manifestFile}" "${stagedManifest}"
		DEPENDS "${manifestFile}"
		COMMENT "Staging ${manifestId}'s manifest")
	add_custom_target(${PACKAGE_NAME}Manifest DEPENDS "${stagedManifest}")
	add_dependencies(${PACKAGE_NAME} ${PACKAGE_NAME}Manifest)

	set_property(GLOBAL APPEND PROPERTY SHALLTEAR_PACKAGE_IDS "${manifestId}")
	set_property(GLOBAL APPEND PROPERTY SHALLTEAR_PACKAGE_TARGETS "${PACKAGE_NAME}")
	set_property(GLOBAL APPEND PROPERTY SHALLTEAR_PACKAGE_MANIFESTS "${manifestFile}")

	set_target_properties(${PACKAGE_NAME} PROPERTIES EXPORT_COMPILE_COMMANDS ON)
endfunction()
