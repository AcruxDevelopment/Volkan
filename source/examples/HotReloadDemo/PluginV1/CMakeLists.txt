# Not built through add_lib_module(): a hot-reloadable plugin is a
# different shape from an ordinary library module. PluginV1 and its
# sibling PluginV2 are two SEPARATE binaries that deliberately export
# the SAME extern "C" symbol names (see ../Shared/PluginApi.h) --
# that's exactly what lets HotReloadContext line them up and patch one
# into the other at runtime. add_lib_module() assumes one canonical
# build per module name and would not fit two builds of the same name
# needing to coexist, so this is plain add_library(), reusing the
# project's usual output-directory/warnings helpers directly. Gated on
# BUILD_EXAMPLES exactly like every other target under source/examples/.

if(BUILD_EXAMPLES)
	add_library(hotreload_demo_plugin_v1 SHARED src/Plugin.cpp src/CppWidget.cpp)
else()
	add_library(hotreload_demo_plugin_v1 SHARED EXCLUDE_FROM_ALL src/Plugin.cpp src/CppWidget.cpp)
endif()

target_include_directories(hotreload_demo_plugin_v1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Shared)
set_target_properties(hotreload_demo_plugin_v1 PROPERTIES
	OUTPUT_NAME "hotreload_demo_plugin_v1"
	# Matches add_lib_module()'s own SHARED-library branch
	# (cmake/BuildHelpers.cmake) exactly, for exactly the same reason:
	# MinGW's linker exports every global symbol from a DLL by
	# default, but MSVC exports NONE unless told to -- without this,
	# PluginApi.h's functions would not be visible in the export table
	# at all when built with MSVC, silently breaking the whole demo on
	# that one toolchain.
	WINDOWS_EXPORT_ALL_SYMBOLS ON)
_set_output_dirs(hotreload_demo_plugin_v1
	"${CMAKE_BINARY_DIR}/${RUNTIME_OUTPUT_SUBDIR}/examples"
	"${CMAKE_BINARY_DIR}/${LIBRARY_OUTPUT_SUBDIR}/examples")
_apply_compile_options(hotreload_demo_plugin_v1)

# The whole point of this target: give source/HotReload's jump-patch
# backends guaranteed room to write into. See cmake/HotReload.cmake.
hot_reload_enable_patchable_functions(hotreload_demo_plugin_v1)

_register_module_target(EXAMPLE hotreload_demo_plugin_v1)
add_dependencies(all_examples hotreload_demo_plugin_v1)
