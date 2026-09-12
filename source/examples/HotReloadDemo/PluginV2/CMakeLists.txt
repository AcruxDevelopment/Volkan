# See ../PluginV1/CMakeLists.txt for why this is hand-written instead
# of add_lib_module() -- this is that same explanation's other half:
# PluginV2 exports the identical extern "C" names as PluginV1 (see
# ../Shared/PluginApi.h) with a different implementation standing in
# for "the same file, after your edit" in this demo's story.

if(BUILD_EXAMPLES)
	add_library(hotreload_demo_plugin_v2 SHARED src/Plugin.cpp src/CppWidget.cpp)
else()
	add_library(hotreload_demo_plugin_v2 SHARED EXCLUDE_FROM_ALL src/Plugin.cpp src/CppWidget.cpp)
endif()

target_include_directories(hotreload_demo_plugin_v2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Shared)
set_target_properties(hotreload_demo_plugin_v2 PROPERTIES
	OUTPUT_NAME "hotreload_demo_plugin_v2"
	WINDOWS_EXPORT_ALL_SYMBOLS ON)
_set_output_dirs(hotreload_demo_plugin_v2
	"${CMAKE_BINARY_DIR}/${RUNTIME_OUTPUT_SUBDIR}/examples"
	"${CMAKE_BINARY_DIR}/${LIBRARY_OUTPUT_SUBDIR}/examples")
_apply_compile_options(hotreload_demo_plugin_v2)
hot_reload_enable_patchable_functions(hotreload_demo_plugin_v2)

_register_module_target(EXAMPLE hotreload_demo_plugin_v2)
add_dependencies(all_examples hotreload_demo_plugin_v2)
