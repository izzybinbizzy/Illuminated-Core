// Illuminated - Core - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// DevBench, when it is in the load order (it is optional; nothing here runs without it):
//   inspect kind=illuminatedcore   the core's version, the parts built in, every installed mod with its version and state, each
//                                  part's switch for the next game start, and every problem line of this start
//   illuminatedcore.control        action = switch (part + value: that part's switch, the way the menu's Installed page sets it)

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
			R"json({"description":"Illuminated - Core (one plugin for every Illuminated mod): its version, the parts built into it, every installed mod's manifest with its data version and whether its part runs this session (runs, switched off, stays off), each part's switch for the next game start, every problem line found when the game started (the same lines as its message box, its Problems found page and IlluminatedCore.log), and how many game functions hold a hook of the core (hookRowsWritten: 0 while no part needs one). Read only.","inputSchema":{"type":"object","properties":{}},"readOnly":true})json";

		constexpr const char* kTool =
			R"json({"description":"Illuminated - Core: action=switch sets one part's switch the way the menu's Installed page does (part = its id as inspect kind=illuminatedcore lists it under parts, value 1 on or 0 off); it is saved to Settings.ini and takes hold from the next game start. The reply is every part's switch after the change. Read state with inspect kind=illuminatedcore.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["switch"]},"part":{"type":"string"},"value":{"type":"number"}},"required":["action","part","value"]}})json";

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
			return json{ { "version", ManifestText::Shown(VersionNow()) }, { "parts", std::move(parts) }, { "installed", std::move(rows) },
				{ "switchesNextStart", SwitchesJson() }, { "settingsFileUnread", survey->switchesUnread }, { "problems", survey->problems },
				{ "hookRowsWritten", Hooks::WrittenCount() } };
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
			if (action != "switch") {
				return DevBenchGlue::Refusal("unknown action '" + action + "'", json::array({ "switch" }));
			}
			const auto part = DevBenchGlue::Text(a_args, "part");
			const auto value = DevBenchGlue::Number(a_args, "value");
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
					DevBenchGlue::Reply(a_sink, a_write, DevBenchGlue::Refusal("Illuminated - Core could not handle that"));
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
