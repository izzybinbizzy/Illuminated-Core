// Illuminated - Core: one hook on a game function, shared by every part that needs it
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// Before the core, each of our plugins wrote its own hook on a game slot and the order between them was whichever DLL wrote last.
// Here a slot has ONE thunk and a list of handlers in the order the parts wake. Each handler has its own bit in the slot's armed
// word: with no bit set the thunk is the game's own call and one read of that word, nothing else. A handler arms itself on an
// event and disarms itself when its work ends. Handlers are added before the hook is written and never after, so the lists are
// read without a lock. One slot serves the same function on several game classes (a character's and the player's, each
// projectile class): the handlers are one list, and each class's thunk hands in its own original function.
// A slot can time its handlers (Meter): the clock is read only round a handler that runs, so a slot nobody armed never reaches it.
// Nothing of the game in here (tests\testcore.cpp): the game's function is a plain function pointer.

#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace HookDispatch
{
	// handlers of one kind (before or after the game's own call) a slot can hold: half the bits of its armed word
	inline constexpr std::size_t kMost = 16;

	// a handler's own switch. Any thread; a switch that belongs to no handler (the slot was full or already written) does nothing
	class Armed
	{
	public:
		Armed() = default;
		Armed(std::atomic<std::uint32_t>* a_word, std::uint32_t a_bit) noexcept :
			word(a_word), bit(a_bit) {}

		void Arm() const noexcept
		{
			if (word) {
				word->fetch_or(bit, std::memory_order_relaxed);
			}
		}

		void Disarm() const noexcept
		{
			if (word) {
				word->fetch_and(~bit, std::memory_order_relaxed);
			}
		}

		[[nodiscard]] bool IsArmed() const noexcept { return word && (word->load(std::memory_order_relaxed) & bit) != 0; }
		// false when the handler was refused: its part then runs without it and says so
		[[nodiscard]] explicit operator bool() const noexcept { return word != nullptr; }

	private:
		std::atomic<std::uint32_t>* word{ nullptr };
		std::uint32_t               bit{ 0 };
	};

	// called once per handler, the first time it lets an exception out: the part's name and the slot's
	using Fault = void (*)(const char* a_part, const char* a_slot) noexcept;

	namespace Detail
	{
		// a handler after the game's call sees what the call returned, and may change it
		template <class R, class... A>
		struct AfterOf
		{
			using type = void (*)(R&, A...);
		};
		template <class... A>
		struct AfterOf<void, A...>
		{
			using type = void (*)(A...);
		};

		template <class F>
		struct Handler
		{
			F                          function{ nullptr };
			const char*                part{ "" };
			std::atomic<std::uint32_t> runs{ 0 };
			std::atomic<float>         usAvg{ 0.0f };
			std::atomic<float>         usMax{ 0.0f };
			std::atomic<bool>          told{ false };

			// one timed run, in microseconds: the average follows about the last 20, the worst is kept until the counts are reset
			void Note(float a_us) noexcept
			{
				const float avg = usAvg.load(std::memory_order_relaxed);
				usAvg.store(avg <= 0.0f ? a_us : avg + (a_us - avg) * 0.05f, std::memory_order_relaxed);
				// two threads running one handler at once never lower the worst seen; the average may lose a sample then, as the
				// shared frame meter's does
				float worst = usMax.load(std::memory_order_relaxed);
				while (a_us > worst && !usMax.compare_exchange_weak(worst, a_us, std::memory_order_relaxed)) {
				}
			}

			void Reset() noexcept
			{
				runs.store(0, std::memory_order_relaxed);
				usAvg.store(0.0f, std::memory_order_relaxed);
				usMax.store(0.0f, std::memory_order_relaxed);
			}
		};
	}

	template <class R, class... A>
	class Slot
	{
	public:
		using Original = R (*)(A...);
		using Before = void (*)(A...);
		using After = typename Detail::AfterOf<R, A...>::type;

		constexpr explicit Slot(const char* a_name) noexcept :
			name(a_name) {}
		Slot(const Slot&) = delete;
		Slot& operator=(const Slot&) = delete;

		// a handler that runs before the game's own call. Refused (an empty switch) once the hook is written or the list is full
		[[nodiscard]] Armed AddBefore(const char* a_part, Before a_handler) noexcept
		{
			if (Sealed() || !a_handler || beforeCount >= kMost) {
				return {};
			}
			before[beforeCount].function = a_handler;
			before[beforeCount].part = a_part;
			return { &armed, std::uint32_t{ 1 } << beforeCount++ };
		}

		// a handler that runs after the game's own call
		[[nodiscard]] Armed AddAfter(const char* a_part, After a_handler) noexcept
		{
			if (Sealed() || !a_handler || afterCount >= kMost) {
				return {};
			}
			after[afterCount].function = a_handler;
			after[afterCount].part = a_part;
			return { &armed, std::uint32_t{ 1 } << (kMost + afterCount++) };
		}

		// a hook is written only for a slot some awake part asked for
		[[nodiscard]] bool Wanted() const noexcept { return beforeCount + afterCount != 0; }
		// any thread: once it reads true, the handler lists are complete and never change again (Seal publishes them)
		[[nodiscard]] bool        Sealed() const noexcept { return sealed.load(std::memory_order_acquire); }
		[[nodiscard]] const char* Name() const noexcept { return name; }

		// called before the first thunk of this slot is written: from here on no handler is added
		void Seal(Fault a_fault) noexcept
		{
			fault = a_fault;
			sealed.store(true, std::memory_order_release);
		}

		// a thunk's whole body; a_original is the game's own function at the place that thunk was written
		R Call(Original a_original, A... a_args)
		{
			if (beforeCount != 0) {
				if (const auto word = armed.load(std::memory_order_relaxed) & kBeforeBits) {
					RunBefore(word, a_args...);
				}
			}
			if constexpr (std::is_void_v<R>) {
				a_original(a_args...);
				if (const auto word = armed.load(std::memory_order_relaxed) >> kMost) {
					RunAfter(word, a_args...);
				}
			} else {
				R result = a_original(a_args...);
				if (const auto word = armed.load(std::memory_order_relaxed) >> kMost) {
					RunAfter(word, result, a_args...);
				}
				return result;
			}
		}

		// the meter: while on, every handler that runs is timed. Any thread; off costs a running handler one read
		void               Meter(bool a_on) noexcept { metered.store(a_on, std::memory_order_relaxed); }
		[[nodiscard]] bool Metered() const noexcept { return metered.load(std::memory_order_relaxed); }

		// every handler's count and times back to nothing (a test's start, a load). Any thread
		void ResetCounts() noexcept
		{
			// the lists are complete only once the slot is sealed; before that no thunk ran and nothing was counted
			if (!Sealed()) {
				return;
			}
			for (std::size_t i = 0; i < beforeCount; ++i) {
				before[i].Reset();
			}
			for (std::size_t i = 0; i < afterCount; ++i) {
				after[i].Reset();
			}
		}

		// for DevBench: each handler's part, how often it ran (a slot nobody armed reads 0 everywhere) and, while the meter was
		// on, what a run cost in microseconds
		struct Seen
		{
			const char*   part{ "" };
			bool          before{ false };
			bool          armed{ false };
			std::uint32_t runs{ 0 };
			float         usAvg{ 0.0f };
			float         usMax{ 0.0f };
		};

		[[nodiscard]] std::size_t Handlers() const noexcept { return static_cast<std::size_t>(beforeCount) + afterCount; }

		[[nodiscard]] Seen At(std::size_t a_index) const noexcept
		{
			const auto word = armed.load(std::memory_order_relaxed);
			if (a_index < beforeCount) {
				const auto& handler = before[a_index];
				return { handler.part, true, (word >> a_index & 1) != 0, handler.runs.load(std::memory_order_relaxed),
					handler.usAvg.load(std::memory_order_relaxed), handler.usMax.load(std::memory_order_relaxed) };
			}
			a_index -= beforeCount;
			if (a_index < afterCount) {
				const auto& handler = after[a_index];
				return { handler.part, false, (word >> (kMost + a_index) & 1) != 0, handler.runs.load(std::memory_order_relaxed),
					handler.usAvg.load(std::memory_order_relaxed), handler.usMax.load(std::memory_order_relaxed) };
			}
			return {};
		}

	private:
		static constexpr std::uint32_t kBeforeBits = (std::uint32_t{ 1 } << kMost) - 1;

		template <class F, class... Args>
		void Run(std::array<Detail::Handler<F>, kMost>& a_list, std::uint32_t a_word, Args&&... a_args)
		{
			using Clock = std::chrono::steady_clock;
			// read once a call, and only with a handler to run
			const bool timed = metered.load(std::memory_order_relaxed);
			// the armed handlers only, lowest bit first: the order the parts wake in
			while (a_word != 0) {
				auto& handler = a_list[static_cast<std::size_t>(std::countr_zero(a_word))];
				a_word &= a_word - 1;
				const auto start = timed ? Clock::now() : Clock::time_point{};
				// no exception may reach the game: the handler's work stops there, the next handler and the game go on
				try {
					handler.function(a_args...);
				} catch (...) {
					if (!handler.told.exchange(true) && fault) {
						fault(handler.part, name);
					}
				}
				handler.runs.fetch_add(1, std::memory_order_relaxed);
				if (timed) {
					handler.Note(std::chrono::duration<float, std::micro>(Clock::now() - start).count());
				}
			}
		}

		void RunBefore(std::uint32_t a_word, A... a_args) { Run(before, a_word, a_args...); }

		template <class... Args>
		void RunAfter(std::uint32_t a_word, Args&&... a_args)
		{
			Run(after, a_word, a_args...);
		}

		const char*                                name;
		Fault                                      fault{ nullptr };
		std::atomic<std::uint32_t>                 armed{ 0 };
		std::atomic<bool>                          metered{ false };
		std::array<Detail::Handler<Before>, kMost> before{};
		std::array<Detail::Handler<After>, kMost>  after{};
		std::uint8_t                               beforeCount{ 0 };
		std::uint8_t                               afterCount{ 0 };
		std::atomic<bool>                          sealed{ false };
	};
}
