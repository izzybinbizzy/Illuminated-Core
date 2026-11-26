// Illuminated - Core - SKSE plugin
// Copyright (C) 2026 izzydoingit
//
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU
// General Public License as published by the Free Software Foundation, either version 3 of the License,
// or (at your option) any later version. See the LICENSE file.
//
// One plugin for every Illuminated mod: each mod's own download carries only its data and a small manifest
// (SKSE\Plugins\IlluminatedCore\Modules\<part>.json), and the part of this plugin a manifest names runs only then.
//
// THE FILES, AND WHAT EACH ONE IS FOR
//   main.cpp         this file - the start, the wrong-game guard, and the order of the work at each SKSE message
//   Kernel.h         what the files share; PCH.h what every file includes
//   Kernel.cpp       what is installed: the manifests and the switches file read at post-load, a switch written back, and each
//                    moment of the session handed to the parts that are awake
//   ManifestText.h   game-free: a data mod's manifest as text, and whether this core runs it
//   SwitchesText.h   game-free: Settings.ini, one switch per part
//   SurveyText.h     game-free: every installed mod's state and every line a player is told, from those two
//   Lifecycle.h      game-free: the moments of a session and a part's work at each
//   HookDispatch.h   game-free: one hook on a game function shared by every part, a handler's own armed bit
//   Hooks.h, HookTable.cpp  the hook table's game side: a slot's one thunk, written only when a part asked for it
//   DimmerKeys.h     game-free: a light's pulse baked into keys the game's own dimmer plays     DimmerKit.h  the game's dimmer
//   Composer.h       game-free: one light's brightness as one product, written only when it changes
//   Probe.cpp        the test part, in the testing build only: DevBench tries the engine routes on the game's own lights
//   Menu.cpp         the SKSE Menu Framework section     DevBench.cpp  DevBench's inspect and control (optional)
//   SKSEMenuFramework.h, DevBenchAPI.*  those mods' own files; the other shared files (master: Tools\Shared Plugin Code)
//                    are named in THIRD-PARTY NOTICES and the README
//   tests\testcore.cpp  the off-game tests of the game-free files and of the shared check sets

#include "Kernel.h"

#include "Hooks.h"
#include "RuntimeGuard.h"
#include "Translation.h"

namespace
{
	void Handle(SKSE::MessagingInterface::Message* a_message)
	{
		using Lifecycle::Moment;
		switch (a_message->type) {
		case SKSE::MessagingInterface::kPostLoad:
			// each step guarded alone: a throw in one still leaves the others done
			// first of all: which parts are awake decides every hook written below
			Core::Guarded("the survey at post-load", []() { Core::Look(); });
			Core::Guarded("DevBench", []() { Core::OfferToDevBench(); });
#ifdef CORE_PROBE
			Core::Guarded("the test part", []() {
				Core::ProbeHooks();
				Core::OfferProbe();
			});
#endif
			// each awake part adds its handlers to the slots it needs, then the rows that have a handler are written
			Core::Tell(Moment::kPostLoad, true);
			// every other plugin's load call is done: a row written now lands outside a hook another author wrote at load
			Core::Guarded("the hooks of post-load", []() { Core::Hooks::WriteAtPostLoad(); });
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			Core::Guarded("the box at data load", []() { Core::TellPlayer(); });
			Core::Guarded("the translation file", []() { SKSE::log::info("{}", Translation::Load("IlluminatedCore")); });
			Core::Guarded("the menu section", []() { Core::RegisterMenu(); });
			Core::Tell(Moment::kDataLoad, true);
			// what has to see every other plugin's data-load work runs one main-thread task later; no task with no part awake
			if (Core::AnyAwake() && !Core::Later("after every plugin's data load", []() { Core::Tell(Moment::kAfterAllPlugins, true); })) {
				SKSE::log::warn("the moment after every plugin's data load could not be queued: the parts' work there did not run");
			}
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
#ifdef CORE_PROBE
			Core::Guarded("the test part's trials", []() { Core::ProbeForget(); });
#endif
			Core::Tell(Moment::kPreLoadGame, true);
			break;
		case SKSE::MessagingInterface::kNewGame:
#ifdef CORE_PROBE
			Core::Guarded("the test part's trials", []() { Core::ProbeForget(); });
#endif
			Core::Tell(Moment::kNewGame, true);
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			// SKSE hands the save's own verdict in the message's data: nothing there is a load that failed
			Core::Tell(Moment::kPostLoadGame, a_message->data != nullptr);
			break;
		default:
			break;
		}
	}

	// no exception may leave a callback into the game: the work stops there and the log says where
	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (a_message) {
			Core::Guarded("SKSE message", [a_message]() { Handle(a_message); });
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	// its own log file: the name Illuminated.log stays the Illuminated mod's part's
	SKSE::InitInfo info{};
	info.logName = "IlluminatedCore";
	SKSE::Init(a_skse, info);
	// the wrong game: loaded but idle (a false here adds SKSE's own "exit game?" box over the guard's)
	if (!RuntimeGuard::RightGame("Illuminated - Core", "Illuminated - Core", "Illuminated - Core - VR")) {
		return true;
	}
	// the start line is the only work here that can throw (low memory): it never leaves the plugin
	try {
		SKSE::log::info("Illuminated - Core {} - one plugin for every Illuminated mod; a mod's part runs when its data is installed",
			ManifestText::Shown(Core::VersionNow()));
	} catch (...) {
	}
	if (const auto* messaging = SKSE::GetMessagingInterface()) {
		messaging->RegisterListener(OnMessage);
	}
	return true;
}
