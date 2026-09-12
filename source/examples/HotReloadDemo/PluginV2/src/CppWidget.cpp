/* CppWidget V2 -- "after your edit". See PluginV1/src/CppWidget.cpp
 * for "before".
 */
#include "CppWidgetApi.h"

#include <cstdio>

int CppWidget::virtualTick(int input)
{
	int result = input * 3;
	std::printf("    CppWidget::virtualTick(%d) -> %d   [V2: input * 3, via vtable]\n", input, result);
	return result;
}

int CppWidget::tick(int input)
{
	int result = input * 3;
	std::printf("    CppWidget::tick(%d) -> %d   [V2: input * 3, via mangled-name lookup]\n", input, result);
	return result;
}

CppWidget CppWidget::operator+(const CppWidget& /*other*/) const
{
	std::printf("    CppWidget::operator+   [V2]\n");
	return CppWidget();
}

extern "C" CppWidget* make_cpp_widget()
{
	return new CppWidget();
}
