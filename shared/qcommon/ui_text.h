// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace UiText {
constexpr int LabelFontFlag = 0x20000000;
struct Style {
	float x = 0, y = 0, size = 16, maxWidth = -1;
	float color[4] = {1, 1, 1, 1};
	int legacyFont = 0;
	float legacyScale = 1;
	bool pixels = false, wrap = false, forceColor = false, outline = true, blink = false, semibold = false, datapad = false;
};
struct Metrics { float width = 0, height = 0; };
struct Run { std::string text; float x = 0, y = 0; int color = -1; };
struct Layout { std::vector<Run> runs; Metrics metrics; };

inline bool ColorCode(const char* p) { return p[0] == '^' && p[1] >= '0' && p[1] <= '9'; }

inline uint32_t Codepoint(unsigned char c) {
	// Stock Western StringEd and console text use Windows-1252.
	static const uint32_t controls[] = {
		0x20ac, 0xfffd, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
		0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0xfffd, 0x017d, 0xfffd,
		0xfffd, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
		0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0xfffd, 0x017e, 0x0178};
	return c >= 0x80 && c < 0xa0 ? controls[c - 0x80] : c;
}

struct Advance { float width, natural; };

// Layout shared by measurement and drawing, including kerning across color runs.
template<class Measure>
inline Layout ArrangeMeasured(const char* text, Measure advance, float lineHeight, float maxWidth, bool wrap, bool forceColor) {
	Layout result;
	if (!text || !*text) return result;
	Run run;
	float x = 0, y = 0;
	unsigned char previous = 0;
	bool wordStart = true, wrapped = false, clipped = false;
	auto flush = [&]() { if (!run.text.empty()) { result.runs.push_back(run); run.text.clear(); } };
	auto newline = [&]() {
		flush(); result.metrics.width = std::max(result.metrics.width, x);
		x = 0; y += lineHeight; previous = 0; clipped = false;
	};
	for (const char* p = text; *p; ++p) {
		if (ColorCode(p)) { flush(); if (!forceColor) run.color = p[1] - '0'; ++p; continue; }
		if (*p == '\r') continue;
		if (*p == '\n') { newline(); wordStart = true; wrapped = false; continue; }
		if (clipped) continue;
		if (wrap && wordStart && *p != ' ' && *p != '\t' && maxWidth > 0) {
			float width = 0;
			unsigned char prior = previous;
			for (const char* q = p; *q && *q != ' ' && *q != '\t' && *q != '\n'; ++q) {
				if (ColorCode(q)) ++q;
				else if (*q != '\r') { width += advance(prior, (unsigned char)*q).width; prior = (unsigned char)*q; }
			}
			if (x > 0 && width <= maxWidth && x + width > maxWidth) { newline(); wrapped = true; }
		}
		wordStart = *p == ' ' || *p == '\t';
		const int count = *p == '\t' ? 4 : 1;
		for (int i = 0; i < count; ++i) {
			const char c = *p == '\t' ? ' ' : *p;
			if (wrapped && x == 0 && c == ' ') continue;
			auto step = advance(previous, (unsigned char)c);
			if (maxWidth >= 0 && x + step.width > maxWidth) {
				if (wrap && x > 0) {
					newline(); wrapped = true; if (c == ' ') continue;
					step = advance(0, (unsigned char)c);
				} else { clipped = !wrap; continue; }
			}
			if (maxWidth >= 0 && step.width > maxWidth) continue;
			if (run.text.empty()) { run.x = x + step.width - step.natural; run.y = y; }
			run.text += c; x += step.width; previous = (unsigned char)c; wrapped = false;
		}
	}
	flush();
	result.metrics.width = std::max(result.metrics.width, x);
	result.metrics.height = y + lineHeight;
	return result;
}

inline Layout Arrange(const char* text, float advance, float lineHeight, float maxWidth, bool wrap, bool forceColor) {
	if (advance <= 0) return {};
	return ArrangeMeasured(text, [advance](unsigned char, unsigned char) { return Advance{advance, advance}; },
		lineHeight, maxWidth, wrap, forceColor);
}
} // namespace UiText
