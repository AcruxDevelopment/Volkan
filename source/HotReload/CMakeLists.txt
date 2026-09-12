# The hot-reload runtime is infrastructure the host application links
# against directly. Unlike the plugins it loads at runtime (see
# source/examples/HotReloadDemo), this library is never itself meant
# to be hot-reloaded, so TYPE STATIC is a deliberate choice here,
# independent of the project-wide BUILD_SHARED_LIBS default.
add_lib_module(hotreload_lib TYPE STATIC OUTPUT_NAME "hotreload")

if(NOT WIN32)
	# dlopen/dlclose/dlinfo (ModuleHandle.cpp). PUBLIC, not PRIVATE:
	# for a STATIC library this needs to reach whatever executable
	# ultimately links hotreload_lib, not just hotreload_lib's own
	# (nonexistent, for a static lib) link step.
	target_link_libraries(hotreload_lib PUBLIC ${CMAKE_DL_LIBS})
endif()
