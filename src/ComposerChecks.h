// Illuminated - Core: the off-game checks of Composer.h (no game needed), run by the core's own tests: call Run with the test's
// check function and add kCount to its floor
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.

#pragma once

#include "Composer.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace ComposerChecks
{
	using Check = void (*)(bool a_ok, const char* a_what, int a_line);

	// the checks one Run makes (raise it when checks are added)
	inline constexpr int kCount = 49;

// a check may hold a braced list with commas of its own
#define CORE_CHECK(...) a_check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

	inline void Run(Check a_check)
	{
		using namespace Composer;
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float inf = std::numeric_limits<float>::infinity();

		// a new light: every voice silent, its product its data's fade, and it has to be written once
		Light light;
		light.base = 2.0f;
		CORE_CHECK(Product(light) == 2.0f);
		CORE_CHECK(Due(light));
		CORE_CHECK(Take(light) == 2.0f && light.written == 2.0f);
		CORE_CHECK(!Due(light));

		// a voice that says what it said before asks for no write
		CORE_CHECK(!Set(light, Voice::kSetting, 1.0f));
		// a voice that moves the product asks for one, and the write settles it
		CORE_CHECK(Set(light, Voice::kSetting, 0.5f));
		CORE_CHECK(Product(light) == 1.0f && Due(light));
		CORE_CHECK(Take(light) == 1.0f && !Due(light));
		// every voice multiplies in
		CORE_CHECK(Set(light, Voice::kKind, 0.5f) && Take(light) == 0.5f);
		CORE_CHECK(Set(light, Voice::kElement, 2.0f) && Take(light) == 1.0f);
		CORE_CHECK(Set(light, Voice::kFading, 0.25f) && Take(light) == 0.25f);
		CORE_CHECK(Set(light, Voice::kOtherMod, 0.5f) && Take(light) == 0.125f);
		CORE_CHECK(Set(light, Voice::kChoice, 4.0f) && Take(light) == 0.5f);
		CORE_CHECK(Set(light, Voice::kOutside, 2.0f) && Take(light) == 1.0f);
		// and each can be taken back alone
		CORE_CHECK(Set(light, Voice::kFading, 1.0f) && Take(light) == 4.0f);

		// a change too small to see asks for no write, but small steps add up against what the light last GOT, not against the
		// step before: a setting dragged slowly still reaches the light
		Light slow;
		CORE_CHECK(Take(slow) == 1.0f);
		CORE_CHECK(!Set(slow, Voice::kSetting, 1.0f - kSeen * 0.5f));
		CORE_CHECK(!Set(slow, Voice::kSetting, 1.0f - kSeen * 0.9f));
		CORE_CHECK(Set(slow, Voice::kSetting, 1.0f - kSeen * 1.5f));
		CORE_CHECK(slow.written == 1.0f);
		CORE_CHECK(Take(slow) < 1.0f && !Due(slow));

		// to dark and back from dark always count, however small the fade on the lit side
		Light dark;
		dark.base = 0.0001f;
		CORE_CHECK(Take(dark) == 0.0001f);
		CORE_CHECK(Set(dark, Voice::kFading, 0.0f) && Product(dark) == 0.0f);
		CORE_CHECK(Take(dark) == 0.0f && !Due(dark));
		CORE_CHECK(!Set(dark, Voice::kSetting, 0.5f));
		CORE_CHECK(Set(dark, Voice::kFading, 1.0f) && Take(dark) > 0.0f);

		// a broken or negative share is no say at all; one past the cap is the cap
		CORE_CHECK(Clean(nan) == 1.0f && Clean(-0.5f) == 1.0f && Clean(-inf) == 1.0f);
		CORE_CHECK(Clean(inf) == kMost && Clean(kMost * 2.0f) == kMost);
		CORE_CHECK(Clean(0.0f) == 0.0f && Clean(0.75f) == 0.75f && Clean(kMost) == kMost);
		Light odd;
		CORE_CHECK(Take(odd) == 1.0f);
		CORE_CHECK(!Set(odd, Voice::kKind, nan) && odd.share[static_cast<std::size_t>(Voice::kKind)] == 1.0f);
		CORE_CHECK(!Set(odd, Voice::kKind, -3.0f) && Product(odd) == 1.0f);
		// the shares together never lift a light past the cap, and a product is never negative or broken
		CORE_CHECK(Set(odd, Voice::kKind, kMost) && Set(odd, Voice::kElement, kMost));
		CORE_CHECK(Product(odd) == kMost);
		odd.base = -1.0f;
		CORE_CHECK(Product(odd) == 0.0f);
		odd.base = nan;
		CORE_CHECK(Product(odd) == 0.0f);
		odd.base = inf;
		CORE_CHECK(Product(odd) == 0.0f);
		// the largest fade a file can hold, with shares above 1 on it, is still a number
		odd.base = (std::numeric_limits<float>::max)();
		CORE_CHECK(Product(odd) == (std::numeric_limits<float>::max)() && Due(odd));
		odd.base = 1.0f;
		odd.share[0] = nan;
		// a broken share written straight into the record is no say at all: the other shares still count
		CORE_CHECK(Product(odd) == kMost);
		// and a zero share beside it keeps the light dark
		odd.share[static_cast<std::size_t>(Voice::kFading)] = 0.0f;
		CORE_CHECK(Product(odd) == 0.0f);

		// tiny and large shares together give the brightness they multiply to, in any order: nothing is lost on the way
		Light wide;
		wide.base = std::ldexp(1.0f, 127);
		wide.share = { std::ldexp(1.0f, -75), std::ldexp(1.0f, -75), 16.0f, 16.0f, 16.0f, 16.0f, 16.0f };
		Light turned = wide;
		turned.share = { 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, std::ldexp(1.0f, -75), std::ldexp(1.0f, -75) };
		CORE_CHECK(Product(wide) == 0.125f && Product(turned) == 0.125f);

		// a voice that does not exist changes nothing
		Light same;
		CORE_CHECK(Take(same) == 1.0f);
		CORE_CHECK(!Set(same, Voice::kTotal, 0.0f) && Product(same) == 1.0f);
		CORE_CHECK(!Set(same, static_cast<Voice>(200), 0.0f) && Product(same) == 1.0f);

		// a light made anew has to be written again though no voice moved; its shares are kept
		Light again;
		again.base = 3.0f;
		CORE_CHECK(Set(again, Voice::kSetting, 0.5f) && Take(again) == 1.5f && !Due(again));
		Forget(again);
		CORE_CHECK(Due(again) && Product(again) == 1.5f);
		CORE_CHECK(Take(again) == 1.5f && !Due(again));

		// a hundred lights nothing happens to: none asks for a write
		bool quiet = true;
		for (int i = 0; i < 100; ++i) {
			Light one;
			one.base = 0.5f + static_cast<float>(i) * 0.01f;
			(void)Take(one);
			quiet = quiet && !Due(one) && !Set(one, Voice::kSetting, 1.0f) && !Set(one, Voice::kFading, 1.0f);
		}
		CORE_CHECK(quiet);
		CORE_CHECK(kVoices == 7);
	}

#undef CORE_CHECK
}
