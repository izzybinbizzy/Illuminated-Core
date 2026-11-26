// Illuminated Core Framework - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// Which lighting the game draws with: the shared GameLighting.h finds it once, at data load (Community Shaders, ENB or Vanilla,
// never picked), and this file keeps the answer for the whole core - every part, the object-light library and the plugin
// interface read the same one. On Community Shaders a light's reach comes from its fade and cutoff where a shader draws inverse
// square; elsewhere, and on a Community Shaders with no such shader, every light of ours is drawn plain.

#include "Kernel.h"

#include "GameLighting.h"
#include "ObjectRows.h"

#include <atomic>

namespace Core
{
	// the kernel's lighting number is GameLighting's, and the object-light library's is the same: the one read crosses unchanged
	static_assert(static_cast<int>(GameLighting::Lighting::kShaders) == 0 && static_cast<int>(GameLighting::Lighting::kEnb) == 1 &&
				  static_cast<int>(GameLighting::Lighting::kVanilla) == 2);
	static_assert(static_cast<int>(ObjectRows::Lighting::kCommunityShaders) == 0 && static_cast<int>(ObjectRows::Lighting::kENB) == 1 &&
				  static_cast<int>(ObjectRows::Lighting::kVanilla) == 2);

	namespace
	{
		std::atomic<std::uint8_t> gFound{ static_cast<std::uint8_t>(GameLighting::Lighting::kVanilla) };
		// CommunityShaders.dll loaded: Light Limit Fix reads every light's first words
		std::atomic<bool> gLightLimitFix{ false };
		// ... and a shader of it draws inverse square
		std::atomic<bool> gInverseSquare{ false };
		std::atomic<bool> gRead{ false };
	}

	void ReadLighting()
	{
		const auto found = GameLighting::Read();
		gLightLimitFix = found.communityShaders;
		gInverseSquare = found.inverseSquare;
		gFound = static_cast<std::uint8_t>(found.lighting);
		gRead = true;
		const auto lines = GameLighting::Describe(found);
		SKSE::log::info("{}", lines.info);
		for (const auto& warning : lines.warnings) {
			SKSE::log::warn("{}", warning);
		}
	}

	std::uint8_t LightingFound() { return gFound.load(); }

	const char* LightingName() { return GameLighting::Name(static_cast<GameLighting::Lighting>(gFound.load())); }

	bool LightingRead() { return gRead.load(); }

	bool InverseSquare() { return gInverseSquare.load(); }

	bool LightLimitFix() { return gLightLimitFix.load(); }
}
