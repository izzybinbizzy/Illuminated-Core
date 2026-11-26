// Illuminated - Core: the off-game checks of DimmerKeys.h (no game needed), run by the core's own tests: call Run with the test's
// check function and add kCount to its floor
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.

#pragma once

#include "DimmerKeys.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace DimmerKeysChecks
{
	using Check = void (*)(bool a_ok, const char* a_what, int a_line);

	// the checks one Run makes (raise it when checks are added)
	inline constexpr int kCount = 1250;

// a check may hold a braced list with commas of its own
#define CORE_CHECK(...) a_check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

	inline void Run(Check a_check)
	{
		using namespace DimmerKeys;
		using Pulse::Kind;
		constexpr std::array kLooping{ Kind::kHealing, Kind::kAbsorbHealth, Kind::kAbsorbMagicka, Kind::kAbsorbStamina, Kind::kIllusion,
			Kind::kConjuration, Kind::kAlteration, Kind::kMagelight, Kind::kWard, Kind::kRune };
		constexpr std::array kEndless{ Kind::kNone, Kind::kFire, Kind::kFrost, Kind::kShock, Kind::kPoison };

		// a shape that never repeats, a light that does not pulse and a broken clock give no loop
		for (const auto kind : kEndless) {
			CORE_CHECK(Period(kind) == 0.0 && !Bake(kind, 7, 1.0f, 0.0).has_value());
		}
		CORE_CHECK(!Bake(Kind::kHealing, 7, 0.0f, 0.0).has_value() && !Bake(Kind::kHealing, 7, -1.0f, 0.0).has_value());
		CORE_CHECK(!Bake(Kind::kHealing, 7, 1.0f, std::nan("")).has_value());

		std::size_t most = 0;
		double      worst = 0.0;
		for (const auto kind : kLooping) {
			const double period = Period(kind);
			CORE_CHECK(period > 0.0);
			// the shape truly repeats after its period, whatever the light's seed (else a loop would drift from it)
			for (const std::uint32_t seed : { 1u, 77u, 0xDEADBEEFu }) {
				double apart = 0.0;
				for (int i = 0; i < 400; ++i) {
					const double t = 3.7 + i * 0.0173;
					apart = (std::max)(apart, static_cast<double>(std::abs(Pulse::Factor(kind, t, seed) - Pulse::Factor(kind, t + period, seed))));
				}
				CORE_CHECK(apart < 1e-5);
			}
			for (const std::uint32_t seed : { 1u, 77u, 0xDEADBEEFu, 0u }) {
				for (const double start : { 0.0, 123.456, 86400.25 }) {
					for (const float depth : { 1.0f, 0.5f }) {
						const auto loop = Bake(kind, seed, depth, start);
						CORE_CHECK(loop.has_value());
						if (!loop) {
							continue;
						}
						const auto& keys = loop->keys;
						most = (std::max)(most, keys.size());
						// the first key at 0, the last at the period with the first key's value, times strictly rising, values in Factor's range
						CORE_CHECK(keys.size() >= 17 && keys.size() <= kMostKeys && keys.front().time == 0.0f && keys.back().time == loop->period);
						CORE_CHECK(keys.back().value == keys.front().value && loop->period == static_cast<float>(period));
						bool rising = true;
						bool inRange = true;
						for (std::size_t i = 0; i < keys.size(); ++i) {
							rising = rising && (i == 0 || keys[i].time > keys[i - 1].time);
							inRange = inRange && keys[i].value >= 0.05f && keys[i].value <= 2.0f;
						}
						CORE_CHECK(rising && inRange);
						// played by straight lines, the loop stays within the tolerance of the shape, over three turns of it
						double off = 0.0;
						for (int i = 0; i < 6000; ++i) {
							const double t = period * 3.0 * i / 6000.0;
							off = (std::max)(off, static_cast<double>(std::abs(At(*loop, t) - Pulse::Factor(kind, start + t, seed, depth))));
						}
						worst = (std::max)(worst, off);
						CORE_CHECK(off <= 2.5 * kTolerance);
					}
				}
			}
		}
		// each light keeps its own place in the shape: two seeds of one kind do not move together
		const auto one = Bake(Kind::kHealing, 1, 1.0f, 0.0);
		const auto two = Bake(Kind::kHealing, 2, 1.0f, 0.0);
		CORE_CHECK(one && two && std::abs(At(*one, 0.5) - At(*two, 0.5)) > 0.001f);
		// an empty loop plays as a steady light
		CORE_CHECK(At(Loop{}, 1.0) == 1.0f);
		// a shape too sharp for any number of keys stops at the most a loop may hold, the pieces' end keys included, times still rising
		const auto jagged = Detail::Trace([](double a_time) { return 1.0 + 0.5 * std::sin(a_time * 4000.0); }, 1.0);
		bool       climbing = true;
		for (std::size_t i = 1; i < jagged.keys.size(); ++i) {
			climbing = climbing && jagged.keys[i].time > jagged.keys[i - 1].time;
		}
		CORE_CHECK(jagged.keys.size() <= kMostKeys && jagged.keys.size() > kMostKeys / 2 && climbing);
	}

#undef CORE_CHECK
}
