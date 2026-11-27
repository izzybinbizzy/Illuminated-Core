// Illuminated Core Framework - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// The kernel's test part, compiled only into the testing build (CORE_PROBE): the routes the core is to stand on are tried here on
// the game's own lights before any mod's part rests on them. DevBench reaches it as the tool illuminatedcore.probe:
//   action=dimmer  hand=left|right kind=<a pulse shape's name> depth=<number>   the game's dimmer put on that hand's casting
//                  light, playing that shape as baked keys (an overshoot first: depth above 1, up to 2, swings wider than any shipped shape)
//   action=read    hand=left|right   the casting light's fade now, and whether a dimmer of this part is on it - read twice a
//                  moment apart, a fade that moved with no code of ours running is the engine playing the keys
//   action=release hand=left|right   the dimmer stopped and the light's fade put back to what it was; the test module's light let go
// THE TEST MODULE (one light, one slider, one dimmer - the kernel's design, step 4 of its order):
//   action=light   hand=left|right   one light of the core's own beside that hand's magic node, dressed for the lighting found
//   action=slider  hand=left|right value=<0 to 3>   its brightness share through the composer; written only when it moves
//   action=dimmer  ... target=own    the pulse on that light, its keys shares of the composer's product (a slider rewrites them)
// Nothing here runs without a DevBench request, and nothing of it is in a shipped build.

#ifdef CORE_PROBE

#	include "Kernel.h"

#	include "Composer.h"
#	include "DevBenchGlue.h"
#	include "DimmerKit.h"
#	include "Hooks.h"
#	include "LightKit.h"

#	include <algorithm>
#	include <array>
#	include <atomic>
#	include <cmath>
#	include <cstddef>
#	include <cstdint>
#	include <filesystem>
#	include <optional>
#	include <string>
#	include <system_error>

namespace Core
{
	namespace
	{
		using DevBenchGlue::json;

