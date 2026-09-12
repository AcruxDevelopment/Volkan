/* PluginApi.h -- the contract PluginV1 and PluginV2 both implement,
 * and the Host loads by name. Every function here follows
 * cpp-style-guide.md section 11's rule for anything loaded at runtime:
 * plain C linkage, no classes or exceptions crossing the boundary.
 *
 * This header is deliberately identical for both plugin versions --
 * in a real project this is the file that stays stable while
 * PluginV1/src/Plugin.cpp and PluginV2/src/Plugin.cpp (standing in for
 * "the same file, before and after your edit") diverge.
 */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

	/* Changes behavior between V1 and V2 -- watch this one get
	 * HOT_RELOAD_SYMBOL_PATCHED. */
	int plugin_tick(int input);

	/* Only in V1, gone in V2 -- watch this one get
	 * HOT_RELOAD_SYMBOL_REMOVED, and the Host's stub take over for any
	 * call still aimed at it afterwards. */
	int plugin_legacy_feature(int input);

	/* Only in V2 -- watch this one get HOT_RELOAD_SYMBOL_ADDED, then
	 * get called for the first time right after. */
	int plugin_new_feature(int input);

#if defined(__GNUC__) || defined(__clang__)
	/* Deliberately too small to safely hold a jump on any of this
	 * library's backends (its patchable-function-entry padding is
	 * overridden down to zero) -- watch this one get
	 * HOT_RELOAD_SYMBOL_SKIPPED_TOO_SMALL rather than corrupt whatever
	 * comes after it. MSVC has no equivalent per-function override
	 * this demo could apply, so this one only exists on GCC/Clang.
	 */
	__attribute__((patchable_function_entry(0))) int plugin_tiny_function(int input);
#endif

	/* An exported VARIABLE, not a function, sitting right next to
	 * real functions in the same library and changing value between
	 * V1 and V2 just like plugin_tick's behavior does -- watch this
	 * one get HOT_RELOAD_SYMBOL_SKIPPED_NOT_CODE. This is the
	 * mechanical proof behind hot_reload.h's "only executable code
	 * changes" guarantee: nothing here tells the library "this one is
	 * data, don't touch it" -- it discovers that itself from the
	 * symbol table, the same way it discovers plugin_tick is code.
	 */
	extern int plugin_build_marker;

#ifdef __cplusplus
}
#endif
