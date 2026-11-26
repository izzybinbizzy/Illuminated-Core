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
//   Kernel.cpp       what is installed: the manifests and the switches file read at data load, a switch written back
//   ManifestText.h   game-free: a data mod's manifest as text, and whether this core runs it
//   SwitchesText.h   game-free: Settings.ini, one switch per part
//   SurveyText.h     game-free: every installed mod's state and every line a player is told, from those two
//   Menu.cpp         the SKSE Menu Framework section     DevBench.cpp  DevBench's inspect and control (optional)
//   SKSEMenuFramework.h, DevBenchAPI.*  those mods' own files; the other shared files (master: Tools\Shared Plugin Code)
//                    are named in THIRD-PARTY NOTICES and the README
//   tests\testcore.cpp  the off-game tests of the game-free files and of the shared check sets

#include "Kernel.h"

#include "RuntimeGuard.h"
#include "Translation.h"

namespace
{
	void Handle(SKSE::MessagingInterface::Message* a_message)
	{
		switch (a_message->type) {
		case SKSE::MessagingInterface::kPostLoad:
			Core::OfferToDevBench();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			// each step guarded alone: a throw in one still leaves the others done (the menu shows what the survey found)
			Core::Guarded("the survey at data load", []() { Core::LookAtData(); });
			Core::Guarded("the translation file", []() { SKSE::log::info("{}", Translation::Load("IlluminatedCore")); });
			Core::Guarded("the menu section", []() { Core::RegisterMenu(); });
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
