// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace RadialWheel {
constexpr int MaxSlots = 32;
constexpr float Radius = 104, IconRadius = 80, DeadZone = 24;
constexpr float Pi = 3.14159265359f;
enum class Kind { Force, Weapon };

struct View {
	uint32_t available = 0;
	int slotCount = 0, current = -1, hovered = -1;
	float x = 0, y = 0;
	bool pointer = false;
	Kind kind = Kind::Force;
};

inline int Count(uint32_t mask, int slots) {
	int count = 0;
	for (int i = 0; i < slots && i < MaxSlots; ++i) if (mask & (1u << i)) ++count;
	return count;
}
inline int Slot(uint32_t mask, int sector, int slots) {
	for (int i = 0; i < slots && i < MaxSlots; ++i)
		if ((mask & (1u << i)) && sector-- == 0) return i;
	return -1;
}
inline int Highlighted(uint32_t mask, int current, int hovered, int slots) {
	const int slot = hovered >= 0 ? hovered : current;
	return slot >= 0 && slot < slots && slot < MaxSlots && (mask & (1u << slot)) ? slot : -1;
}
inline int Highlighted(const View& view) {
	return Highlighted(view.available, view.current, view.hovered, view.slotCount);
}

// Passive cycling feedback uses real time and never owns input or game speed.
class Preview {
	double until = 0;
public:
	void Show(double now) { until = now + 2.0; }
	void Cancel() { until = 0; }
	float Opacity(double now) const {
		const double remaining = until - now;
		return remaining <= 0 ? 0 : remaining < 0.3 ? float(remaining / 0.3) : 1;
	}
};
} // namespace RadialWheel