		constexpr const char* kTool =
			R"json({"description":"Illuminated Core Framework, testing build only: tries the engine routes on the game's own lights. action=dimmer puts the game's light dimmer on the player's casting light of one hand (hand left or right; a spell must be drawn) and plays a pulse shape as baked keys (kind = healing, absorbhealth, absorbmagicka, absorbstamina, illusion, conjuration, alteration, magelight, ward, rune, or one of the four played as a 12-second loop: fire, frost, shock, poison; depth = how wide it swings, 1 as shipped, up to 2 to overshoot, in steps of 0.05; the reply gives depthAsked and the depth played). action=read gives that light's fade now. action=release stops the dimmer and puts the fade back. THE TEST MODULE (one light, one slider, one dimmer): action=light hangs one light of the core's own beside that hand's magic node (a spell must be drawn; candle gold, reach 300, dressed for the lighting found); action=slider value=<0 to 3> sets that light's brightness share through the composer (1 as made); action=dimmer target=own plays the pulse on that light instead of the casting light; action=read reports both lights; action=release lets both go. action=frames (no hand) tries the shared hook on the player's update, only in a start with the file Data/SKSE/Plugins/IlluminatedCore/Probe Frames.txt in place (else written is false and a note says so): with count=N its handler counts N frames and switches itself off, without count it only reads - handlerRuns stands still while nothing is armed. Each reply is the state after the action, read on the main thread.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["dimmer","read","release","frames","light","slider"]},"count":{"type":"number"},"hand":{"type":"string","enum":["left","right"]},"kind":{"type":"string"},"depth":{"type":"number"},"target":{"type":"string","enum":["casting","own"]},"value":{"type":"number"}},"required":["action"]}})json";

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
				{ "fade", light->GetLightRuntimeData().fade }, { "thisPart", trial.light.get() == light && trial.dimmer != nullptr },
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

		// THE TEST MODULE: one light of the core's own beside a hand's magic node - under the node's parent bone at the node's own
		// offset, since the caster takes over any light beneath the node itself (GenDynamic, the plugin framework's 7a) - its
		// brightness one product through the composer, a slider as the setting's share, and the game's dimmer playing a pulse on it
		struct Own
		{
			RE::NiPointer<RE::NiPointLight>        light{};
			RE::NiPointer<RE::BSLight>             bs{};
			RE::NiPointer<RE::NiInterpController>  dimmer{};
			RE::NiPointer<RE::NiFloatInterpolator> keys{};
			std::optional<DimmerKeys::Loop>        loop{};
			Composer::Light                        record{};
		};

		// main thread only
		std::array<Own, 2> gOwn{};

		constexpr float       kOwnReach = 300.0f;
		constexpr RE::NiColor kOwnColor{ 1.0f, 201.0f / 255.0f, 77.0f / 255.0f };

		// the fade a light of ours wears for a composer product on the lighting found: the product itself where a shader draws
		// inverse square, raised the way every plain light of ours is elsewhere (PlainAt). Dress, the dimmer's keys and a slider's
		// rewrite of them all take this one, so a pulse swings round the fade the steady light had
		[[nodiscard]] float WornFade(float a_product)
		{
			const float fade = (std::max)(a_product, 0.0f);
			return InverseSquare() ? fade : LightKit::PlainAt(fade, kOwnReach, kOwnReach).fade;
		}

		// the largest share of the steady fade a loop's keys reach (1 for a steady light): an inverse-square light's cutoff is set
		// for that peak, so a pulse never carries the pool past its reach
		[[nodiscard]] float PeakOf(const std::optional<DimmerKeys::Loop>& a_loop)
		{
			float peak = 1.0f;
			if (a_loop) {
				for (const auto& key : a_loop->keys) {
					peak = (std::max)(peak, key.value);
				}
			}
			return peak;
		}

		// the light's color, reach and fade for the lighting found, the way every light of ours is dressed (Creatures' Dress);
		// a_writeFade false while the game's dimmer plays the fade - then only the reach follows the product
		void Dress(RE::NiPointLight* a_light, float a_product, float a_peak, bool a_writeFade)
		{
			auto& data = a_light->GetLightRuntimeData();
			data.diffuse = kOwnColor;
			const float fade = WornFade(a_product);
			if (InverseSquare()) {
				if (a_writeFade) {
					data.fade = fade;
				}
				data.radius = { kOwnReach, kOwnReach, LightKit::kLightSize };
				a_light->SetLightAttenuation(kOwnReach);
				LightKit::Isl::SetOwn(a_light);
				LightKit::Isl::SetCutoff(a_light, LightKit::CutoffFor((std::max)(fade * a_peak, 0.001f), kOwnReach));
				return;
			}
			const auto plain = LightKit::PlainAt((std::max)(a_product, 0.0f), kOwnReach, kOwnReach);
			if (a_writeFade) {
				data.fade = plain.fade;
			}
			data.radius = { plain.radius, plain.radius, LightKit::kLightSize };
			a_light->SetLightAttenuation(plain.radius);
			if (LightLimitFix()) {
				LightKit::Isl::SetOwnLinear(a_light);
			} else {
				data.ambient = LightKit::Tenth(kOwnColor);
			}
		}

		// the composer's product onto the light: through the playing dimmer's keys when it pulses (its reach set again for the new
		// peak; keys the game no longer holds are made and played anew), else as its steady fade. False when nothing reached the light
		bool WriteOwn(Own& a_own)
		{
			if (!a_own.light) {
				return false;
			}
			const float product = Composer::Take(a_own.record);
			if (!a_own.loop || !a_own.dimmer) {
				Dress(a_own.light.get(), product, 1.0f, true);
				return true;
			}
			const float worn = WornFade(product);
			if (!DimmerKit::Rewrite(a_own.keys, a_own.loop->keys, worn)) {
				const auto keys = DimmerKit::MakeKeys(a_own.loop->keys, worn);
				if (!keys) {
					return false;
				}
				DimmerKit::Play(a_own.light.get(), a_own.dimmer, keys, a_own.loop->period, true);
				a_own.keys = keys;
			}
			Dress(a_own.light.get(), product, PeakOf(a_own.loop), false);
			return true;
		}

		void ReleaseOwn(std::size_t a_hand)
		{
			auto& own = gOwn[a_hand];
			if (own.dimmer) {
				own.dimmer->Stop();
				own.dimmer->SetTarget(nullptr);
			}
			LightKit::TakeOff(LightKit::Scene(), own.bs);
			if (own.light && own.light->parent) {
				own.light->parent->DetachChild(own.light.get());
			}
			own = {};
		}

		[[nodiscard]] json OwnSeen(std::size_t a_hand)
		{
			const auto& own = gOwn[a_hand];
			if (!own.light) {
				return json{ { "made", false } };
			}
			const auto& data = own.light->GetLightRuntimeData();
			const auto  at = own.light->world.translate;
			return json{ { "made", true }, { "fade", data.fade }, { "radius", data.radius.x }, { "product", Composer::Product(own.record) },
				{ "slider", own.record.share[static_cast<std::size_t>(Composer::Voice::kSetting)] }, { "pulsing", own.dimmer != nullptr },
				{ "at", { at.x, at.y, at.z } }, { "lighting", LightingName() }, { "inverseSquare", InverseSquare() } };
		}

		// a light of ours beside the hand's magic node; refused with no spell drawn
		[[nodiscard]] json MakeOwn(std::size_t a_hand)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* caster = player ? player->GetActorRuntimeData().magicCasters[a_hand] : nullptr;
			auto* node = caster ? caster->magicNode : nullptr;
			auto* parent = node ? node->parent : nullptr;
			auto* scene = LightKit::Scene();
			if (!parent || !scene) {
				return DevBenchGlue::Refusal("no magic node on that hand: a spell has to be drawn");
			}
			ReleaseOwn(a_hand);
			auto&                           own = gOwn[a_hand];
			RE::NiPointer<RE::NiPointLight> light(LightKit::CloneLight());
			if (!light) {
				return DevBenchGlue::Refusal("the game made no light");
			}
			light->name = "IlluminatedCoreTestLight";
			light->local.translate = node->local.translate;
			own.record.base = 1.0f;
			Composer::Forget(own.record);
			Dress(light.get(), Composer::Take(own.record), 1.0f, true);
			RE::NiPointer<RE::BSLight> bs(LightKit::Hang(parent, light.get(), scene));
			if (!bs) {
				own = {};
				return DevBenchGlue::Refusal("the scene took no light");
			}
			own.light = light;
			own.bs = bs;
			json out{ { "ok", true } };
			out["own"] = OwnSeen(a_hand);
			return out;
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
				const auto now = DevBenchGlue::OnMainThread([slot]() {
					json seen = Seen(slot);
					seen["own"] = OwnSeen(slot);
					return seen;
				});
				return now ? *now : DevBenchGlue::NotRunYet();
			}
			if (action == "release") {
				const auto now = DevBenchGlue::OnMainThread([slot]() {
					Release(slot);
					ReleaseOwn(slot);
					json seen = Seen(slot);
					seen["own"] = OwnSeen(slot);
					return seen;
				});
				return now ? *now : DevBenchGlue::NotRunYet();
			}
			if (action == "light") {
				const auto now = DevBenchGlue::OnMainThread([slot]() { return MakeOwn(slot); });
				return now ? *now : DevBenchGlue::NotRunYet();
			}
			if (action == "slider") {
				const auto value = DevBenchGlue::Number(a_args, "value");
				if (!value || !std::isfinite(*value)) {
					return DevBenchGlue::Refusal("slider needs a number value, 0 to 3 (1 as made)");
				}
				const auto share = static_cast<float>(std::clamp(*value, 0.0, 3.0));
				const auto now = DevBenchGlue::OnMainThread([slot, share]() {
					auto& own = gOwn[slot];
					if (!own.light) {
						return DevBenchGlue::Refusal("no light of the test module on that hand: action=light first");
					}
					// the composer says whether the light has to be written at all: a share that changes nothing writes nothing
					const bool due = Composer::Set(own.record, Composer::Voice::kSetting, share);
					const bool written = due && WriteOwn(own);
					json       out{ { "ok", true }, { "written", written } };
					out["own"] = OwnSeen(slot);
					return out;
				});
				return now ? *now : DevBenchGlue::NotRunYet();
			}
			if (action != "dimmer") {
				return DevBenchGlue::Refusal("unknown action '" + action + "'", json::array({ "dimmer", "read", "release", "frames", "light", "slider" }));
			}
			const auto target = DevBenchGlue::Text(a_args, "target");
			if (!target.empty() && target != "casting" && target != "own") {
				return DevBenchGlue::Refusal("target is casting (the game's casting light, as without a target) or own (the test module's light)",
					json::array({ "casting", "own" }));
			}
			const bool onOwn = target == "own";
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
			if (onOwn) {
				const auto now = DevBenchGlue::OnMainThread([slot, loop, asked, depth]() {
					auto& own = gOwn[slot];
					if (!own.light) {
						return DevBenchGlue::Refusal("no light of the test module on that hand: action=light first");
					}
					// the keys are shares of the fade the steady light wears for the composer's product: a slider after this rewrites
					// them in place, and the reach is set for the loop's peak
					const float product = Composer::Product(own.record);
					const auto  keys = DimmerKit::MakeKeys(loop->keys, WornFade(product));
					if (!keys) {
						return DevBenchGlue::Refusal("the game made no keys");
					}
					DimmerKit::Play(own.light.get(), own.dimmer, keys, loop->period, true);
					own.keys = keys;
					own.loop = loop;
					Dress(own.light.get(), product, PeakOf(own.loop), false);
					json out{ { "ok", true }, { "keys", loop->keys.size() }, { "period", loop->period }, { "depthAsked", asked }, { "depth", depth } };
					out["own"] = OwnSeen(slot);
					return out;
				});
				return now ? *now : DevBenchGlue::NotRunYet();
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
			ReleaseOwn(hand);
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
