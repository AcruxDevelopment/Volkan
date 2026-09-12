/* PluginV1 -- "before your edit". See PluginV2/src/Plugin.cpp for
 * "after"; the two deliberately export the identical set of names
 * (minus plugin_legacy_feature, removed in V2) so HotReloadContext can
 * line them up. See ../Shared/PluginApi.h for what each export is
 * there to demonstrate.
 */
#include "PluginApi.h"

#include <cstdio>

int plugin_tick(int input)
{
	int result = input * 2;
	std::printf("    plugin_tick(%d) -> %d   [V1: input * 2]\n", input, result);
	return result;
}

int plugin_legacy_feature(int input)
{
	std::printf("    plugin_legacy_feature(%d) -> %d   [V1, about to be removed in V2]\n", input, input);
	return input;
}

#if defined(__GNUC__) || defined(__clang__)
int plugin_tiny_function(int input)
{
	return input + 1;
}
#endif

int plugin_build_marker = 1;
