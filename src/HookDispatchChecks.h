// Illuminated Core Framework: the off-game checks of HookDispatch.h (no game needed), run by the core's own tests: call Run with the test's
// check function and add kCount to its floor
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.

#pragma once

#include "HookDispatch.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace HookDispatchChecks
{
	using Check = void (*)(bool a_ok, const char* a_what, int a_line);

	// the checks one Run makes (raise it when checks are added)
	inline constexpr int kCount = 59;

	// what ran, in order: the stand-in for the game's function writes "game", each handler its own word
	inline std::string gRan;
	inline std::string gFaults;

	inline int Game(int a_value)
	{
		gRan += "game ";
		return a_value * 2;
	}

	inline int Same(int a_value)
	{
		return a_value;
	}

	inline void GameVoid(int* a_value)
	{
		gRan += "game ";
		++*a_value;
	}

	// a handler's work that takes a known time: busy until 200 microseconds have gone by on the meter's own clock
	inline void Spin()
	{
		const auto end = std::chrono::steady_clock::now() + std::chrono::microseconds(200);
		while (std::chrono::steady_clock::now() < end) {
		}
	}

	inline void Fault(const char* a_part, const char* a_slot) noexcept
	{
		gFaults += std::string(a_part) + "@" + a_slot + " ";
	}

// a check may hold a braced list with commas of its own
#define CORE_CHECK(...) a_check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

	inline void Run(Check a_check)
	{
		using namespace HookDispatch;
		using IntSlot = Slot<int, int>;

		// nothing asked: no hook is wanted, and a call is the game's own call alone
		static IntSlot none("none");
		CORE_CHECK(!none.Wanted() && none.Handlers() == 0);
		none.Seal(Fault);
		gRan.clear();
		CORE_CHECK(none.Call(Game, 21) == 42 && gRan == "game ");

		// handlers of three parts, before and after: none runs until it is armed
		static IntSlot slot("effect start");
		const auto     wardsBefore = slot.AddBefore("wards", [](int) { gRan += "wards-before "; });
		const auto     lightAfter = slot.AddAfter("illuminated", [](int& a_result, int a_value) { gRan += "illuminated-after(" + std::to_string(a_value) + "," + std::to_string(a_result) + ") "; });
		const auto     creaturesAfter = slot.AddAfter("creatures", [](int&, int) { gRan += "creatures-after "; });
		CORE_CHECK(wardsBefore && lightAfter && creaturesAfter && slot.Wanted() && slot.Handlers() == 3 && !slot.Sealed());
		slot.Seal(Fault);
		CORE_CHECK(slot.Sealed());
		gRan.clear();
		CORE_CHECK(slot.Call(Game, 5) == 10 && gRan == "game ");
		for (std::size_t i = 0; i < slot.Handlers(); ++i) {
			CORE_CHECK(slot.At(i).runs == 0 && !slot.At(i).armed);
		}

		// armed: before handlers ahead of the game's call, after handlers behind it, each list in the order the parts were added
		creaturesAfter.Arm();
		lightAfter.Arm();
		wardsBefore.Arm();
		gRan.clear();
		CORE_CHECK(slot.Call(Game, 5) == 10 && gRan == "wards-before game illuminated-after(5,10) creatures-after ");
		CORE_CHECK(wardsBefore.IsArmed() && slot.At(0).before && slot.At(0).armed && std::string(slot.At(0).part) == "wards" && slot.At(0).runs == 1);
		CORE_CHECK(!slot.At(1).before && std::string(slot.At(1).part) == "illuminated" && std::string(slot.At(2).part) == "creatures");

		// one handler disarms itself: only that one stops
		lightAfter.Disarm();
		gRan.clear();
		CORE_CHECK(slot.Call(Game, 1) == 2 && gRan == "wards-before game creatures-after " && !lightAfter.IsArmed());
		CORE_CHECK(slot.At(1).runs == 1 && slot.At(2).runs == 2);
		wardsBefore.Disarm();
		creaturesAfter.Disarm();
		gRan.clear();
		CORE_CHECK(slot.Call(Game, 1) == 2 && gRan == "game ");

		// once the hook is written no handler is added: the switch handed back is empty and does nothing
		const auto late = slot.AddAfter("late", [](int&, int) { gRan += "late "; });
		CORE_CHECK(!late && !late.IsArmed() && slot.Handlers() == 3);
		late.Arm();
		gRan.clear();
		CORE_CHECK(slot.Call(Game, 1) == 2 && gRan == "game ");
		CORE_CHECK(!Armed().IsArmed() && !Armed());

		// a handler after the call may change what the game is handed back; one that throws stops alone and is told once
		static IntSlot hostile("hostile");
		const auto     thrower = hostile.AddAfter("thrower", [](int&, int) { throw std::runtime_error("x"); });
		const auto     changer = hostile.AddAfter("changer", [](int& a_result, int) { a_result = 7; });
		const auto     early = hostile.AddBefore("early", [](int) { throw 3; });
		hostile.Seal(Fault);
		thrower.Arm();
		changer.Arm();
		early.Arm();
		gFaults.clear();
		gRan.clear();
		CORE_CHECK(hostile.Call(Game, 1) == 7 && gRan == "game ");
		CORE_CHECK(hostile.Call(Game, 1) == 7 && gFaults == "early@hostile thrower@hostile ");
		// with no fault function set a throw is still swallowed
		static IntSlot quiet("quiet");
		const auto     loud = quiet.AddBefore("loud", [](int) { throw 1; });
		quiet.Seal(nullptr);
		loud.Arm();
		CORE_CHECK(quiet.Call(Game, 2) == 4);

		// a function that returns nothing: the same rules, the handlers see its arguments
		static Slot<void, int*> plain("plain");
		const auto              before = plain.AddBefore("a", [](int* a_value) { *a_value += 10; });
		const auto              after = plain.AddAfter("b", [](int* a_value) { *a_value *= 3; });
		plain.Seal(Fault);
		int value = 0;
		plain.Call(GameVoid, &value);
		CORE_CHECK(value == 1);
		before.Arm();
		after.Arm();
		value = 0;
		plain.Call(GameVoid, &value);
		CORE_CHECK(value == 33);

		// a full list refuses the next handler and keeps the sixteen it has; a null handler is refused
		static IntSlot full("full");
		for (std::size_t i = 0; i < kMost; ++i) {
			CORE_CHECK(static_cast<bool>(full.AddBefore("p", [](int) {})) && static_cast<bool>(full.AddAfter("p", [](int&, int) {})));
		}
		CORE_CHECK(!full.AddBefore("p", [](int) {}) && !full.AddAfter("p", [](int&, int) {}) && full.Handlers() == 2 * kMost);
		CORE_CHECK(!full.AddBefore("p", nullptr) && !full.AddAfter("p", nullptr));
		// every bit of the armed word belongs to one handler: the last before handler and the last after handler run alone
		static IntSlot     edges("edges");
		std::vector<Armed> switches;
		for (std::size_t i = 0; i < kMost; ++i) {
			switches.push_back(edges.AddBefore("b", [](int a_value) { gRan += "b" + std::to_string(a_value) + " "; }));
		}
		for (std::size_t i = 0; i < kMost; ++i) {
			switches.push_back(edges.AddAfter("a", [](int&, int a_value) { gRan += "a" + std::to_string(a_value) + " "; }));
		}
		edges.Seal(Fault);
		switches[kMost - 1].Arm();
		switches[2 * kMost - 1].Arm();
		gRan.clear();
		CORE_CHECK(edges.Call(Game, 4) == 8 && gRan == "b4 game a4 ");
		CORE_CHECK(edges.At(kMost - 1).runs == 1 && edges.At(kMost - 2).runs == 0 && edges.At(2 * kMost - 1).runs == 1 && edges.At(2 * kMost).runs == 0);

		// arming from other threads while the hook is called: no bit of another handler is lost
		static Slot<int, int>   busy("busy");
		static std::atomic<int> counted{ 0 };
		std::vector<Armed>      bits;
		for (std::size_t i = 0; i < kMost; ++i) {
			bits.push_back(busy.AddAfter("t", [](int&, int) { counted.fetch_add(1, std::memory_order_relaxed); }));
		}
		busy.Seal(Fault);
		{
			std::vector<std::thread> threads;
			for (std::size_t t = 0; t < 4; ++t) {
				threads.emplace_back([&bits, t]() {
					for (int round = 0; round < 2000; ++round) {
						for (std::size_t i = t; i < kMost; i += 4) {
							bits[i].Arm();
							bits[i].Disarm();
						}
					}
					for (std::size_t i = t; i < kMost; i += 4) {
						bits[i].Arm();
					}
				});
			}
			for (int round = 0; round < 2000; ++round) {
				static_cast<void>(busy.Call(Same, round));
			}
			for (auto& thread : threads) {
				thread.join();
			}
		}
		counted = 0;
		CORE_CHECK(busy.Call(Same, 1) == 1 && counted == static_cast<int>(kMost));

		// one slot on two game classes: the handlers are one list, each thunk hands in its own class's function
		static IntSlot two("two classes");
		const auto     shared = two.AddAfter("p", [](int& a_result, int) { ++a_result; });
		two.Seal(Fault);
		shared.Arm();
		CORE_CHECK(two.Call(Game, 3) == 7 && two.Call(Same, 3) == 4 && two.At(0).runs == 2);

		// the meter. Off: a handler that runs is counted and never timed
		static Slot<void, int*> timed("timed");
		const auto              spinner = timed.AddBefore("p", [](int*) { Spin(); });
		const auto              timedThrower = timed.AddAfter("q", [](int*) {
			Spin();
			throw std::runtime_error("after the work");
		});
		timed.Seal(Fault);
		spinner.Arm();
		int timedCalls = 0;
		timed.Call(GameVoid, &timedCalls);
		CORE_CHECK(!timed.Metered() && timed.At(0).runs == 1 && timed.At(0).usAvg == 0.0f && timed.At(0).usMax == 0.0f);
		// on: each run's own time, the first run the average itself
		timed.Meter(true);
		timedThrower.Arm();
		timed.Call(GameVoid, &timedCalls);
		CORE_CHECK(timed.Metered() && timed.At(0).runs == 2 && timed.At(0).usAvg >= 200.0f && timed.At(0).usMax >= timed.At(0).usAvg);
		// a handler that throws is timed and counted all the same, and the game's call ran both times
		CORE_CHECK(timed.At(1).runs == 1 && timed.At(1).usMax >= 200.0f && timedCalls == 2);
		// a reset clears the counts and the times, never the meter's switch or a handler's own
		timed.ResetCounts();
		CORE_CHECK(timed.At(0).runs == 0 && timed.At(0).usAvg == 0.0f && timed.At(1).runs == 0 && timed.At(1).usMax == 0.0f && timed.Metered() &&
				   spinner.IsArmed());

		// a slot is sealed at its moment whether or not a thunk was written: a handler added later is refused, and only Written
		// says a thunk runs the lists (the audit's C2)
		static IntSlot unwritten("unwritten");
		CORE_CHECK(!unwritten.Written() && !unwritten.Sealed());
		unwritten.Seal(Fault);
		CORE_CHECK(unwritten.Sealed() && !unwritten.Written() && !unwritten.AddAfter("late", [](int&, int) {}) && !unwritten.Wanted());
		unwritten.MarkWritten();
		CORE_CHECK(unwritten.Written() && unwritten.Handlers() == 0);

		// an instead handler runs after the before handlers with the arguments by reference: a change reaches the game's call and
		// the after handlers (the audit's C4: a ward's art swapped before the game loads it)
		static IntSlot swapped("swap");
		const auto     swapper = swapped.AddInstead("wards", [](int&, int& a_value) {
			gRan += "swapped ";
			a_value = 7;
			return false;
		});
		const auto     swapSeen = swapped.AddAfter("illuminated", [](int& a_result, int a_value) { gRan += "after(" + std::to_string(a_value) + "," + std::to_string(a_result) + ") "; });
		CORE_CHECK(swapper && swapSeen && swapped.Wanted() && swapped.Handlers() == 2);
		swapped.Seal(Fault);
		swapper.Arm();
		swapSeen.Arm();
		gRan.clear();
		CORE_CHECK(swapped.Call(Game, 2) == 14 && gRan == "swapped game after(7,14) ");
		CORE_CHECK(swapped.At(0).instead && !swapped.At(0).before && swapped.At(0).armed && swapped.At(0).runs == 1 && !swapped.At(1).instead && !swapped.At(1).before);

		// one that answers skips the game's own call: the after handlers see its answer; disarmed, the game's call is back
		static IntSlot skip("skip");
		const auto     skipper = skip.AddInstead("wards", [](int& a_result, int&) {
			a_result = -5;
			return true;
		});
		const auto     second = skip.AddInstead("late", [](int&, int&) {
			gRan += "second ";
			return true;
		});
		const auto     bump = skip.AddAfter("p", [](int& a_result, int) { ++a_result; });
		skip.Seal(Fault);
		skipper.Arm();
		second.Arm();
		bump.Arm();
		gRan.clear();
		CORE_CHECK(skip.Call(Game, 2) == -4 && gRan.empty() && skip.At(1).runs == 0);
		skipper.Disarm();
		second.Disarm();
		gRan.clear();
		CORE_CHECK(skip.Call(Game, 2) == 5 && gRan == "game ");
		// refused once sealed, as every other kind
		CORE_CHECK(!skip.AddInstead("late", [](int&, int&) { return true; }) && skip.Handlers() == 3);

		// one that throws answers nothing: what it changed before the throw stays changed, the game's call runs, it is told once
		static IntSlot thrown("thrown");
		const auto     breaker = thrown.AddInstead("breaker", [](int&, int& a_value) -> bool {
			a_value = 3;
			throw std::runtime_error("x");
		});
		thrown.Seal(Fault);
		breaker.Arm();
		gFaults.clear();
		gRan.clear();
		CORE_CHECK(thrown.Call(Game, 2) == 6 && gRan == "game " && thrown.Call(Game, 2) == 6 && gFaults == "breaker@thrown ");

		// a function that returns nothing: an answer skips it as well
		static Slot<void, int*> plainSkip("plain skip");
		const auto              stand = plainSkip.AddInstead("p", [](int*&) { return true; });
		plainSkip.Seal(Fault);
		stand.Arm();
		int untouched = 0;
		gRan.clear();
		plainSkip.Call(GameVoid, &untouched);
		CORE_CHECK(untouched == 0 && gRan.empty());
	}

#undef CORE_CHECK
}
