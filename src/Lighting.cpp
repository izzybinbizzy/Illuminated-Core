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
		// the whole answer in one word, written once and read once, so a reader on another thread (DevBench's) never sees half of
		// it: bits 0-1 the lighting, bit 2 Light Limit Fix reads every light's first words (CommunityShaders.dll loaded), bit 3 a
		// shader of it draws inverse square, bit 4 read
		constexpr std::uint8_t    kLightLimitFix = 1 << 2;
		constexpr std::uint8_t    kInverseSquare = 1 << 3;
		constexpr std::uint8_t    kRead = 1 << 4;
		std::atomic<std::uint8_t> gState{ static_cast<std::uint8_t>(GameLighting::Lighting::kVanilla) };
	}

	LightingState LightingNow()
	{
		const auto state = gState.load(std::memory_order_acquire);
		const auto found = static_cast<std::uint8_t>(state & 3);
		return { found, GameLighting::Name(static_cast<GameLighting::Lighting>(found)), (state & kRead) != 0, (state & kLightLimitFix) != 0,
			(state & kInverseSquare) != 0 };
	}

	void ReadLighting()
	{
		const auto found = GameLighting::Read();
		gState.store(static_cast<std::uint8_t>(static_cast<std::uint8_t>(found.lighting) | (found.communityShaders ? kLightLimitFix : 0) |
											   (found.inverseSquare ? kInverseSquare : 0) | kRead),
			std::memory_order_release);
		const auto lines = GameLighting::Describe(found);
		SKSE::log::info("{}", lines.info);
		for (const auto& warning : lines.warnings) {
			SKSE::log::warn("{}", warning);
		}
	}

	std::uint8_t LightingFound() { return LightingNow().found; }

	const char* LightingName() { return LightingNow().name; }

	bool LightingRead() { return LightingNow().read; }

	bool InverseSquare() { return LightingNow().inverseSquare; }

	bool LightLimitFix() { return LightingNow().lightLimitFix; }
}
