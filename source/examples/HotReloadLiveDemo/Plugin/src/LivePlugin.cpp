/* LivePlugin.cpp -- THE FILE TO EDIT.
 *
 * With source/examples/HotReloadLiveDemo/Host running in one
 * terminal, rebuild just this file from another with:
 *
 *     cmake --build <your build dir> --target hotreload_live_plugin
 *
 * or run watch.sh (in this same directory) in that other terminal to
 * have it rebuild automatically every time you save. Either way, the
 * running Host notices within about a second of a successful rebuild
 * and hot-reloads -- no restart.
 *
 * Things worth trying, roughly in order:
 *
 *   1. Change the message or the multiplier in live_tick() below and
 *      rebuild -- watch the running Host's output change mid-flight,
 *      still counting the same frame number it already had.
 *
 *   2. Delete live_tick() entirely (or rename it) and rebuild -- the
 *      Host supplies a fallback for exactly this case, so you'll see
 *      that kick in instead of a crash. Bring the function back under
 *      the same name and rebuild again... and notice it does NOT
 *      automatically take back over -- see the Host's own comment on
 *      why, right where it resolves live_tick.
 *
 *   3. Add a brand new function:
 *
 *          extern "C" int live_extra(int frame) { ... }
 *
 *      and rebuild -- the Host looks for this one on every frame and
 *      starts calling it the moment it exists.
 *
 *   4. Add a new exported variable, e.g.
 *
 *          extern "C" int live_marker = 1;
 *
 *      then change its value on a later edit and rebuild -- watch the
 *      reload log call it out as data, classified and left alone,
 *      never patched, exactly like plugin_build_marker in
 *      source/examples/HotReloadDemo.
 */
#include "hot_reload/hot_reload.h"

#include <cstdio>
#include <iostream>

extern "C" HOT_RELOAD_EXPORT int live_tick(int frame)
{
	int multiplier = 2; // <- try changing this
	int value = frame * multiplier;
	std::printf("  live_tick (%d) -> %d\n", frame, value);
	std::cout.flush();
	return value;
}
