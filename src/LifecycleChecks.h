// Illuminated - Core: the off-game checks of Lifecycle.h (no game needed), run by the core's own tests: call Run with the test's
// check function and add kCount to its floor
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.

#pragma once

#include "Lifecycle.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace LifecycleChecks
{
	using Check = void (*)(bool a_ok, const char* a_what, int a_line);

	// the checks one Run makes (raise it when checks are added)
	inline constexpr int kCount = 17;

	namespace Detail
	{
		// what the parts of the checks did, in the order they did it: one letter per call, the moment's number after it
		inline std::string gTrail;
		inline int         gFaults = 0;
		inline bool        gLastOk = true;

		inline void First(Lifecycle::Moment a_moment, bool a_ok)
		{
			gTrail += 'a';
			gTrail += static_cast<char>('0' + static_cast<int>(a_moment));
			gLastOk = a_ok;
		}

		inline void Throws(Lifecycle::Moment, bool)
		{
			gTrail += 'x';
			throw std::runtime_error("a part's work failed");
		}

		inline void Last(Lifecycle::Moment a_moment, bool)
		{
			gTrail += 'c';
			gTrail += static_cast<char>('0' + static_cast<int>(a_moment));
		}

		inline void Fault(std::string_view a_part, Lifecycle::Moment) noexcept
		{
			++gFaults;
			gTrail += '!';
			gTrail += a_part.empty() ? '?' : a_part.front();
		}
	}

// a check may hold a braced list with commas of its own
#define CORE_CHECK(...) a_check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

	inline void Run(Check a_check)
	{
		using namespace Lifecycle;
		using Detail::gFaults;
		using Detail::gLastOk;
		using Detail::gTrail;
		const std::array<Part, 4> parts{ Part{ { "first", {}, false }, Detail::First }, Part{ { "broken", {}, false }, Detail::Throws },
			Part{ { "silent", {}, false }, nullptr }, Part{ { "last", {}, false }, Detail::Last } };
		const auto                all = [](std::string_view) { return true; };
		const auto                none = [](std::string_view) { return false; };
		const auto                fresh = [] {
			gTrail.clear();
			gFaults = 0;
			gLastOk = true;
		};

		// every awake part is called once, in the parts' order; a part with no work is passed over
		fresh();
		CORE_CHECK(Lifecycle::Run(parts, Moment::kDataLoad, true, all, Detail::Fault) == 2);
		// an error in one part is told once with that part's id and stops neither the part before nor the part after
		CORE_CHECK(gTrail == "a1x!bc1");
		CORE_CHECK(gFaults == 1);

		// a part that is asleep is never called, at any moment
		fresh();
		CORE_CHECK(Lifecycle::Run(parts, Moment::kPostLoad, true, none, Detail::Fault) == 0);
		CORE_CHECK(gTrail.empty() && gFaults == 0);
		for (std::size_t i = 0; i < kMoments; ++i) {
			fresh();
			const auto only = [](std::string_view a_id) { return a_id == "last"; };
			const auto done = Lifecycle::Run(parts, static_cast<Moment>(i), true, only, Detail::Fault);
			CORE_CHECK(done == 1 && gTrail == std::string("c") + static_cast<char>('0' + static_cast<int>(i)) && gFaults == 0);
		}

		// the save's own verdict reaches the part
		fresh();
		const auto first = [](std::string_view a_id) { return a_id == "first"; };
		CORE_CHECK(Lifecycle::Run(parts, Moment::kPostLoadGame, false, first, Detail::Fault) == 1 && !gLastOk);
		// no fault function is no crash
		fresh();
		CORE_CHECK(Lifecycle::Run(parts, Moment::kNewGame, true, all, nullptr) == 2 && gTrail == "a4xc4" && gFaults == 0);
		// an error while asking whether a part is awake counts against that part and stops no other
		fresh();
		const auto asks = [](std::string_view a_id) -> bool {
			if (a_id == "first") {
				throw std::runtime_error("the question itself failed");
			}
			return a_id == "last";
		};
		CORE_CHECK(Lifecycle::Run(parts, Moment::kDataLoad, true, asks, Detail::Fault) == 1 && gTrail == "!fc1" && gFaults == 1);
		// no parts at all is nothing to do
		fresh();
		CORE_CHECK(Lifecycle::Run({}, Moment::kDataLoad, true, all, Detail::Fault) == 0 && gTrail.empty());
		// every moment has a name of its own, and a number that is no moment has none
		bool named = true;
		for (std::size_t i = 0; i < kMoments; ++i) {
			named = named && !NameOf(static_cast<Moment>(i)).empty() && NameOf(static_cast<Moment>(i)) != "no moment";
			for (std::size_t j = 0; j < i; ++j) {
				named = named && NameOf(static_cast<Moment>(i)) != NameOf(static_cast<Moment>(j));
			}
		}
		CORE_CHECK(named && NameOf(Moment::kTotal) == "no moment" && kMoments == 7);
	}

#undef CORE_CHECK
}
