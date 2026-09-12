add_example_module(hotreload_demo_host OUTPUT_NAME "hotreload_demo_host" DEPENDS hotreload_lib)

target_include_directories(hotreload_demo_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Shared)

# The Host loads PluginV1/PluginV2 itself, at runtime, through
# source/HotReload -- it must NOT link against them directly (that
# would defeat the entire point of loading them dynamically via
# dlopen()/LoadLibrary()), so this is a build-order-only dependency
# plus each one's exact build output path baked in as a default, never
# a target_link_libraries().
add_dependencies(hotreload_demo_host hotreload_demo_plugin_v1 hotreload_demo_plugin_v2)
target_compile_definitions(hotreload_demo_host PRIVATE
	"PLUGIN_V1_PATH=\"$<TARGET_FILE:hotreload_demo_plugin_v1>\""
	"PLUGIN_V2_PATH=\"$<TARGET_FILE:hotreload_demo_plugin_v2>\""
)
