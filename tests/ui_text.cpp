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
	auto proportional = [](unsigned char prior, unsigned char c) {
		const float natural = c == 'i' || c == ' ' ? 4.0f : 10.0f;
		return UiText::Advance{natural - (prior == 'A' && c == 'V' ? 2.0f : 0.0f), natural};
	};
	text = UiText::ArrangeMeasured("A^1V", proportional, 20, -1, false, false);
	assert(text.metrics.width == 18 && text.runs[1].x == 8);
	text = UiText::ArrangeMeasured("iii WWW", proportional, 20, 35, true, false);
	assert(text.metrics.width <= 35 && text.metrics.height == 40);
	text = UiText::ArrangeMeasured("WWWiii", proportional, 20, 25, false, false);
	assert(text.runs[0].text == "WW" && text.metrics.width == 20);
	std::puts("PASS: gameplay text colors, wrapping, clipping, line breaks, and encoding");
}
