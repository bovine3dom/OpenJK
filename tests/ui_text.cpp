// SPDX-License-Identifier: GPL-2.0-or-later
#include "qcommon/ui_text.h"
#include <cassert>
#include <cstdio>

int main() {
	auto text = UiText::Arrange("^1Red ^7white", 10, 20, -1, false, false);
	assert(text.metrics.width == 90 && text.metrics.height == 20);
	assert(text.runs.size() == 2 && text.runs[0].color == 1 && text.runs[1].color == 7);
	assert(text.runs[1].x == 40);
	text = UiText::Arrange("^1Red", 10, 20, -1, false, true);
	assert(text.runs[0].color == -1 && text.metrics.width == 30);
	text = UiText::Arrange("one two three", 10, 20, 80, true, false);
	assert(text.metrics.width <= 80 && text.metrics.height == 40);
	assert(text.runs.back().text == "three" && text.runs.back().y == 20);
	text = UiText::Arrange("abcdefghij", 10, 20, 30, true, false);
	assert(text.metrics.width == 30 && text.metrics.height == 80);
	text = UiText::Arrange("abcdefghij", 10, 20, 30, false, false);
	assert(text.metrics.width == 30 && text.runs[0].text == "abc");
	text = UiText::Arrange("a\r\nb", 10, 20, -1, false, false);
	assert(text.metrics.width == 10 && text.metrics.height == 40);
	assert(UiText::Codepoint(0x92) == 0x2019 && UiText::Codepoint(0xe9) == 0xe9);
	text = UiText::Arrange("test", 10, 20, 0, true, false);
	assert(text.runs.empty());
	std::puts("PASS: gameplay text colors, wrapping, clipping, line breaks, and encoding");
}
