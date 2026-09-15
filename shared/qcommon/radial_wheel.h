// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cmath>

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

class Selection {
	int keys[2] = {-2, -2};
	uint32_t mask = 0;
	int slots = 0, sector = -1, pending = -1;
	bool open = false, finished = false;
	float x = 0, y = 0;
public:
	bool Open() const { return open; }
	bool CapturesInput() const { return open || finished; }
	uint32_t Mask() const { return mask; }
	int Hovered() const { return Slot(mask, sector, slots); }
	float X() const { return x; }
	float Y() const { return y; }
	void Cancel() { open = finished = false; pending = -1; }
	bool Press(int key, uint32_t available, int slotCount = MaxSlots) {
		if (slotCount <= 0 || slotCount > MaxSlots) return false;
		if (slotCount < MaxSlots) available &= (1u << slotCount) - 1;
		if (!available || keys[0] == key || keys[1] == key || (!open && (keys[0] != -2 || keys[1] != -2))) return false;
		const int index = keys[0] == -2 ? 0 : keys[1] == -2 ? 1 : -1;
		if (index < 0) return false;
		keys[index] = key;
		if (open) return false;
		mask = available; slots = slotCount;
		sector = pending = -1;
		x = y = 0;
		finished = false;
		open = true;
		return true;
	}
	bool Release(int key) {
		bool matched = false;
		for (int& held : keys) if (held == key || key == -1) { held = -2; matched = true; }
		if (!matched || keys[0] != -2 || keys[1] != -2 || !open) return false;
		pending = Hovered(); open = false; finished = true;
		return true;
	}
	int TakeSelection() { const int result = pending; pending = -1; finished = false; return result; }
	void Move(float dx, float dy) {
		if (!open || !std::isfinite(dx) || !std::isfinite(dy)) return;
		x += dx; y += dy;
		const float distance = std::sqrt(x * x + y * y);
		if (distance > Radius) { x *= Radius / distance; y *= Radius / distance; }
		if (distance < DeadZone) { sector = -1; return; }
		const float step = 2 * Pi / Count(mask, slots);
		float angle = std::atan2(y, x) + Pi / 2;
		if (angle < 0) angle += 2 * Pi;
		if (sector >= 0 && std::abs(std::remainder(angle - sector * step, 2 * Pi)) <= step / 2 + 0.04f) return;
		sector = int(std::floor(angle / step + 0.5f)) % Count(mask, slots);
	}
};
} // namespace RadialWheel
