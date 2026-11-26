// Illuminated - Core: a light's pulse as keys the game's own dimmer plays
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// Pulse.h gives each kind of spell its shape as a function of time, worked out by our code for every shaped light on every frame.
// Here a shape is turned, once, when the light is made, into one loop of keys for the game's light dimmer (a controller the
// engine runs itself, with straight lines between keys): after that no code of ours runs for the pulse. Ten shapes repeat
// exactly and are traced within a stated tolerance. The four that never repeat (fire, frost, shock, poison) play a loop of
// 12 seconds instead, the way Illuminated - Creature Lighting NG plays its flicker (his checklist 927): a key every thirtieth of a
// second, the last quarter second easing to the first value so nothing jumps where the loop starts again. Both are checked off
// the game (tests\testcore.cpp). Nothing of the game in this file.

#pragma once

#include "Pulse.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
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
	// a repeating shape is read on this many points to a loop, and every key sits on one of them: the lines are held to half the
	// tolerance there, and the other half is room for what a corner falling between two points can add (the sharpest, a
	// heartbeat's at the shapes as designed, adds at most about 0.0007 on this grid)
	inline constexpr std::size_t kGrid = 8192;
	// a corner's error between two points grows with the swing: a loop wider than the shapes as designed is read on this many
	// points, so a light as shipped keeps the cost of the grid above
	inline constexpr std::size_t kGridWide = 16384;
	// the widest swing a loop is baked at: twice the shapes as designed. The wide grid holds the tolerance up to it, and the
	// checks prove it there; a wider one is held to this
	inline constexpr float       kMostDepth = 2.0f;
	inline constexpr std::size_t kMostKeys = 1024;
	// a loop is cut into this many even pieces first, so a shape with several beats in one loop has each of them looked at
	inline constexpr std::size_t kPieces = 16;

	// the loop of a shape that never repeats, its eased last stretch, and its keys a second (Creatures' shipped values; the
	// shortest shape, a shock spike, is 45 ms)
	inline constexpr double kNoiseLoop = 12.0;
	inline constexpr double kSeam = 0.25;
	inline constexpr double kNoiseKeysPerSecond = 30.0;

	// whether a_kind's shape never repeats by itself (fire, frost, shock, poison): such a shape is looped, not traced
	[[nodiscard]] constexpr bool IsNoise(Pulse::Kind a_kind) noexcept
	{
		return a_kind == Pulse::Kind::kFire || a_kind == Pulse::Kind::kFrost || a_kind == Pulse::Kind::kShock || a_kind == Pulse::Kind::kPoison;
	}

	// the length of a_kind's loop: the seconds after which a repeating shape repeats exactly, kNoiseLoop for the four that never
	// repeat, 0 for a light that does not pulse
	[[nodiscard]] constexpr double Period(Pulse::Kind a_kind) noexcept
	{
		if (IsNoise(a_kind)) {
			return kNoiseLoop;
		}
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
		default:
			return 0.0;
		}
	}

	namespace Detail
	{
		// the points a loop of a_depth is read on: the wide grid only for a swing wider than the shapes as designed
		[[nodiscard]] constexpr std::size_t GridFor(float a_depth) noexcept { return a_depth > 1.0f ? kGridWide : kGrid; }

		// one stretch between two kept grid points: the point between them farthest from their straight line, and how far
		struct Span
		{
			std::size_t from{ 0 };
			std::size_t to{ 0 };
			std::size_t worst{ 0 };
			double      off{ 0.0 };
		};

		[[nodiscard]] inline Span Measure(const std::vector<double>& a_value, std::size_t a_from, std::size_t a_to)
		{
			Span         span{ a_from, a_to, a_from, 0.0 };
			const double rise = (a_value[a_to] - a_value[a_from]) / static_cast<double>(a_to - a_from);
			for (std::size_t i = a_from + 1; i < a_to; ++i) {
				const double off = std::abs(a_value[i] - (a_value[a_from] + rise * static_cast<double>(i - a_from)));
				if (off > span.off) {
					span.off = off;
					span.worst = i;
				}
			}
			return span;
		}

		// one loop of a shape that repeats after a_period seconds: the shape read on a_grid + 1 points (GridFor), then keys added
		// where the straight lines stray most, worst first, until every point is within half the tolerance or the loop holds
		// kMostKeys
		template <class F>
		[[nodiscard]] Loop Trace(const F& a_shape, double a_period, std::size_t a_grid = kGrid)
		{
			std::vector<double> value(a_grid + 1);
			for (std::size_t i = 0; i < a_grid; ++i) {
				value[i] = a_shape(a_period * static_cast<double>(i) / static_cast<double>(a_grid));
			}
			// the last point takes the first point's value: the shape repeats there, and the wrap must not show a step from rounding
			value[a_grid] = value[0];
			const auto                                                     lesser = [](const Span& a_one, const Span& a_two) { return a_one.off < a_two.off; };
			std::priority_queue<Span, std::vector<Span>, decltype(lesser)> spans(lesser);
			std::vector<bool>                                              kept(a_grid + 1, false);
			for (std::size_t piece = 0; piece < kPieces; ++piece) {
				kept[a_grid * piece / kPieces] = true;
				spans.push(Measure(value, a_grid * piece / kPieces, a_grid * (piece + 1) / kPieces));
			}
			kept[a_grid] = true;
			std::size_t count = kPieces + 1;
			// one key of room is kept: a loop turned to a light's own place (Turn) gains a key where it now starts
			while (!spans.empty() && count + 1 < kMostKeys && spans.top().off > 0.5 * kTolerance) {
				const Span span = spans.top();
				spans.pop();
				kept[span.worst] = true;
				++count;
				spans.push(Measure(value, span.from, span.worst));
				spans.push(Measure(value, span.worst, span.to));
			}
			Loop out;
			out.period = static_cast<float>(a_period);
			out.keys.reserve(count);
			for (std::size_t i = 0; i <= a_grid; ++i) {
				if (kept[i]) {
					const float time = i == a_grid ? out.period : static_cast<float>(a_period * static_cast<double>(i) / static_cast<double>(a_grid));
					out.keys.push_back({ time, static_cast<float>(value[i]) });
				}
			}
			return out;
		}

		// a_seconds of a shape that never repeats, a key every 1 / kNoiseKeysPerSecond second, the last kSeam seconds easing to the
		// first value; the last key, at a_seconds, is the first value itself
		template <class F>
		[[nodiscard]] Loop Even(const F& a_shape, double a_seconds)
		{
			Loop out;
			out.period = static_cast<float>(a_seconds);
			const auto   count = static_cast<std::size_t>(std::llround(a_seconds * kNoiseKeysPerSecond));
			const double first = a_shape(0.0);
			out.keys.reserve(count + 1);
			for (std::size_t i = 0; i <= count; ++i) {
				const double t = a_seconds * static_cast<double>(i) / static_cast<double>(count);
				double       value = a_shape(t);
				const double seam = (t - (a_seconds - kSeam)) / kSeam;
				if (seam > 0.0) {
					value += (first - value) * (std::min)(seam, 1.0);
				}
				out.keys.push_back({ i == count ? out.period : static_cast<float>(t), static_cast<float>(i == count ? first : value) });
			}
			return out;
		}
	}

	// what the dimmer shows a_seconds after it started playing a_loop: the straight line between the two keys round that moment
	[[nodiscard]] inline float At(const Loop& a_loop, double a_seconds);

	// whether a light's seed moves a_kind's shape only in time (Pulse's Phase: every repeating shape but illusion, whose two waves
	// each take their own place): then one traced loop serves every light of that kind and depth, turned to the light's own place
	[[nodiscard]] constexpr bool Shifts(Pulse::Kind a_kind) noexcept
	{
		switch (a_kind) {
		case Pulse::Kind::kHealing:
		case Pulse::Kind::kAbsorbHealth:
		case Pulse::Kind::kAbsorbMagicka:
		case Pulse::Kind::kAbsorbStamina:
		case Pulse::Kind::kConjuration:
		case Pulse::Kind::kAlteration:
		case Pulse::Kind::kMagelight:
		case Pulse::Kind::kWard:
		case Pulse::Kind::kRune:
			return true;
		default:
			return false;
		}
	}

	namespace Detail
	{
		// a_loop played from a_shift seconds into it: the same straight lines, so the same distance from the shape, with one key
		// added where the turned loop now starts (its value read off the lines) and the same key again at its end
		[[nodiscard]] inline Loop Turn(const Loop& a_loop, double a_period, double a_shift)
		{
			Loop out;
			out.period = a_loop.period;
			const double shift = a_shift - std::floor(a_shift / a_period) * a_period;
			const float  first = At(a_loop, shift);
			const float  end = a_loop.period;
			out.keys.reserve(a_loop.keys.size() + 1);
			out.keys.push_back({ 0.0f, first });
			std::vector<Key> middle;
			middle.reserve(a_loop.keys.size());
			// the loop's own last key is its first again: left out, as the turned loop ends on its own first key
			for (std::size_t i = 0; i + 1 < a_loop.keys.size(); ++i) {
				double time = static_cast<double>(a_loop.keys[i].time) - shift;
				if (time < 0.0) {
					time += a_period;
				}
				const auto at = static_cast<float>(time);
				if (at > 0.0f && at < end) {
					middle.push_back({ at, a_loop.keys[i].value });
				}
			}
			std::ranges::sort(middle, {}, &Key::time);
			out.keys.insert(out.keys.end(), middle.begin(), middle.end());
			out.keys.push_back({ end, first });
			return out;
		}

		// the traced loop of a_kind at a_depth from its seed 0 and time 0, made once and kept (a few kinds and depths per session)
		[[nodiscard]] inline std::shared_ptr<const Loop> Traced(Pulse::Kind a_kind, float a_depth, double a_period)
		{
			struct Entry
			{
				Pulse::Kind                 kind{ Pulse::Kind::kNone };
				float                       depth{ 0.0f };
				std::shared_ptr<const Loop> loop{};
			};
			static std::mutex         lock;
			static std::vector<Entry> kept;
			// a session uses a handful; past this many a loop is made and not kept, so nothing grows without end
			constexpr std::size_t kMostKept = 64;
			{
				std::scoped_lock guard{ lock };
				for (const auto& entry : kept) {
					if (entry.kind == a_kind && entry.depth == a_depth) {
						return entry.loop;
					}
				}
			}
			auto loop = std::make_shared<const Loop>(
				Trace([&](double a_time) { return static_cast<double>(Pulse::Factor(a_kind, a_time, 0, a_depth)); }, a_period, GridFor(a_depth)));
			std::scoped_lock guard{ lock };
			if (kept.size() < kMostKept) {
				kept.push_back({ a_kind, a_depth, loop });
			}
			return loop;
		}
	}

	// the loop of a light of a_kind and a_seed at a_depth, as Pulse::Factor shapes it from the session time a_start on (the moment
	// the dimmer starts playing); nothing for a light that does not pulse. A shape that only moves in time with the seed is traced
	// once per kind and depth and turned to each light's place, so a burst of new lights costs a copy of keys each. A depth past
	// kMostDepth is baked at kMostDepth, so such a loop swings less than Pulse::Factor at that depth would
	[[nodiscard]] inline std::optional<Loop> Bake(Pulse::Kind a_kind, std::uint32_t a_seed, float a_depth, double a_start)
	{
		const double period = Period(a_kind);
		if (!(period > 0.0) || !(a_depth > 0.0f) || !std::isfinite(a_start)) {
			return std::nullopt;
		}
		// past kMostDepth the grid no longer holds the tolerance: a wider swing is baked at the widest one that is proven
		const float depth = (std::min)(a_depth, kMostDepth);
		if (Shifts(a_kind)) {
			// Pulse's Phase puts seed s at Unit(s, 0x51ED) of the cycle: the same place as seed 0 that many periods later
			const double place = (Pulse::Unit(a_seed, 0x51ED) - Pulse::Unit(0, 0x51ED)) * period;
			return Detail::Turn(*Detail::Traced(a_kind, depth, period), period, a_start + place);
		}
		const auto shape = [&](double a_time) { return static_cast<double>(Pulse::Factor(a_kind, a_start + a_time, a_seed, depth)); };
		return IsNoise(a_kind) ? Detail::Even(shape, period) : Detail::Trace(shape, period, Detail::GridFor(depth));
	}

	inline float At(const Loop& a_loop, double a_seconds)
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
