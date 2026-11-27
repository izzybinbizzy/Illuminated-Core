// Illuminated Core Framework - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// The hook table's game side: a slot's one thunk written into a class's vtable or over a call site, and only when some awake part
// asked for that slot (HookDispatch.h holds the handlers and the armed word; nothing of the game is in it). Each place a slot is
// written keeps its own copy of the game's function there, so one handler list serves a character's and the player's vtable alike.

#pragma once

#include "HookDispatch.h"
#include "Kernel.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Core::Hooks
{
	// one place a slot is written. Place tells two places of one slot apart (the vtables of two classes, two call sites)
	template <auto* Slot, std::size_t Place>
	struct Thunk;

	template <class R, class... A, HookDispatch::Slot<R, A...>* Slot, std::size_t Place>
	struct Thunk<Slot, Place>
	{
		using Original = R (*)(A...);

		static inline Original original{ nullptr };

		static R Call(A... a_args) { return Slot->Call(original, a_args...); }
	};

	// ------------------------------------------------------------------ HookTable.cpp: the rows
	// the slots a part may add a handler to, each before the moment its row is written
	inline HookDispatch::Slot<void, RE::PlayerCharacter*, float> gPlayerUpdate{ "the player's update" };

	// a handler let an exception out (one log line per handler), and a place was written (logged, counted for DevBench)
	void Fault(const char* a_part, const char* a_slot) noexcept;
	void Wrote(const char* a_slot, const char* a_where) noexcept;
	// a handler added after its slot's moment: refused, and one log line names the part and the slot
	void Late(const char* a_part, const char* a_slot) noexcept;
	// the rows of the post-load moment, each written only when its slot has a handler. Called at SKSE's post-post-load, once
	// every plugin's post-load handlers are in. Main thread
	void WriteAtPostLoad();
	// the rows of the data-load moment (hooks that must land outside one another author writes at post-load, or that need the
	// game's records): each slot of it sealed there, written only when it has a handler. Main thread, after the parts' data-load work
	void WriteAtDataLoad();

	// every call-site row of the table (SKSE write_call<5>, 14 bytes of trampoline each), counted whether or not its part wakes:
	// the trampoline is asked for at Init, before the survey. A call-site row added to HookTable.cpp adds one here, and a part
	// that carries FloorLightFix adds FloorLightFix::kTrampolineBytes
	inline constexpr std::size_t kCallSiteRows = 0;
	inline constexpr std::size_t kTrampolineBytes = kCallSiteRows * 14;
	// how many places hold a thunk of the core; any thread. 0 while no part asked for a slot
	[[nodiscard]] std::size_t WrittenCount() noexcept;

	// for DevBench: the table as it stands - each slot, its handlers in the order they run, how often each ran and, while the
	// meter was on, what a run cost in microseconds. Any thread
	struct HandlerSeen
	{
		const char*   part{ "" };
		bool          before{ false };
		bool          instead{ false };  // answers in the game's place or changes its arguments (HookDispatch AddInstead)
		bool          armed{ false };
		std::uint32_t runs{ 0 };
		float         usAvg{ 0.0f };
		float         usMax{ 0.0f };
	};
	struct Row
	{
		const char*              slot{ "" };
		const char*              moment{ "" };  // the moment its row is written at: post-load or data load
		bool                     written{ false };
		bool                     metered{ false };
		std::vector<HandlerSeen> handlers;
	};
	[[nodiscard]] std::vector<Row> Rows();
	// every slot's meter on or off, and every handler's count and times back to nothing. Any thread
	void Meter(bool a_on) noexcept;
	void ResetCounts() noexcept;

	// a slot's thunk into one class's vtable. False, with nothing written, when no awake part asked for the slot or the place is
	// written already. The slot is sealed either way: its moment has come. Main thread, at the slot's install moment
	template <auto* Slot, std::size_t Place>
	bool WriteVTable(REL::VariantID a_table, std::size_t a_index, const char* a_class)
	{
		using Mine = Thunk<Slot, Place>;
		if (!Slot->Sealed()) {
			Slot->Seal(Fault, Late);
		}
		if (!Slot->Wanted() || Mine::original) {
			return false;
		}
		REL::Relocation<std::uintptr_t> table{ a_table };
		// the game's function is read out before the thunk goes in: a call arriving the moment the entry is written already has it
		const auto entry = reinterpret_cast<std::uintptr_t*>(table.address() + a_index * sizeof(std::uintptr_t));
		Mine::original = reinterpret_cast<typename Mine::Original>(*entry);
		table.write_vfunc(a_index, Mine::Call);
		Slot->MarkWritten();
		Wrote(Slot->Name(), a_class);
		return true;
	}
}
