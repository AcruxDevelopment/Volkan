/* PluginV2 -- "after your edit". See PluginV1/src/Plugin.cpp for
 * "before". plugin_tick's behavior changed, plugin_legacy_feature was
 * deleted, and plugin_new_feature was added -- exactly the three kinds
 * of change hot_reload.h's HotReloadSymbolEvent enum distinguishes.
 */
#include "PluginApi.h"

#include <cstdio>

int plugin_tick(int input)
{
	int result = input * 3;
	std::printf("    plugin_tick(%d) -> %d   [V2: input * 3]\n", input, result);
	return result;
}

/* plugin_legacy_feature deliberately does not exist in V2 anymore. */

int plugin_new_feature(int input)
{
	int result = input * 10;
	std::printf("    plugin_new_feature(%d) -> %d   [V2: brand new]\n", input, result);
	return result;
}

#if defined(__GNUC__) || defined(__clang__)
int plugin_tiny_function(int input)
{
	return input + 2;
}
#endif

int plugin_build_marker = 2;
