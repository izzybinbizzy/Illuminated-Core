// Illuminated - Core - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// The game's own light dimmer, driven from one place: keys built once (DimmerKeys.h bakes a pulse into them), handed to the
// engine's NiLightDimmerController on a light, and played by the engine from then on with no code of ours per frame. Before the
// core this was written twice by hand (the creature lights, the burning banners). The controller and its key makers are not in
// our pin of CommonLib, so they are reached by address; the way to set one up was read off CommonLibSSE-NG's pull request 477
// (MIT) and Baked Goods by ItzIvy05.

#pragma once

#include "DimmerKeys.h"

#include <cstdint>
#include <span>

namespace DimmerKit
{
	namespace Detail
	{
		[[nodiscard]] inline RE::NiInterpController* NewDimmer()
		{
			using F = RE::NiInterpController* (*)();
			static REL::Relocation<F> make{ RELOCATION_ID(72049, 73695) };
			return make();
		}

		[[nodiscard]] inline RE::NiFloatInterpolator* NewInterpolator()
		{
			using F = RE::NiFloatInterpolator* (*)();
			static REL::Relocation<F> make{ RELOCATION_ID(71088, 72686) };
			return make();
		}

		[[nodiscard]] inline RE::NiFloatData* NewFloatData()
		{
			using F = RE::NiFloatData* (*)();
			static REL::Relocation<F> make{ RELOCATION_ID(71069, 72667) };
			return make();
		}

		[[nodiscard]] inline RE::NiFloatKey* NewLinearKeys(std::uint32_t a_count)
		{
			using F = RE::NiFloatKey* (*)(std::uint32_t);
			static REL::Relocation<F> make{ RELOCATION_ID(72540, 74222) };
			return make(a_count);
		}

		inline void ReplaceKeys(RE::NiFloatData* a_data, RE::NiFloatKey* a_keys, std::uint32_t a_count)
		{
			using F = void (*)(RE::NiFloatData*, RE::NiFloatKey*, std::uint32_t, RE::NiAnimationKey::KeyType);
			static REL::Relocation<F> replace{ RELOCATION_ID(71071, 72669) };
			replace(a_data, a_keys, a_count, RE::NiAnimationKey::KeyType::kLink);
		}
	}

	// the five ids are in the VR Address Library only from 0.295.0, and a missing id stops the game at its first use: with an
	// older one no keys are made and every light holds its steady fade
	[[nodiscard]] inline bool Ready()
	{
#ifdef ENABLE_SKYRIM_VR
		static const bool ready = [] {
			const bool ok = REL::IDDB::get().IsVRAddressLibraryAtLeastVersion("0.295.0");
			if (!ok) {
				SKSE::log::warn("VR Address Library older than 0.295.0: lights hold steady (no pulse, no fade) until it is updated");
			}
			return ok;
		}();
		return ready;
#else
		return true;
#endif
	}

	// keys the game can play: each key's value times a_fade (the light's steady fade with every factor on it). Empty when the game
	// would not make them; the light then holds its fade
	[[nodiscard]] inline RE::NiPointer<RE::NiFloatInterpolator> MakeKeys(std::span<const DimmerKeys::Key> a_keys, float a_fade)
	{
		if (!Ready() || a_keys.size() < 2) {
			return {};
		}
		RE::NiPointer<RE::NiFloatInterpolator> interpolator(Detail::NewInterpolator());
		const RE::NiPointer<RE::NiFloatData>   data(Detail::NewFloatData());
		if (!interpolator || !data) {
			return {};
		}
		const auto count = static_cast<std::uint32_t>(a_keys.size());
		auto*      keys = Detail::NewLinearKeys(count);
		if (!keys) {
			return {};
		}
		for (std::uint32_t i = 0; i < count; ++i) {
			keys[i].SetTime(a_keys[i].time);
			keys[i].SetValue(a_keys[i].value * a_fade);
		}
		Detail::ReplaceKeys(data.get(), keys, count);
		interpolator->floatData = data;
		return interpolator;
	}

	// the same keys at a new steady fade, written into the keys the dimmer is playing: the light takes the new fade on its next
	// engine update with no restart and no step in its pulse (the burning banners do this when a setting moves mid-burn). False
	// when the keys the game holds are not these (then MakeKeys and Play). Main thread
	inline bool Rewrite(const RE::NiPointer<RE::NiFloatInterpolator>& a_playing, std::span<const DimmerKeys::Key> a_keys, float a_fade)
	{
		auto* data = a_playing ? a_playing->floatData.get() : nullptr;
		if (!data || !data->keys || data->numKeys != a_keys.size()) {
			return false;
		}
		// the same count is not the same keys: every key's time is compared before any value is written, so keys of another shape
		// are never left half rewritten
		for (std::uint32_t i = 0; i < data->numKeys; ++i) {
			const auto* key = data->keys->GetKeyAt(i, data->keySize);
			if (!key || key->GetTime() != a_keys[i].time) {
				return false;
			}
		}
		for (std::uint32_t i = 0; i < data->numKeys; ++i) {
			data->keys->GetKeyAt(i, data->keySize)->SetValue(a_keys[i].value * a_fade);
		}
		return true;
	}

	// these keys on this light's dimmer from their start; the dimmer is made the first time. The game runs it from here on.
	// Main thread
	inline void Play(RE::NiLight* a_light, RE::NiPointer<RE::NiInterpController>& a_dimmer, const RE::NiPointer<RE::NiFloatInterpolator>& a_keys,
		float a_seconds, bool a_loop)
	{
		if (!a_keys || !a_light) {
			return;
		}
		if (!a_dimmer) {
			a_dimmer.reset(Detail::NewDimmer());
			if (!a_dimmer) {
				return;
			}
		}
		// a light that was made anew takes the dimmer over: the game runs a dimmer only through the light it hangs on
		if (a_dimmer->target != a_light) {
			using Flag = RE::NiAVObject::Flag;
			a_light->GetFlags().set(Flag::kSelectiveUpdate, Flag::kSelectiveUpdateController);
			a_dimmer->SetTarget(a_light);
		}
		using Time = RE::NiTimeController::Flag;
		a_keys->lastIndex = 0;
		a_dimmer->SetInterpolator(a_keys.get(), 0);
		a_dimmer->flags.reset(Time::kCycleType_Mask);
		a_dimmer->flags.set(Time::kAnimType_AppInit, Time::kComputeScaledTime, a_loop ? Time::kCycleType_Loop : Time::kCycleType_Clamp);
		a_dimmer->frequency = 1.0f;
		a_dimmer->phase = 0.0f;
		a_dimmer->loKeyTime = 0.0f;
		a_dimmer->hiKeyTime = a_seconds;
		a_dimmer->Start(0.0f);
	}
}
