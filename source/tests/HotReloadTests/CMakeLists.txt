# Reuses PluginV1/PluginV2 from source/examples/HotReloadDemo as test
# fixtures rather than building yet another pair of throwaway shared
# libraries -- they already exist specifically to differ from each
# other in the ways this test needs to check (a changed function, a
# removed one, an added one, an unchanged exported variable). Requires
# BUILD_EXAMPLES as well as BUILD_TESTS for that reason.
add_test_module(hotreload_tests OUTPUT_NAME "hotreload_tests" DEPENDS hotreload_lib)
target_include_directories(hotreload_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../examples/HotReloadDemo/Shared)

if(BUILD_EXAMPLES)
	add_dependencies(hotreload_tests hotreload_demo_plugin_v1 hotreload_demo_plugin_v2)
	target_compile_definitions(hotreload_tests PRIVATE
		"PLUGIN_V1_PATH=\"$<TARGET_FILE:hotreload_demo_plugin_v1>\""
		"PLUGIN_V2_PATH=\"$<TARGET_FILE:hotreload_demo_plugin_v2>\"")
else()
	message(WARNING "hotreload_tests needs BUILD_EXAMPLES=ON too (it reuses the HotReloadDemo plugins as fixtures); skipping its checks.")
	target_compile_definitions(hotreload_tests PRIVATE HOTRELOAD_TESTS_SKIP=1)
endif()

# UndefinedSymbolFixture is a SIBLING under source/tests/, discovered
# independently by this project's own _discover_module_subdirectories()
# -- deliberately NOT add_subdirectory()'d from here, even though it's
# conceptually "this test's fixture": add_test_module()'s own source
# glob (*.cpp, recursive from CMAKE_CURRENT_SOURCE_DIR) would otherwise
# also sweep up the fixture's .cpp directly into hotreload_tests itself
# if it lived in a subdirectory of this one -- found the hard way while
# building this: the fixture's deliberately-undefined symbol then
# fails the whole test executable's OWN link, which does not tolerate
# an undefined symbol the way a shared library does.
add_dependencies(hotreload_tests hotreload_tests_undefined_symbol_fixture)
target_compile_definitions(hotreload_tests PRIVATE
	"UNDEFINED_SYMBOL_FIXTURE_PATH=\"$<TARGET_FILE:hotreload_tests_undefined_symbol_fixture>\"")
