// Illuminated - Core - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// The kernel's test part, compiled only into the testing build (CORE_PROBE): the routes the core is to stand on are tried here on
// the game's own lights before any mod's part rests on them. DevBench reaches it as the tool illuminatedcore.probe:
//   action=dimmer  hand=left|right kind=<a pulse shape's name> depth=<number>   the game's dimmer put on that hand's casting
//                  light, playing that shape as baked keys (an overshoot first: depth above 1, up to 2, swings wider than any shipped shape)
//   action=read    hand=left|right   the casting light's fade now, and whether a dimmer of this part is on it - read twice a
//                  moment apart, a fade that moved with no code of ours running is the engine playing the keys
//   action=release hand=left|right   the dimmer stopped and the light's fade put back to what it was
// Nothing here runs without a DevBench request, and nothing of it is in a shipped build.

#ifdef CORE_PROBE

#	include "Kernel.h"

#	include "DevBenchGlue.h"
#	include "DimmerKit.h"
#	include "Hooks.h"

#	include <algorithm>
#	include <array>
#	include <atomic>
#	include <cmath>
#	include <cstddef>
#	include <cstdint>
#	include <filesystem>
#	include <string>
#	include <system_error>

namespace Core
{
	namespace
	{
		using DevBenchGlue::json;

		constexpr const char* kTool =
			R"json({"description":"Illuminated - Core, testing build only: tries the engine routes on the game's own lights. action=dimmer puts the game's light dimmer on the player's casting light of one hand (hand left or right; a spell must be drawn) and plays a pulse shape as baked keys (kind = healing, absorbhealth, absorbmagicka, absorbstamina, illusion, conjuration, alteration, magelight, ward, rune, or one of the four played as a 12-second loop: fire, frost, shock, poison; depth = how wide it swings, 1 as shipped, up to 2 to overshoot, in steps of 0.05; the reply gives depthAsked and the depth played). action=read gives that light's fade now. action=release stops the dimmer and puts the fade back. action=frames (no hand) tries the shared hook on the player's update, only in a start with the file Data/SKSE/Plugins/IlluminatedCore/Probe Frames.txt in place (else written is false and a note says so): with count=N its handler counts N frames and switches itself off, without count it only reads - handlerRuns stands still while nothing is armed. Each reply is the state after the action, read on the main thread.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["dimmer","read","release","frames"]},"count":{"type":"number"},"hand":{"type":"string","enum":["left","right"]},"kind":{"type":"string"},"depth":{"type":"number"}},"required":["action"]}})json";

		// one hand's trial: the light the dimmer was put on, the dimmer, and the fade the light had before
		struct Trial
		{
			RE::NiPointer<RE::NiLight>            light{};
			RE::NiPointer<RE::NiInterpController> dimmer{};
			float                                 fade{ 0.0f };
		};

		// main thread only
		std::array<Trial, 2> gTrials{};

		// the frames trial writes a hook on the player's update for the whole session, so even the testing build writes it only
		// when a test asks for it at the start, with this file
		constexpr const char* kFramesOn = "Data/SKSE/Plugins/IlluminatedCore/Probe Frames.txt";

		// the hook table's own trial: a handler on the player's update that counts a given number of frames and switches itself
		// off. Its runs before, during and after show the idle rule: a handler nobody armed is never called
		HookDispatch::Armed        gFrames{};
		std::atomic<std::int32_t>  gFramesLeft{ 0 };
		std::atomic<std::uint32_t> gFramesSeen{ 0 };

		void CountFrame(RE::PlayerCharacter*, float)
		{
			gFramesSeen.fetch_add(1, std::memory_order_relaxed);
			if (gFramesLeft.fetch_sub(1, std::memory_order_relaxed) <= 1) {
				gFrames.Disarm();
			}
		}

		// main thread, where the table is written: what the trial stands at
		[[nodiscard]] json FramesSeen()
		{
			const auto seen = Hooks::gPlayerUpdate.Handlers() > 0 ? Hooks::gPlayerUpdate.At(0) : decltype(Hooks::gPlayerUpdate.At(0)){};
			json       out{ { "ok", true }, { "written", Hooks::gPlayerUpdate.Sealed() }, { "armed", gFrames.IsArmed() },
				{ "framesCounted", gFramesSeen.load(std::memory_order_relaxed) }, { "handlerRuns", seen.runs },
				{ "framesLeft", gFramesLeft.load(std::memory_order_relaxed) } };
			if (!gFrames) {
				out["note"] = std::string("the frames trial is off in this start: no hook is written for it. Put the file ") + kFramesOn +
				              " in place and start the game again";
			}
			return out;
		}

