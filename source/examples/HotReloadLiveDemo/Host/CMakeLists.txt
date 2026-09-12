add_example_module(hotreload_live_demo_host OUTPUT_NAME "hotreload_live_demo_host" DEPENDS hotreload_lib)

# Build-order-only, exactly like HotReloadDemo/Host -- see that file's
# CMakeLists.txt comment. This Host must NOT link against the plugin
# directly; it loads it dynamically through source/HotReload instead.
add_dependencies(hotreload_live_demo_host hotreload_live_plugin)
target_compile_definitions(hotreload_live_demo_host PRIVATE
	"LIVE_PLUGIN_PATH=\"$<TARGET_FILE:hotreload_live_plugin>\"")
