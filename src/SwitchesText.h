// Illuminated - Core: its own small settings file as text - one switch per mod's part
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// SKSE\Plugins\IlluminatedCore\Settings.ini, section [Modules], one line per part: `illuminated = on`. The menu's Installed
// page writes it; a player whose menu does not open edits it by hand. A switch takes hold from the next game start. Read the way
// people edit a file: a byte order mark, any case, spaces, notes after ; or #. Nothing of the game in here (tests\testcore.cpp).

#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SwitchesText
{
	[[nodiscard]] inline std::string_view Trim(std::string_view a_text) noexcept
	{
		const auto space = [](char a_c) { return std::isspace(static_cast<unsigned char>(a_c)) != 0; };
		while (!a_text.empty() && space(a_text.front())) {
			a_text.remove_prefix(1);
		}
		while (!a_text.empty() && space(a_text.back())) {
			a_text.remove_suffix(1);
		}
		return a_text;
	}

	[[nodiscard]] inline bool Same(std::string_view a_left, std::string_view a_right) noexcept
	{
		return std::ranges::equal(a_left, a_right, [](char a_x, char a_y) {
			return std::tolower(static_cast<unsigned char>(a_x)) == std::tolower(static_cast<unsigned char>(a_y));
		});
	}

	// on / off, true / false, yes / no, 1 / 0, in any case; anything else is no answer
	[[nodiscard]] inline std::optional<bool> OnOff(std::string_view a_text) noexcept
	{
		for (const auto word : { "on", "true", "yes", "1" }) {
			if (Same(a_text, word)) {
				return true;
			}
		}
		for (const auto word : { "off", "false", "no", "0" }) {
			if (Same(a_text, word)) {
				return false;
			}
		}
		return std::nullopt;
	}

	struct Read
	{
		// one answer per part the file names, in the file's order, each part once
		std::vector<std::pair<std::string_view, bool>> switches{};
		// each line that could not be used, with its line number
		std::vector<std::string> problems{};
	};

	// a_modules: the ids this core knows; the views in the result point into it
	[[nodiscard]] inline Read Parse(std::string_view a_text, std::span<const std::string_view> a_modules)
	{
		Read        out;
		bool        ours = false;
		std::size_t number = 0;
		if (a_text.starts_with("\xEF\xBB\xBF")) {
			a_text.remove_prefix(3);
		}
		while (!a_text.empty()) {
			const auto end = a_text.find('\n');
			auto       line = a_text.substr(0, end);
			a_text.remove_prefix(end == std::string_view::npos ? a_text.size() : end + 1);
			++number;
			line = Trim(line.substr(0, line.find_first_of(";#")));
			if (line.empty()) {
				continue;
			}
			if (line.front() == '[' && line.back() == ']') {
				ours = Same(Trim(line.substr(1, line.size() - 2)), "Modules");
				continue;
			}
			if (!ours) {
				continue;
			}
			const auto eq = line.find('=');
			const auto key = Trim(line.substr(0, eq));
			const auto known = std::ranges::find_if(a_modules, [key](std::string_view a_id) { return Same(a_id, key); });
			if (eq == std::string_view::npos) {
				out.problems.push_back(std::format("line {}: no = on this line", number));
				continue;
			}
			if (known == a_modules.end()) {
				out.problems.push_back(std::format("line {}: no part of this name", number));
				continue;
			}
			const auto answer = OnOff(Trim(line.substr(eq + 1)));
			if (!answer) {
				out.problems.push_back(std::format("line {}: {} is neither on nor off; it stays on", number, *known));
				continue;
			}
			if (std::ranges::find(out.switches, *known, &std::pair<std::string_view, bool>::first) != out.switches.end()) {
				out.problems.push_back(std::format("line {}: {} is written twice; the first stays", number, *known));
				continue;
			}
			out.switches.emplace_back(*known, *answer);
		}
		return out;
	}

	// a part is on unless the file switches it off
	[[nodiscard]] inline bool IsOn(const Read& a_read, std::string_view a_module) noexcept
	{
		const auto it = std::ranges::find(a_read.switches, a_module, &std::pair<std::string_view, bool>::first);
		return it == a_read.switches.end() || it->second;
	}

	// the whole file, every part with its switch
	[[nodiscard]] inline std::string Text(std::span<const std::pair<std::string_view, bool>> a_switches)
	{
		std::string out =
			"; Illuminated - Core - which parts run. Read when the game starts; a change takes hold from the next start.\n"
			"; Each line: the part's name, then on or off. The same switches are on the menu's Installed page.\n\n[Modules]\n";
		for (const auto& [module, on] : a_switches) {
			out += std::format("{} = {}\n", module, on ? "on" : "off");
		}
		return out;
	}
}
