// Illuminated - Core: one light's brightness as ONE product of everything that has a say on it
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// Before the core, up to four passes wrote one light every frame (a slider, the strength of its kind, the fading, a flicker) and
// told each other by handshakes what the last one had written. Here a light has one record: the fade its data gives it and one
// share per voice. A voice changes its share when something happens (a setting moved, a value the game wrote, an effect's start
// or end); the product is worked out then, and the light is written only when that product is no longer what the light last got.
// A light nothing happens to is never touched. A factor that moves all the time (a pulse) is not a share here: it is a loop of
// keys the game's own dimmer plays (DimmerKeys.h), and the product is the fade those keys are scaled by.
// Nothing of the game in this file.

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Composer
{
	// the voices on a light's brightness; each share is 1 while its voice has nothing to say
	enum class Voice : std::uint8_t
	{
		// the brightness setting of the light's group
		kSetting,
		// the strength set for the light's kind (a hand, a bolt, a rune)
		kKind,
		// the strength set for its element
		kElement,
		// the fading module's share (a charge running out, magicka spent)
		kFading,
		// another mod's dimming seen on the same light
		kOtherMod,
		// what an item or an option picked for it
		kChoice,
		// a plugin outside the core that scales the light
		kOutside,
		kTotal
	};

	inline constexpr std::size_t kVoices = static_cast<std::size_t>(Voice::kTotal);

	// a change of the product smaller than this share of it cannot be seen and is not worth a write
	inline constexpr float kSeen = 1.0f / 512.0f;
	// no light is written brighter than this many times its data's fade, whatever the shares multiply to
	inline constexpr float kMost = 16.0f;

	struct Light
	{
		// the fade the light's data gives it
		float                      base{ 1.0f };
		std::array<float, kVoices> share{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
		// what the light last got; not a number while it got nothing yet
		float written{ std::numeric_limits<float>::quiet_NaN() };
	};

	static_assert(kVoices == 7, "a new voice needs its 1 in Light::share");

	// a share as it is kept: a broken or negative number is no say at all (1 and 0 would both be wrong guesses, and 1 leaves the
	// light as its data made it)
	[[nodiscard]] constexpr float Clean(float a_share) noexcept
	{
		return (a_share >= 0.0f && a_share <= kMost) ? a_share : (a_share > kMost ? kMost : 1.0f);
	}

	// the light's steady fade: its data's fade times every share, never negative, never past kMost times the data's fade
	[[nodiscard]] constexpr float Product(const Light& a_light) noexcept
	{
		// each share cleaned as it is read (a share written past Set is held to the same rule), and the product worked out in
		// double: seven shares of 0 to kMost cannot underflow or overflow there in any order, so a brightness a light can take is
		// never lost on the way to it
		double all = 1.0;
		for (const float share : a_light.share) {
			all *= static_cast<double>(Clean(share));
		}
		if (all > static_cast<double>(kMost)) {
			all = static_cast<double>(kMost);
		}
		// a fade from a file may be anything: a negative, broken or endless one is no light at all
		constexpr float kLargest = (std::numeric_limits<float>::max)();
		const bool      usable = a_light.base >= 0.0f && a_light.base <= kLargest;
		const double    product = (usable ? static_cast<double>(a_light.base) : 0.0) * all;
		// a very large fade times its shares may leave the numbers a light can take
		return product <= static_cast<double>(kLargest) ? static_cast<float>(product) : kLargest;
	}

	// whether the light has to be written: it got nothing yet, or its product moved visibly away from what it last got. Going to
	// dark and coming back from dark always count
	[[nodiscard]] inline bool Due(const Light& a_light) noexcept
	{
		const float now = Product(a_light);
		if (std::isnan(a_light.written)) {
			return true;
		}
		if ((now == 0.0f) != (a_light.written == 0.0f)) {
			return true;
		}
		const float larger = now > a_light.written ? now : a_light.written;
		return std::abs(now - a_light.written) > larger * kSeen;
	}

	// one voice's new share; true when the light now has to be written
	[[nodiscard]] inline bool Set(Light& a_light, Voice a_voice, float a_share) noexcept
	{
		const auto at = static_cast<std::size_t>(a_voice);
		if (at >= kVoices) {
			return false;
		}
		a_light.share[at] = Clean(a_share);
		return Due(a_light);
	}

	// the fade to write now, noted as written
	[[nodiscard]] inline float Take(Light& a_light) noexcept
	{
		a_light.written = Product(a_light);
		return a_light.written;
	}

	// the light was made anew (the engine dropped it, a new 3D): whatever it got before is gone with it
	inline void Forget(Light& a_light) noexcept
	{
		a_light.written = std::numeric_limits<float>::quiet_NaN();
	}
}