		// the casting light the game made for that hand, or nothing (no spell drawn, or the light is not made yet). Main thread
		[[nodiscard]] RE::NiLight* CastingLight(std::size_t a_hand)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return nullptr;
			}
			const auto* caster = player->GetActorRuntimeData().magicCasters[a_hand];
			return caster && caster->light ? caster->light->light.get() : nullptr;
		}

		[[nodiscard]] json Seen(std::size_t a_hand)
		{
			auto*       light = CastingLight(a_hand);
			const auto& trial = gTrials[a_hand];
			if (!light) {
				return json{ { "ok", true }, { "light", false }, { "note", "no casting light on that hand: a spell has to be drawn" } };
			}
			return json{ { "ok", true }, { "light", true }, { "name", std::string(light->name.c_str() ? light->name.c_str() : "") },
				{ "fade", light->GetLightRuntimeData().fade }, { "ours", trial.light.get() == light && trial.dimmer != nullptr },
				{ "fadeBefore", trial.fade } };
		}

		void Release(std::size_t a_hand)
		{
			auto& trial = gTrials[a_hand];
			if (trial.dimmer) {
				trial.dimmer->Stop();
				trial.dimmer->SetTarget(nullptr);
			}
			if (trial.light) {
				trial.light->GetLightRuntimeData().fade = trial.fade;
			}
			trial = {};
		}

		[[nodiscard]] json Act(const json& a_args)
		{
			const auto action = DevBenchGlue::Text(a_args, "action");
			if (action == "frames") {
				// count = how many frames the handler counts before it switches itself off; none given only reads. The count is set and
				// the handler armed on the main thread, where it counts, so a request never crosses a handler switching itself off
				const auto         count = DevBenchGlue::Number(a_args, "count");
				const std::int32_t frames = count && *count >= 1.0 ? static_cast<std::int32_t>(std::clamp(*count, 1.0, 100000.0)) : 0;
				const auto         now = DevBenchGlue::OnMainThread([frames]() {
					if (frames > 0 && gFrames) {
						gFramesLeft.store(frames, std::memory_order_relaxed);
						gFrames.Arm();
					}
					return FramesSeen();
				});
				return now ? *now : DevBenchGlue::NotRunYet();
			}
			const auto hand = DevBenchGlue::Text(a_args, "hand");
			if (hand != "left" && hand != "right") {
				return DevBenchGlue::Refusal("hand is left or right", json::array({ "left", "right" }));
			}
			const std::size_t slot = hand == "left" ? 0 : 1;
			if (action == "read") {
				const auto now = DevBenchGlue::OnMainThread([slot]() { return Seen(slot); });
				return now ? *now : DevBenchGlue::NotRunYet();
			}
			if (action == "release") {
				const auto now = DevBenchGlue::OnMainThread([slot]() {
					Release(slot);
					return Seen(slot);
				});
				return now ? *now : DevBenchGlue::NotRunYet();
			}
			if (action != "dimmer") {
				return DevBenchGlue::Refusal("unknown action '" + action + "'", json::array({ "dimmer", "read", "release", "frames" }));
			}
			const auto kind = Pulse::KindOf(DevBenchGlue::Text(a_args, "kind"));
			const auto asked = DevBenchGlue::Number(a_args, "depth").value_or(1.0);
			if (!kind || !(DimmerKeys::Period(*kind) > 0.0)) {
				return DevBenchGlue::Refusal("kind names no pulse shape",
					json::array({ "healing", "absorbhealth", "absorbmagicka", "absorbstamina", "illusion", "conjuration", "alteration", "magelight", "ward", "rune",
						"fire", "frost", "shock", "poison" }));
			}
			// held to the widest proven swing, in twentieths, so a sweep of depths keeps few traced loops
			const auto depth = static_cast<float>(std::round(std::clamp(asked, 0.1, static_cast<double>(DimmerKeys::kMostDepth)) * 20.0) / 20.0);
			const auto loop = DimmerKeys::Bake(*kind, 1, depth, 0.0);
			if (!loop) {
				return DevBenchGlue::Refusal("that shape gave no keys");
			}
			const auto now = DevBenchGlue::OnMainThread([slot, loop, asked, depth]() {
				auto* light = CastingLight(slot);
				if (!light) {
					return Seen(slot);
				}
				Release(slot);
				auto& trial = gTrials[slot];
				trial.light.reset(light);
				trial.fade = light->GetLightRuntimeData().fade;
				const auto keys = DimmerKit::MakeKeys(loop->keys, trial.fade);
				if (!keys) {
					trial = {};
					return DevBenchGlue::Refusal("the game made no keys");
				}
				DimmerKit::Play(light, trial.dimmer, keys, loop->period, true);
				json seen = Seen(slot);
				seen["keys"] = loop->keys.size();
				seen["period"] = loop->period;
				seen["depthAsked"] = asked;
				seen["depth"] = depth;
				return seen;
			});
			return now ? *now : DevBenchGlue::NotRunYet();
		}

		void Handler(void*, const char* a_args, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
		{
			try {
				DevBenchGlue::Reply(a_sink, a_write, Act(DevBenchGlue::Args(a_args)));
			} catch (...) {
				// the refusal is JSON too and may throw (low memory): never out of a noexcept function
				try {
					DevBenchGlue::Reply(a_sink, a_write, DevBenchGlue::Refusal("the test part could not handle that"));
				} catch (...) {
				}
			}
		}
	}

	// before the post-load rows are written: the test part's one handler, so the player's update row has a reason to be written -
	// only when the test asked for the frames trial with its file
	void ProbeHooks()
	{
		std::error_code ec;
		if (!std::filesystem::exists(kFramesOn, ec)) {
			SKSE::log::info("[DEVBENCH] TESTING BUILD: the frames trial is off ({} is not there): no hook is written for it", kFramesOn);
			return;
		}
		gFrames = Hooks::gPlayerUpdate.AddAfter("the test part", CountFrame);
		SKSE::log::info("[DEVBENCH] TESTING BUILD: the frames trial is on ({}): its hook on the player's update is written", kFramesOn);
	}

	// a save is about to load or a new game starts: the lights of the session before go with it, and so does every trial on them
	void ProbeForget()
	{
		for (std::size_t hand = 0; hand < gTrials.size(); ++hand) {
			Release(hand);
		}
	}

	void OfferProbe()
	{
		auto* devbench = DevBenchAPI::GetDevBenchInterface001();
		if (!devbench || devbench->GetBuildNumber() < DevBenchGlue::kNeedsBuild) {
			return;
		}
		devbench->RegisterTool("illuminatedcore.probe", kTool, Handler, nullptr);
		SKSE::log::info("[DEVBENCH] TESTING BUILD: the tool illuminatedcore.probe is offered");
	}
}

#endif
