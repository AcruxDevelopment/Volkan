# The one target this whole example is about rebuilding, in place,
# while Host is already running and has it loaded. Hand-written rather
# than add_lib_module() for the same reason as HotReloadDemo/PluginV1
# -- see that file's comment -- even though there's only one build of
# this one: what makes this target unusual isn't needing two versions,
# it's needing to be a completely ordinary, repeatedly-overwritable
# build output while another process holds an OLDER build of it open
# via source/HotReload (see that library's HotReloadContext::
# stagePathForLoad() for why the running Host never itself locks this
# exact path).

if(BUILD_EXAMPLES)
	add_library(hotreload_live_plugin SHARED src/LivePlugin.cpp)
else()
	add_library(hotreload_live_plugin SHARED EXCLUDE_FROM_ALL src/LivePlugin.cpp)
endif()

set_target_properties(hotreload_live_plugin PROPERTIES
	OUTPUT_NAME "hotreload_live_plugin"
	WINDOWS_EXPORT_ALL_SYMBOLS OFF)
target_include_directories(hotreload_live_plugin PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../../HotReload/include)
_set_output_dirs(hotreload_live_plugin
	"${CMAKE_BINARY_DIR}/${RUNTIME_OUTPUT_SUBDIR}/examples"
	"${CMAKE_BINARY_DIR}/${LIBRARY_OUTPUT_SUBDIR}/examples")
_apply_compile_options(hotreload_live_plugin)
hot_reload_enable_patchable_functions(hotreload_live_plugin)

# Demonstrates the narrower-export pattern (see hot_reload.h's
# HOT_RELOAD_EXPORT and this function's own comment in
# cmake/HotReload.cmake) -- contrast with HotReloadDemo's PluginV1/
# PluginV2, which deliberately leave the GCC/Clang/MinGW default
# (export everything) in place to show that style too. Both are
# correct; this is the one this project would actually recommend.
hot_reload_restrict_exports(hotreload_live_plugin)

_register_module_target(EXAMPLE hotreload_live_plugin)
add_dependencies(all_examples hotreload_live_plugin)
