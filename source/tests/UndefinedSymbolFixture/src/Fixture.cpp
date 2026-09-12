/* A genuine ODR violation, kept deliberately: a non-inline static
 * class member that is declared but never given an out-of-class
 * definition. This compiles cleanly and links cleanly into a shared
 * library -- a shared library is allowed to keep undefined symbols by
 * default -- and only fails once something actually calls
 * trigger_undefined_symbol() below, ODR-using counter for the first
 * time. See source/HotReload/README.md's "hot_reload_last_error()"
 * section for the full story; HotReloadTests/Main.cpp loads this
 * fixture specifically to confirm hot_reload_last_error() reports
 * something useful for exactly this case, rather than leaving it a
 * silent "load failed" black box.
 */
class HasUndefinedStaticMember
{
public:
	static int useIt()
	{
		return counter;
	}

private:
	static int counter; // declared, deliberately never defined anywhere
};

extern "C" int trigger_undefined_symbol()
{
	return HasUndefinedStaticMember::useIt();
}
