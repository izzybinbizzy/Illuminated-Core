// Illuminated Core Framework - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// DevBench, when it is in the load order (it is optional; nothing here runs without it):
//   inspect kind=illuminatedcore   the core's version, the parts built in, every installed mod with its version and state, each
//                                  part's switch for the next game start, every problem line of this start, and the hook table:
//                                  each handler's part, whether it is armed, how often it ran and what a run cost
//   illuminatedcore.control        action = switch (part + value: that part's switch, the way the menu's Installed page sets it),
//                                  meter (value 1 on, 0 off: the hook table times every handler that runs), resetcounts

#include "Kernel.h"

#include "DevBenchGlue.h"
#include "Hooks.h"

#include <string>
#include <utility>

namespace Core
{
	namespace
	{
		using DevBenchGlue::json;

		constexpr const char* kKey = "illuminatedcore";

		constexpr const char* kDescriptor =
			R"json({"description":"Illuminated Core Framework (one plugin for every Illuminated mod): its version, the lighting it found at data load (lighting: Community Shaders, ENB or Vanilla; lightingRead false before data load; inverseSquare and lightLimitFix as every light of the core is dressed by), the parts built into it, every installed mod's manifest with its data version and whether its part runs this session (runs, switched off, stays off), each part's switch for the next game start, every problem line found when the game started (the same lines as its message box, its Problems found page and IlluminatedCore.log), how many game functions hold a hook of the core (hookRowsWritten: 0 while no part needs one), and the hook table (hooks: per slot whether it is written and metered, and each handler in the order it runs with its part, when = before or after the game's own call, armed, runs since the last resetcounts, and usAvg / usMax in microseconds, both 0 until illuminatedcore.control action=meter switched the meter on). Read only.","inputSchema":{"type":"object","properties":{}},"readOnly":true})json";

		constexpr const char* kTool =
			R"json({"description":"Illuminated Core Framework: action=switch sets one part's switch the way the menu's Installed page does (part = its id as inspect kind=illuminatedcore lists it under parts, value 1 on or 0 off); it is saved to Settings.ini and takes hold from the next game start; the reply is every part's switch after the change. action=meter (value 1 on or 0 off) has the hook table time every handler that runs: off, a running handler pays one read; a slot nobody armed never reaches the meter. action=resetcounts puts every handler's runs and times back to 0. Both reply with the hook table after the change. Read state with inspect kind=illuminatedcore.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["switch","meter","resetcounts"]},"part":{"type":"string"},"value":{"type":"number"}},"required":["action"]}})json";

		[[nodiscard]] const char* StateName(SurveyText::State a_state)
		{
			switch (a_state) {
			case SurveyText::State::kRuns:
				return "runs";
			case SurveyText::State::kSwitchedOff:
				return "switched off";
			case SurveyText::State::kStaysOff:
			default:
				return "stays off";
			}
		}

		[[nodiscard]] json SwitchesJson()
		{
			json out = json::object();
			for (const auto& [part, on] : SwitchesNow()) {
				out[std::string(part)] = on;
			}
			return out;
		}

		[[nodiscard]] json HooksJson()
		{
			json out = json::array();
			for (const auto& row : Hooks::Rows()) {
				json handlers = json::array();
				for (const auto& handler : row.handlers) {
					handlers.push_back({ { "part", handler.part }, { "when", handler.before ? "before" : handler.instead ? "instead" :
																														   "after" },
						{ "armed", handler.armed },
						{ "runs", handler.runs }, { "usAvg", handler.usAvg }, { "usMax", handler.usMax } });
				}
				out.push_back({ { "slot", row.slot }, { "moment", row.moment }, { "written", row.written }, { "metered", row.metered },
					{ "handlers", std::move(handlers) } });
			}
			return out;
		}

		// the survey is made once at post-load and never changed after: read as it is, on DevBench's own thread
		[[nodiscard]] json StateNow()
		{
			const auto survey = SurveyNow();
			json       parts = json::array();
			for (const auto& part : Parts()) {
				parts.push_back({ { "id", std::string(part.id) }, { "lowestData", ManifestText::Shown(part.lowestData) }, { "lighting", part.lighting } });
			}
			json rows = json::array();
			for (const auto& row : survey->rows) {
				rows.push_back({ { "part", row.module }, { "title", row.title }, { "dataVersion", ManifestText::Shown(row.dataVersion) },
					{ "state", StateName(row.state) }, { "known", row.known }, { "namedTwice", row.twice } });
			}
			const auto lighting = LightingNow();
			return json{ { "version", ManifestText::Shown(VersionNow()) }, { "lighting", lighting.name }, { "lightingRead", lighting.read },
				{ "inverseSquare", lighting.inverseSquare }, { "lightLimitFix", lighting.lightLimitFix }, { "parts", std::move(parts) }, { "installed", std::move(rows) },
				{ "switchesNextStart", SwitchesJson() }, { "settingsFileUnread", survey->switchesUnread }, { "problems", survey->problems },
				{ "hookRowsWritten", Hooks::WrittenCount() }, { "hooks", HooksJson() } };
		}

		void Handler(void*, const char*, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
		{
			try {
				DevBenchGlue::Reply(a_sink, a_write, StateNow());
			} catch (...) {
				// the refusal is JSON too and may throw (low memory): never out of a noexcept function
				try {
					DevBenchGlue::Reply(a_sink, a_write, DevBenchGlue::Refusal("the report could not be made"));
				} catch (...) {
				}
			}
		}

		[[nodiscard]] json Act(const json& a_args)
		{
			const auto action = DevBenchGlue::Text(a_args, "action");
			const auto value = DevBenchGlue::Number(a_args, "value");
			if (action == "resetcounts") {
				Hooks::ResetCounts();
				return json{ { "ok", true }, { "hooks", HooksJson() } };
			}
			if (action == "meter") {
				if (!value) {
					return DevBenchGlue::Refusal("meter needs a number value: 1 on, 0 off");
				}
				Hooks::Meter(*value != 0.0);
				return json{ { "ok", true }, { "hooks", HooksJson() } };
			}
			if (action != "switch") {
				return DevBenchGlue::Refusal("unknown action '" + action + "'", json::array({ "switch", "meter", "resetcounts" }));
			}
			const auto part = DevBenchGlue::Text(a_args, "part");
			if (!value) {
				return DevBenchGlue::Refusal("switch needs a number value: 1 on, 0 off");
			}
			if (!SetSwitch(part, *value != 0.0)) {
				json known = json::array();
				for (const auto& each : Parts()) {
					known.push_back(std::string(each.id));
				}
				return DevBenchGlue::Refusal("no part '" + part + "' in this core", std::move(known));
			}
			// a Settings.ini that could not be read at the start is never written over: the switch then holds for this session only
			return json{ { "ok", true }, { "part", part }, { "saved", !SurveyNow()->switchesUnread }, { "switchesNextStart", SwitchesJson() } };
		}

		void Control(void*, const char* a_args, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
		{
			try {
				DevBenchGlue::Reply(a_sink, a_write, Act(DevBenchGlue::Args(a_args)));
			} catch (...) {
				try {
					DevBenchGlue::Reply(a_sink, a_write, DevBenchGlue::Refusal("Illuminated Core Framework could not handle that"));
				} catch (...) {
				}
			}
		}
	}

	void OfferToDevBench()
	{
		auto* devbench = DevBenchAPI::GetDevBenchInterface001();
		if (!devbench) {
			SKSE::log::info("[DEVBENCH] DevBench is not in this load order");
			return;
		}
		const auto build = devbench->GetBuildNumber();
		if (build < DevBenchGlue::kNeedsBuild) {
			SKSE::log::warn("[DEVBENCH] DevBench build {} is older than {} and cannot take an inspect extension", build, DevBenchGlue::kNeedsBuild);
			return;
		}
		const bool fresh = devbench->RegisterToolExtension("inspect", kKey, kDescriptor, Handler, nullptr);
		devbench->RegisterTool("illuminatedcore.control", kTool, Control, nullptr);
		SKSE::log::info("[DEVBENCH] offered to DevBench build {}: inspect kind={} ({}), the tool illuminatedcore.control", build, kKey,
			fresh ? "a new entry" : "replacing one already registered");
	}
}
