// Illuminated Core Framework: the off-game checks of DimmerKeys.h (no game needed), run by the core's own tests: call Run with the test's
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
	inline constexpr int kCount = 2721;

// a check may hold a braced list with commas of its own
#define CORE_CHECK(...) a_check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

	inline void Run(Check a_check)
	{
		using namespace DimmerKeys;
		using Pulse::Kind;
		constexpr std::array kLooping{ Kind::kHealing, Kind::kAbsorbHealth, Kind::kAbsorbMagicka, Kind::kAbsorbStamina, Kind::kIllusion,
			Kind::kConjuration, Kind::kAlteration, Kind::kMagelight, Kind::kWard, Kind::kRune };
		constexpr std::array kNoise{ Kind::kFire, Kind::kFrost, Kind::kShock, Kind::kPoison };

		// a light that does not pulse and a broken clock give no loop
		CORE_CHECK(Period(Kind::kNone) == 0.0 && !IsNoise(Kind::kNone) && !Bake(Kind::kNone, 7, 1.0f, 0.0).has_value());
		CORE_CHECK(!Bake(Kind::kHealing, 7, 0.0f, 0.0).has_value() && !Bake(Kind::kHealing, 7, -1.0f, 0.0).has_value());
		CORE_CHECK(!Bake(Kind::kHealing, 7, 1.0f, std::nan("")).has_value());
		// a swing past the widest proven one is baked at that one (the test part asks up to 20 for an overshoot)
		// on every route a loop takes: traced once and turned, traced per light, and played as a 12-second loop
		for (const auto kind : { Kind::kAbsorbHealth, Kind::kIllusion, Kind::kFire }) {
			const auto wide = Bake(kind, 3, 20.0f, 0.0);
			const auto widest = Bake(kind, 3, kMostDepth, 0.0);
			CORE_CHECK(wide && widest && wide->keys.size() == widest->keys.size() && At(*wide, 0.37) == At(*widest, 0.37));
		}
		// a light as shipped keeps the cheaper grid; only a wider swing is read on the wide one
		CORE_CHECK(Detail::GridFor(1.0f) == kGrid && Detail::GridFor(0.5f) == kGrid && Detail::GridFor(kMostDepth) == kGridWide);

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
					for (const float depth : { 1.0f, 0.5f, kMostDepth }) {
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
						CORE_CHECK(off <= kTolerance);
					}
				}
			}
		}
		// a heartbeat whose peak falls inside the first piece, a few hundredths of a millisecond in (CodeRabbit's case on pull request 1):
		// read finely across that piece, the loop stays within the tolerance there too, on the grid as shipped and at the widest swing
		for (const float depth : { 1.0f, kMostDepth }) {
			const double beatStart = 1.0597 - Pulse::Unit(1, 0x51ED);
			const auto   beat = Bake(Kind::kAbsorbHealth, 1, depth, beatStart);
			double       beatOff = beat ? 0.0 : 1.0;
			for (int i = 0; beat && i <= 20000; ++i) {
				const double t = 0.1 * i / 20000.0;
				beatOff = (std::max)(beatOff, static_cast<double>(std::abs(At(*beat, t) - Pulse::Factor(Kind::kAbsorbHealth, beatStart + t, 1, depth))));
			}
			CORE_CHECK(beatOff <= kTolerance);
		}

		// the four shapes that never repeat play a loop of 12 seconds (his checklist 927), Creatures' way
		for (const auto kind : kNoise) {
			CORE_CHECK(IsNoise(kind) && Period(kind) == kNoiseLoop);
			for (const std::uint32_t seed : { 1u, 77u, 0xDEADBEEFu, 0u }) {
				for (const double start : { 0.0, 123.456, 86400.25 }) {
					for (const float depth : { 1.0f, 0.5f, kMostDepth }) {
						const auto loop = Bake(kind, seed, depth, start);
						CORE_CHECK(loop.has_value());
						if (!loop) {
							continue;
						}
						const auto& keys = loop->keys;
						// a key every thirtieth of a second over the whole loop, the first at 0, the last at its end
						CORE_CHECK(keys.size() == 361 && keys.front().time == 0.0f && keys.back().time == loop->period);
						CORE_CHECK(keys.back().value == keys.front().value && loop->period == static_cast<float>(kNoiseLoop));
						bool rising = true;
						bool inRange = true;
						for (std::size_t i = 0; i < keys.size(); ++i) {
							rising = rising && (i == 0 || keys[i].time > keys[i - 1].time);
							inRange = inRange && keys[i].value >= 0.05f && keys[i].value <= 2.0f;
						}
						CORE_CHECK(rising && inRange);
						// before the seam every key is the shape itself at its moment
						double off = 0.0;
						for (std::size_t i = 0; i < keys.size(); ++i) {
							const double t = kNoiseLoop * static_cast<double>(i) / static_cast<double>(keys.size() - 1);
							if (t < kNoiseLoop - kSeam) {
								off = (std::max)(off, static_cast<double>(std::abs(keys[i].value - Pulse::Factor(kind, start + t, seed, depth))));
							}
						}
						CORE_CHECK(off < 1e-5);
						// every turn of the loop plays the same
						CORE_CHECK(std::abs(At(*loop, 1.234) - At(*loop, 1.234 + kNoiseLoop)) < 1e-5f &&
								   std::abs(At(*loop, 7.77) - At(*loop, 7.77 + 3.0 * kNoiseLoop)) < 1e-5f);
					}
				}
			}
		}
		// two fire lights do not flicker together
		const auto fireOne = Bake(Kind::kFire, 1, 1.0f, 0.0);
		const auto fireTwo = Bake(Kind::kFire, 2, 1.0f, 0.0);
		double     fireApart = 0.0;
		for (int i = 0; fireOne && fireTwo && i < 120; ++i) {
			fireApart = (std::max)(fireApart, static_cast<double>(std::abs(At(*fireOne, i * 0.1) - At(*fireTwo, i * 0.1))));
		}
		CORE_CHECK(fireApart > 0.01);
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
