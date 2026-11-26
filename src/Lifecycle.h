// Illuminated - Core: the moments of a game session, handed to each part that is awake
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// Before the core, each plugin had its own SKSE listener and did its own work at each message. Here the kernel has the one
// listener and names the moments; a part is a row of plain function pointers the kernel calls, in the parts' fixed order, and
// only for a part whose mod's data is installed and switched on. A part that is asleep is never called, at any moment. Each
// part's work is guarded alone: an error in one part stops that part's work at that moment and nothing else.
// Nothing of the game in this file (tests\testcore.cpp).

#pragma once

#include "ManifestText.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Lifecycle
{
	enum class Moment : std::uint8_t
	{
		// every plugin's load call is done: hooks that have to sit outside another author's load-time hook go in here
		kPostLoad,
		// the game's records are loaded and it is the core's turn among the plugins: records made, data read, menus
		kDataLoad,
		// one main-thread task later, when every plugin's own data-load work is done: what has to see their work
		kAfterAllPlugins,
		// a save is about to load, or a new game starts: everything kept for the session before is let go
		kPreLoadGame,
		kNewGame,
		// a save has loaded; the flag tells whether it loaded well
		kPostLoadGame,
		// the engine took the lights out of the scene (a cell change): each part hangs its lights again
		kLightsDropped,
		kTotal
	};

	inline constexpr std::size_t kMoments = static_cast<std::size_t>(Moment::kTotal);

	[[nodiscard]] constexpr std::string_view NameOf(Moment a_moment) noexcept
	{
		constexpr std::string_view kNames[kMoments]{ "post-load", "data load", "after every plugin's data load", "before a save loads",
			"a new game", "after a save loaded", "the engine dropped the lights" };
		const auto                 at = static_cast<std::size_t>(a_moment);
		return at < kMoments ? kNames[at] : std::string_view{ "no moment" };
	}

	// one part of the core: what the manifest reader knows of it, and its work per moment. a_ok is the save's own verdict at
	// kPostLoadGame and true at every other moment
	struct Part
	{
		ManifestText::Known known{};
		void (*at)(Moment a_moment, bool a_ok){ nullptr };
	};

	// called when a part's work at a moment let an error out: the part's id and the moment
	using Fault = void (*)(std::string_view a_part, Moment a_moment) noexcept;

	// a moment handed to every awake part, in the parts' order. a_awake answers for a part's id (the survey's verdict). Returns
	// how many parts did their work without an error
	template <class Awake>
	std::size_t Run(std::span<const Part> a_parts, Moment a_moment, bool a_ok, const Awake& a_awake, Fault a_fault) noexcept
	{
		std::size_t done = 0;
		for (const auto& part : a_parts) {
			if (!part.at) {
				continue;
			}
			// no exception may leave a callback into the game: that part's work stops there, the next part goes on
			try {
				if (!a_awake(part.known.id)) {
					continue;
				}
				part.at(a_moment, a_ok);
				++done;
			} catch (...) {
				if (a_fault) {
					a_fault(part.known.id, a_moment);
				}
			}
		}
		return done;
	}
}
