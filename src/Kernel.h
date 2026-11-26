// Illuminated - Core - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// What the kernel's files share. Each block names the file that defines it.

#pragma once

#include "Lifecycle.h"
#include "SurveyText.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace Core
{
	// Runs a_work and lets no exception out: inside a game callback, a menu draw or a task one would end the game. The first
	// failure at each place is logged, later ones there are skipped quietly.
	template <class F>
	inline void Guarded(const char* a_where, F&& a_work) noexcept
	{
		static std::atomic<bool> told{ false };
		try {
			std::forward<F>(a_work)();
		} catch (const std::exception& e) {
			if (!told.exchange(true)) {
				// the log line itself may throw (low memory): never out of a noexcept function
				try {
					SKSE::log::error("{}: stopped by an error ({}); later ones here are skipped quietly", a_where, e.what());
				} catch (...) {
				}
			}
		} catch (...) {
			if (!told.exchange(true)) {
				try {
					SKSE::log::error("{}: stopped by an unknown error; later ones here are skipped quietly", a_where);
				} catch (...) {
				}
			}
		}
	}

	// a_job on the game's main thread at the next chance (SKSE's task interface); false when it could not be queued
	template <class F>
	[[nodiscard]] bool Later(const char* a_where, F&& a_job) noexcept
	{
		try {
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([a_where, job = std::forward<F>(a_job)]() { Guarded(a_where, job); });
				return true;
			}
		} catch (...) {
		}
		return false;
	}

	// ------------------------------------------------------------------ Kernel.cpp: what is installed, and each part's switch
	// the version the DLL declares (xmake.lua set_version), never a typed number
	[[nodiscard]] ManifestText::Version VersionNow();
	// the parts built into this version, in the order they wake
	[[nodiscard]] std::span<const ManifestText::Known> Parts();
	// post-load, main thread, once, before any hook of a part is written: the manifests and the switches file read, each line
	// logged. Files only - nothing of the game's data is needed, and the hooks of that moment must know which parts are awake
	void Look();
	// data load, main thread, once: the one box of this start, when Look found something to say
	void TellPlayer();
	// whether a part runs this session; any thread, false before Look
	[[nodiscard]] bool IsAwake(std::string_view a_part);
	// whether any part runs this session; any thread
	[[nodiscard]] bool AnyAwake();
	// a moment of the session handed to every awake part, in the parts' order (Lifecycle.h); a_ok is the save's own verdict after
	// a load and true otherwise. Main thread. With no part awake it does nothing
	void Tell(Lifecycle::Moment a_moment, bool a_ok);
	// what Look found; any thread, never null (empty before post-load)
	[[nodiscard]] std::shared_ptr<const SurveyText::Survey> SurveyNow();
	// every part with its switch for the next game start; any thread
	[[nodiscard]] std::vector<std::pair<std::string_view, bool>> SwitchesNow();
	// a part's switch for the next game start: kept at once, written to Settings.ini by one main-thread task. Any thread; false for a
	// part this core does not have
	bool SetSwitch(std::string_view a_part, bool a_on);

	// ------------------------------------------------------------------ Lighting.cpp: the lighting the game draws with, read once
	// data load, main thread, once, before anything reads it: GameLighting.h's one look, logged
	void ReadLighting();
	// GameLighting's number (0 Community Shaders, 1 ENB, 2 Vanilla); Vanilla before ReadLighting. Any thread
	[[nodiscard]] std::uint8_t LightingFound();
	[[nodiscard]] const char*  LightingName();
	[[nodiscard]] bool         LightingRead();
	// CommunityShaders.dll is loaded, so Light Limit Fix reads every light's first words; and a shader of it draws inverse square
	[[nodiscard]] bool LightLimitFix();
	[[nodiscard]] bool InverseSquare();

	// ------------------------------------------------------------------ Menu.cpp / DevBench.cpp
	void RegisterMenu();
	void OfferToDevBench();

#ifdef CORE_PROBE
	// ------------------------------------------------------------------ Probe.cpp: the test part, in the testing build only
	void ProbeHooks();
	void OfferProbe();
	// before a save loads and at a new game, main thread: every trial let go
	void ProbeForget();
#endif
}
