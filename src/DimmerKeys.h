// Illuminated - Core: a light's pulse as keys the game's own dimmer plays
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// Pulse.h gives each kind of spell its shape as a function of time, worked out by our code for every shaped light on every frame.
// Ten of its shapes repeat exactly. Such a shape is turned here, once, when the light is made, into one loop of keys for the
// game's light dimmer (a controller the engine runs itself, with straight lines between keys): after that no code of ours runs
// for the pulse. The keys follow the shape within a stated tolerance, checked off the game (tests\testcore.cpp). The four shapes
// that never repeat (fire, frost, shock, poison) have no loop here.
// Nothing of the game in this file.

#pragma once

#include "Pulse.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace DimmerKeys
{
	// one key: seconds into the loop, and the factor on the light's steady fade there
	struct Key
	{
		float time{ 0.0f };
		float value{ 1.0f };
	};

	// one loop: the first key at 0, the last at the period with the first key's value, so the wrap shows no step
	struct Loop
	{
		std::vector<Key> keys{};
		float            period{ 0.0f };
	};

	// how far a straight line between two keys may stray from the shape, as a share of the steady fade
	inline constexpr double kTolerance = 0.002;
	// no piece of the loop is cut finer than this share of the period, however sharp a corner is
	inline constexpr double      kFinest = 1.0 / 4096.0;
	inline constexpr std::size_t kMostKeys = 1024;
	// a loop is cut into this many even pieces first, so a shape with several beats in one loop has each of them looked at
	inline constexpr int kPieces = 16;

	// seconds after which the shape of a_kind repeats exactly; 0 for a shape that never repeats, and for none
	[[nodiscard]] constexpr double Period(Pulse::Kind a_kind) noexcept
	{
		switch (a_kind) {
		case Pulse::Kind::kHealing:
			return 4.0;
		case Pulse::Kind::kAbsorbHealth:
			return 1.0;
		case Pulse::Kind::kAbsorbMagicka:
			return 1.2;
		case Pulse::Kind::kAbsorbStamina:
			return 0.8;
		case Pulse::Kind::kIllusion:
			// two waves of 0.9 and 1.1 a second meet again after 10 seconds
			return 10.0;
		case Pulse::Kind::kConjuration:
			return 3.0;
		case Pulse::Kind::kAlteration:
			return 6.0;
		case Pulse::Kind::kMagelight:
			return 3.0;
		case Pulse::Kind::kWard:
			return 0.5;
		case Pulse::Kind::kRune:
			return 2.5;
		case Pulse::Kind::kNone:
		case Pulse::Kind::kFire:
		case Pulse::Kind::kFrost:
		case Pulse::Kind::kShock:
		case Pulse::Kind::kPoison:
		default:
			return 0.0;
		}
	}

	namespace Detail
	{
		// keys between two times whose values are known, cut in half wherever the straight line strays past the tolerance
		template <class F>
		void Cut(const F& a_shape, double a_from, double a_fromValue, double a_to, double a_toValue, double a_finest, std::vector<Key>& a_keys)
		{
			const double middle = 0.5 * (a_from + a_to);
			const double value = a_shape(middle);
			// room is kept for the pieces' own end keys, so a loop never holds more than kMostKeys
			const auto full = [&a_keys] { return a_keys.size() + static_cast<std::size_t>(kPieces) >= kMostKeys; };
			const bool fine = (a_to - a_from) <= a_finest || full();
			// a quarter and three quarters are looked at too: a corner or a narrow beat can sit on the line at the middle alone
			const double first = a_shape(0.5 * (a_from + middle));
			const double third = a_shape(0.5 * (middle + a_to));
			const auto   off = [&](double a_at, double a_is) {
				const double share = (a_at - a_from) / (a_to - a_from);
				return std::abs(a_is - (a_fromValue + (a_toValue - a_fromValue) * share));
			};
			if (fine || (off(middle, value) <= kTolerance && off(0.5 * (a_from + middle), first) <= kTolerance &&
							off(0.5 * (middle + a_to), third) <= kTolerance)) {
				return;
			}
			Cut(a_shape, a_from, a_fromValue, middle, value, a_finest, a_keys);
			// the left half may have used the room that was left
			if (full()) {
				return;
			}
			a_keys.push_back({ static_cast<float>(middle), static_cast<float>(value) });
			Cut(a_shape, middle, value, a_to, a_toValue, a_finest, a_keys);
		}

		// one loop of a shape that repeats after a_period seconds: at most kMostKeys keys, however sharp the shape is
		template <class F>
		[[nodiscard]] Loop Trace(const F& a_shape, double a_period)
		{
			Loop out;
			out.period = static_cast<float>(a_period);
			const double first = a_shape(0.0);
			double       from = 0.0;
			double       fromValue = first;
			out.keys.push_back({ 0.0f, static_cast<float>(first) });
			for (int i = 1; i <= kPieces; ++i) {
				const double to = a_period * i / kPieces;
				// the last key takes the first key's value: the shape repeats there, and the wrap must not show a step from rounding
				const double toValue = i == kPieces ? first : a_shape(to);
				Cut(a_shape, from, fromValue, to, toValue, a_period * kFinest, out.keys);
				out.keys.push_back({ i == kPieces ? out.period : static_cast<float>(to), static_cast<float>(toValue) });
				from = to;
				fromValue = toValue;
			}
			return out;
		}
	}

	// the loop of a light of a_kind and a_seed at a_depth, as Pulse::Factor shapes it from the session time a_start on (the moment
	// the dimmer starts playing); nothing for a shape that does not repeat, and for a light that does not pulse
	[[nodiscard]] inline std::optional<Loop> Bake(Pulse::Kind a_kind, std::uint32_t a_seed, float a_depth, double a_start)
	{
		const double period = Period(a_kind);
		if (!(period > 0.0) || !(a_depth > 0.0f) || !std::isfinite(a_start)) {
			return std::nullopt;
		}
		return Detail::Trace([&](double a_time) { return static_cast<double>(Pulse::Factor(a_kind, a_start + a_time, a_seed, a_depth)); }, period);
	}

	// what the dimmer shows a_seconds after it started playing a_loop: the straight line between the two keys round that moment
	[[nodiscard]] inline float At(const Loop& a_loop, double a_seconds)
	{
		if (a_loop.keys.empty() || !(a_loop.period > 0.0f)) {
			return 1.0f;
		}
		const double period = static_cast<double>(a_loop.period);
		const double in = a_seconds - std::floor(a_seconds / period) * period;
		std::size_t  low = 0;
		std::size_t  high = a_loop.keys.size() - 1;
		while (high - low > 1) {
			const std::size_t middle = (low + high) / 2;
			(static_cast<double>(a_loop.keys[middle].time) <= in ? low : high) = middle;
		}
		const auto&  a = a_loop.keys[low];
		const auto&  b = a_loop.keys[high];
		const double span = static_cast<double>(b.time) - static_cast<double>(a.time);
		const double share = span > 0.0 ? (in - static_cast<double>(a.time)) / span : 0.0;
		return static_cast<float>(static_cast<double>(a.value) + (static_cast<double>(b.value) - static_cast<double>(a.value)) * share);
	}
}
