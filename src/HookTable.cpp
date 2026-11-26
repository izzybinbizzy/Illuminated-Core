// Illuminated - Core - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// The hook table: every game function the core hooks, in one place - the slot per game, the moment it is written, and what else
// is known to sit on it. A row is written only when an awake part added a handler to its slot (Hooks.h, HookDispatch.h), so a
// core with no part awake writes nothing: DevBench's read-out then lists every row as not written.
// DRAFT: not compiled yet - the first row comes with the kernel's test part.

#include "Hooks.h"

#include <atomic>
#include <cstddef>
#include <utility>
#include <vector>

namespace Core::Hooks
{
	namespace
	{
		// how many places were written; counted on the main thread, read by DevBench on its own
		std::atomic<std::size_t> gWritten{ 0 };

		// every slot of the table, for what is done to all of them alike: a new slot is named here once
		template <class F>
		void ForEachSlot(F&& a_do)
		{
			a_do(gPlayerUpdate);
		}
	}

	void Fault(const char* a_part, const char* a_slot) noexcept
	{
		// the log line itself may throw (low memory): never out of a hook
		try {
			SKSE::log::error("{}: its handler on {} stopped by an error; its later errors there are not logged again", a_part, a_slot);
		} catch (...) {
		}
	}

	void Wrote(const char* a_slot, const char* a_where) noexcept
	{
		gWritten.fetch_add(1, std::memory_order_relaxed);
		// the thunk is in already: a log line that fails must not keep the rows after this one from being written
		try {
			SKSE::log::info("hook: {} written on {}", a_slot, a_where);
		} catch (...) {
		}
	}

	// post-load: after every other plugin's load call, so a row lands outside a hook another author wrote at load
	void WriteAtPostLoad()
	{
		// once a frame in the world. Slot 0xAD on SE and AE, 0xAF on VR (0xAE there is another function)
		static_cast<void>(WriteVTable<&gPlayerUpdate, 0>(RE::VTABLE_PlayerCharacter[0], REL::Relocate<std::size_t>(0xAD, 0xAD, 0xAF), "PlayerCharacter"));
	}

	std::size_t WrittenCount() noexcept
	{
		return gWritten.load(std::memory_order_relaxed);
	}

	std::vector<Row> Rows()
	{
		std::vector<Row> rows;
		ForEachSlot([&rows](const auto& a_slot) {
			Row row{ a_slot.Name(), a_slot.Sealed(), a_slot.Metered(), {} };
			// read on DevBench's thread: the handler lists only once the slot is sealed, when they are complete and never change
			if (!row.written) {
				rows.push_back(std::move(row));
				return;
			}
			row.handlers.reserve(a_slot.Handlers());
			for (std::size_t i = 0; i < a_slot.Handlers(); ++i) {
				const auto seen = a_slot.At(i);
				row.handlers.push_back({ seen.part, seen.before, seen.armed, seen.runs, seen.usAvg, seen.usMax });
			}
			rows.push_back(std::move(row));
		});
		return rows;
	}

	void Meter(bool a_on) noexcept
	{
		ForEachSlot([a_on](auto& a_slot) { a_slot.Meter(a_on); });
	}

	void ResetCounts() noexcept
	{
		ForEachSlot([](auto& a_slot) { a_slot.ResetCounts(); });
	}
}
