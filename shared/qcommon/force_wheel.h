// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cmath>
#include <cctype>
#include <cstddef>

namespace ForceWheel {
constexpr int MaxPowers = 12;
constexpr float Radius = 104, IconRadius = 80, DeadZone = 24;
constexpr float Pi = 3.14159265359f;

inline bool AllowsCommand(const char* command) {
	const char* allowed[] = {"+forcewheel", "forcenext", "forceprev", "+forward", "+back", "+moveleft", "+moveright",
		"+moveup", "+movedown", "+speed", "+strafe", "+left", "+right"};
	for (const char* name : allowed) {
		std::size_t i = 0;
		while (name[i] && std::tolower(static_cast<unsigned char>(command[i])) == name[i]) ++i;
		if (!name[i] && (!command[i] || std::isspace(static_cast<unsigned char>(command[i])))) return true;
	}
	return false;
}

// Slot indices follow cgame's existing Force selection order, not power IDs.
struct Frame {
	int available = 0, current = 0;
	int energy = 0, activePowers = 0;
	bool allowed = false, open = false;
	int selected = -1, hovered = -1;
	float x = 0, y = 0;
};

inline int Highlighted(const Frame& frame) {
	const int slot = frame.hovered >= 0 ? frame.hovered : frame.current;
	return slot >= 0 && slot < MaxPowers && (frame.available & (1 << slot)) ? slot : -1;
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

inline int Count(int mask) {
	int count = 0;
	for (int i = 0; i < MaxPowers; ++i) if (mask & (1 << i)) ++count;
	return count;
}
inline int Slot(int mask, int sector) {
	for (int i = 0; i < MaxPowers; ++i)
		if ((mask & (1 << i)) && sector-- == 0) return i;
	return -1;
}

class Selection {
	int keys[2] = {-2, -2};
	int mask = 0, sector = -1, pending = -1;
	bool open = false, finished = false;
	float x = 0, y = 0;
public:
	bool Open() const { return open; }
	bool CapturesInput() const { return open || finished; }
	int Mask() const { return mask; }
	int Hovered() const { return Slot(mask, sector); }
	float X() const { return x; }
	float Y() const { return y; }
	void Cancel() { open = finished = false; pending = -1; } // Keep held keys until release.
	bool Press(int key, int available) {
		available &= (1 << MaxPowers) - 1;
		if (!available || keys[0] == key || keys[1] == key || (!open && (keys[0] != -2 || keys[1] != -2))) return false;
		const int index = keys[0] == -2 ? 0 : keys[1] == -2 ? 1 : -1;
		if (index < 0) return false;
		keys[index] = key;
		if (open) return false;
		mask = available;
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
		pending = Hovered();
		open = false;
		finished = true;
		return true;
	}
	int TakeSelection() { const int result = pending; pending = -1; finished = false; return result; }
	void Move(float dx, float dy) {
		if (!open || !std::isfinite(dx) || !std::isfinite(dy)) return;
		x += dx; y += dy;
		const float distance = std::sqrt(x * x + y * y);
		if (distance > Radius) { x *= Radius / distance; y *= Radius / distance; }
		if (distance < DeadZone) { sector = -1; return; }
		const float step = 2 * Pi / Count(mask);
		float angle = std::atan2(y, x) + Pi / 2;
		if (angle < 0) angle += 2 * Pi;
		// A small angular margin stops selection chatter at sector boundaries.
		if (sector >= 0 && std::abs(std::remainder(angle - sector * step, 2 * Pi)) <= step / 2 + 0.04f) return;
		sector = int(std::floor(angle / step + 0.5f)) % Count(mask);
	}
};
} // namespace ForceWheel
