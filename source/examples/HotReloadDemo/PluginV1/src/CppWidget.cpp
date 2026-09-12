/* CppWidget V1 -- "before your edit". See PluginV2/src/CppWidget.cpp
 * for "after": same three exports change behavior together, exactly
 * like PluginV1/src/Plugin.cpp's plain-C functions do.
 */
#include "CppWidgetApi.h"

#include <cstdio>

int CppWidget::virtualTick(int input)
{
	int result = input * 2;
	std::printf("    CppWidget::virtualTick(%d) -> %d   [V1: input * 2, via vtable]\n", input, result);
	return result;
}

int CppWidget::tick(int input)
{
	int result = input * 2;
	std::printf("    CppWidget::tick(%d) -> %d   [V1: input * 2, via mangled-name lookup]\n", input, result);
	return result;
}

CppWidget CppWidget::operator+(const CppWidget& /*other*/) const
{
	std::printf("    CppWidget::operator+   [V1]\n");
	return CppWidget();
}

extern "C" CppWidget* make_cpp_widget()
{
	return new CppWidget();
}
