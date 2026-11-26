// Illuminated - Core: the survey - which parts run this session, which stay off, and every line a player is told
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// One look at post-load, from text alone: the manifests found, the switches file, the old plugin files still loaded. Its result
// is what the log, the menu's Installed and Problems found pages, the one message box and DevBench all show, so the four never
// disagree. Nothing of the game in here (tests\testcore.cpp); every file is hostile input and nothing throws.

#pragma once

#include "ManifestText.h"
#include "SwitchesText.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SurveyText
{
	using ManifestText::Known;
	using ManifestText::Manifest;
	using ManifestText::Verdict;
	using ManifestText::Version;

	// a file of the Modules folder: its name, and its text when it could be read
	struct File
	{
		std::string                name{};
		std::optional<std::string> text{};
	};

	// a version of a mod from before the core, found installed: the part it belongs to, the mod's name, the file that shows it,
	// and whether that file is a plugin that runs
	struct Old
	{
		std::string_view part{};
		std::string_view name{};
		std::string_view file{};
		bool             runs{ false };
	};

	struct Input
	{
		std::span<const File>             files{};
		std::string_view                  switches{};
		std::span<const Known>            parts{};
		Version                           core{};
		std::span<const std::string_view> features{};
		std::span<const Old>              old{};
		// Settings.ini is there but could not be read: every switch counts as on, and the file is not written over
		bool switchesUnread{ false };
		// files of the Modules folder left unread because it holds more than a start reads
		std::size_t leftOut{ 0 };
		// the Modules folder is there but its listing stopped on an error: a mod's file may not have been reached
		bool folderUnread{ false };
	};

	enum class State : std::uint8_t
	{
		kRuns,
		kSwitchedOff,
		kStaysOff
	};

	// one installed mod, as the Installed page shows it
	struct Row
	{
		std::string module{};
		std::string title{};
		Version     dataVersion{};
		State       state{ State::kStaysOff };
		// whether this core has the part at all, and whether two files name it: a row carries a switch only when known and not twice
		bool known{ false };
		bool twice{ false };
	};

	struct Survey
	{
		std::vector<Row> rows{};
		// every line the box, the Problems found page and the log show, the most serious first
		std::vector<std::string> problems{};
		// every part this core has with its switch as the file gives it, in the parts' order: what a save writes back
		std::vector<std::pair<std::string_view, bool>> switches{};
		// Settings.ini could not be read: a switch moved this session is kept for the session and not written
		bool switchesUnread{ false };
		// the Modules folder held a file, or could not be listed: with no row then, files were there and none could be used
		bool filesFound{ false };
	};

	namespace Detail
	{
		// a file's name as a line may show it: a name that is not plain text is not repeated
		[[nodiscard]] inline std::string Shown(std::string_view a_name)
		{
			return ManifestText::Plain(a_name, 80) ? std::format("\"{}\"", a_name) : std::string("A file in the Modules folder");
		}

		[[nodiscard]] inline std::string Named(const Manifest& a_manifest)
		{
			return std::format("{} {}", a_manifest.title, ManifestText::Shown(a_manifest.dataVersion));
		}
	}

	[[nodiscard]] inline Survey Look(const Input& a_in)
	{
		Survey                   out;
		std::vector<Manifest>    found;
		std::vector<std::string> broken;
		for (const auto& file : a_in.files) {
			if (!file.text) {
				broken.push_back(std::format("{} could not be read. It is left out.", Detail::Shown(file.name)));
				continue;
			}
			auto read = ManifestText::Parse(*file.text);
			if (!read.manifest) {
				broken.push_back(std::format("{} is left out: {}.", Detail::Shown(file.name), read.problem));
				continue;
			}
			found.push_back(std::move(*read.manifest));
		}
		// a fixed order whatever order the folder gave: the parts' own order, then by name
		const auto place = [&](const Manifest& a_manifest) {
			const auto* known = ManifestText::Find(a_in.parts, a_manifest.module);
			return known ? static_cast<std::size_t>(known - a_in.parts.data()) : a_in.parts.size();
		};
		std::ranges::stable_sort(found, [&](const Manifest& a_left, const Manifest& a_right) {
			return std::pair(place(a_left), std::string_view(a_left.title)) < std::pair(place(a_right), std::string_view(a_right.title));
		});

		std::vector<std::string_view> ids;
		for (const auto& part : a_in.parts) {
			ids.push_back(part.id);
		}
		const auto file = SwitchesText::Parse(a_in.switches, ids);
		for (const auto& part : a_in.parts) {
			out.switches.emplace_back(part.id, SwitchesText::IsOn(file, part.id));
		}

		// two lighting mods installed light every spell twice, in the plugin and in the files their light frameworks read
		std::vector<const Manifest*> lighting;
		for (const auto& manifest : found) {
			const auto* known = ManifestText::Find(a_in.parts, manifest.module);
			if (known && known->lighting && std::ranges::none_of(lighting, [&](const Manifest* a_m) { return a_m->module == manifest.module; })) {
				lighting.push_back(&manifest);
			}
		}
		const bool clash = lighting.size() > 1;

		const auto twice = ManifestText::Duplicates(found);
		for (const auto& id : twice) {
			out.problems.push_back(std::format(
				"Two files in the Modules folder name the part \"{}\": an old and a new copy of one mod. It stays off until one is removed.", id));
		}
		if (clash) {
			out.problems.push_back(std::format("{} and {} are both installed. Both light the same spells, so neither runs. One of the two has to be removed.",
				lighting[0]->title, lighting[1]->title));
		}
		// an old version's plugin that still runs always counts; its files alone only while no new data of that mod is installed
		// (an update over the old files leaves some behind, and the new data's manifest says which version is in use)
		const auto counts = [&](const Old& a_old) {
			return a_old.runs || std::ranges::find(found, a_old.part, &Manifest::module) == found.end();
		};
		for (const auto& old : a_in.old) {
			if (!counts(old)) {
				continue;
			}
			out.problems.push_back(old.runs ? std::format("A version of {} from before {} is still installed and keeps running (\"{}\"). Its part of {} stays "
														  "off until that version is removed.",
												  old.name, ManifestText::kCore, old.file, ManifestText::kCore) :
											  std::format("Files of {} from a version before {} are installed (\"{}\"). That version does not run with {}: "
														  "it has to be updated or removed.",
												  old.name, ManifestText::kCore, old.file, ManifestText::kCore));
		}
		for (const auto& manifest : found) {
			const auto* known = ManifestText::Find(a_in.parts, manifest.module);
			Row         row{ manifest.module, manifest.title, manifest.dataVersion, State::kRuns, known != nullptr, false };
			const auto  verdict = ManifestText::Judge(manifest, a_in.parts, a_in.core, a_in.features);
			if (std::ranges::find(twice, manifest.module) != twice.end()) {
				row.state = State::kStaysOff;
				row.twice = true;
			} else if (verdict != Verdict::kRuns) {
				row.state = State::kStaysOff;
				out.problems.push_back(ManifestText::Line(manifest, verdict, a_in.parts, a_in.core, a_in.features));
			} else if (std::ranges::any_of(a_in.old, [&](const Old& a_old) { return a_old.runs && a_old.part == manifest.module; }) ||
					   (clash && known && known->lighting)) {
				row.state = State::kStaysOff;
			} else if (!SwitchesText::IsOn(file, manifest.module)) {
				row.state = State::kSwitchedOff;
			}
			out.rows.push_back(std::move(row));
		}
		out.problems.insert(out.problems.end(), std::make_move_iterator(broken.begin()), std::make_move_iterator(broken.end()));
		out.filesFound = !a_in.files.empty() || a_in.folderUnread || a_in.leftOut > 0;
		if (a_in.folderUnread) {
			out.problems.emplace_back(
				"The Modules folder (SKSE\\Plugins\\IlluminatedCore\\Modules) could not be read to its end, so an installed mod may "
				"be missing here. Check that the folder can be opened, then start the game again.");
		}
		if (a_in.leftOut > 0) {
			out.problems.push_back(std::format("The Modules folder holds more files than are read at a start: {} left unread.", a_in.leftOut));
		}
		if (a_in.switchesUnread) {
			out.switchesUnread = true;
			out.problems.emplace_back("Settings.ini could not be read, so every switch counts as on. The file is left as it is until it can be read.");
		}
		for (const auto& problem : file.problems) {
			out.problems.push_back(std::format("Settings.ini, {}.", problem));
		}
		return out;
	}

	// the survey kept when the look itself failed: no part runs, and one line says so wherever a survey is shown
	[[nodiscard]] inline Survey Failed()
	{
		Survey out;
		out.filesFound = true;
		out.problems.emplace_back(
			"What is installed could not be worked out when the game started, so no mod's part runs. IlluminatedCore.log names the "
			"error. Start the game again.");
		return out;
	}

	// the ids of the parts that run this session, in the parts' order
	[[nodiscard]] inline std::vector<std::string_view> Awake(const Survey& a_survey, std::span<const Known> a_parts)
	{
		std::vector<std::string_view> out;
		for (const auto& part : a_parts) {
			if (std::ranges::any_of(a_survey.rows, [&](const Row& a_row) { return a_row.state == State::kRuns && a_row.module == part.id; })) {
				out.push_back(part.id);
			}
		}
		return out;
	}

	// a page's or section's name as the menu framework takes it: a '/' parts two names of a path there, so one inside a name (a
	// translation may hold one) is marked with a backslash, the framework's own way to keep it part of the name
	[[nodiscard]] inline std::string MenuName(std::string_view a_name)
	{
		std::string out;
		out.reserve(a_name.size() + 2);
		for (const char c : a_name) {
			if (c == '/') {
				out += '\\';
			}
			out += c;
		}
		return out;
	}

	// the lines of a box may hold this many characters together: the game's message box has no scroll bar
	inline constexpr std::size_t kBoxRoom = 600;

	// the one box of a start: the first lines in full while they fit (the first always, a_most at most), the rest counted. Empty
	// when there is nothing to say
	[[nodiscard]] inline std::string BoxText(const Survey& a_survey, std::size_t a_most = 5)
	{
		if (a_survey.problems.empty()) {
			return {};
		}
		std::string out = std::format("{}\n", ManifestText::kCore);
		std::size_t shown = 0;
		std::size_t used = 0;
		while (shown < std::min(a_most, a_survey.problems.size()) && (shown == 0 || used + a_survey.problems[shown].size() <= kBoxRoom)) {
			used += a_survey.problems[shown].size();
			out += std::format("\n- {}", a_survey.problems[shown++]);
		}
		if (const auto rest = a_survey.problems.size() - shown; rest > 0) {
			out += std::format("\n\nAnd {} more.", rest);
		}
		out += "\n\nThe whole list is on the Problems found page of the menu and in IlluminatedCore.log.";
		return out;
	}
}
