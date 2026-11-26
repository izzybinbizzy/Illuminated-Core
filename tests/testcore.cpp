// Illuminated - Core: the off-game tests (no game needed). Built and run under clang's address and undefined-behavior checkers.
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// The kernel's own game-free code, then the check sets the shared files ship (each ran once per carrying plugin before).

#include "ManifestText.h"
#include "SurveyText.h"
#include "SwitchesText.h"

#include "AdvancedSettingsChecks.h"
#include "EnbShaderLightsChecks.h"
#include "FloorLightChecks.h"
#include "NoteRingChecks.h"
#include "Po3TweaksChecks.h"
#include "RowParkChecks.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	int gChecks = 0;
	int gFails = 0;

	void Check(bool a_ok, const char* a_what, int a_line)
	{
		++gChecks;
		if (!a_ok) {
			++gFails;
			std::printf("FAIL line %d: %s\n", a_line, a_what);
		}
	}

// variadic: a check may hold a braced list with commas of its own
#define CHECK(...) Check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

	using namespace ManifestText;

	constexpr std::array<Known, 2>            kKnown{ { { "illuminated", Version{ { 4, 0, 0 } }, true }, { "wards", Version{ { 5, 0, 0 } }, false } } };
	constexpr std::array<std::string_view, 2> kFeatures{ "record-slots", "fading" };
	constexpr Version                         kCoreNow{ { 1, 2, 0 } };

	Verdict JudgeText(const char* a_text)
	{
		const auto read = Parse(a_text);
		return read.manifest ? Judge(*read.manifest, kKnown, kCoreNow, kFeatures) : Verdict::kUnknownModule;
	}

	void Versions()
	{
		CHECK(ParseVersion("1") == Version{ { 1, 0, 0 } });
		CHECK(ParseVersion("1.2") == Version{ { 1, 2, 0 } });
		CHECK(ParseVersion("1.2.3") == Version{ { 1, 2, 3 } });
		CHECK(ParseVersion("10.0.1") > ParseVersion("9.99.99"));
		CHECK(ParseVersion("1.2.0") == ParseVersion("1.2"));
		for (const char* bad : { "", ".", "1.", ".1", "1..2", "1.2.3.4", "a", "1.x", "-1", "1.2 ", " 1", "1e3", "4294967296000", "0x10" }) {
			CHECK(!ParseVersion(bad).has_value());
		}
		CHECK(Shown(Version{ { 1, 2, 3 } }) == "1.2.3");
	}

	void Reading()
	{
		const auto good = Parse(R"({"format":1,"module":"illuminated","title":"Illuminated - Spell Lighting NG","dataVersion":"4.0",)"
								R"("needsCore":"1.0.0","needsCoreMax":"1.99.99","needs":["record-slots","fading"],"notice":"A plain line."})");
		CHECK(good.manifest.has_value() && good.problem.empty());
		if (good.manifest) {
			const auto& m = *good.manifest;
			CHECK(m.module == "illuminated" && m.title == "Illuminated - Spell Lighting NG");
			CHECK(m.dataVersion == Version{ { 4, 0, 0 } } && m.needsCore == Version{ { 1, 0, 0 } });
			CHECK(m.needsCoreMax == Version{ { 1, 99, 99 } } && m.needs.size() == 2 && m.notice == "A plain line.");
		}
		// a byte order mark is passed; a title left out or not plain falls back to the module's id; a notice not plain is dropped
		const auto bare = Parse("\xEF\xBB\xBF{\"format\":1,\"module\":\"wards\",\"dataVersion\":\"5\",\"needsCore\":\"1\",\"title\":\"two\\nlines\",\"notice\":\"a\\tb\"}");
		CHECK(bare.manifest.has_value());
		if (bare.manifest) {
			CHECK(bare.manifest->title == "wards" && bare.manifest->notice.empty() && !bare.manifest->needsCoreMax.has_value());
		}
		// every broken file gives a sentence and no manifest, and nothing throws
		for (const char* bad : { "", "{", "[]", "7", "null", R"({"format":2,"module":"wards","dataVersion":"5","needsCore":"1"})",
				 R"({"format":"1","module":"wards","dataVersion":"5","needsCore":"1"})", R"({"format":1,"dataVersion":"5","needsCore":"1"})",
				 R"({"format":1,"module":7,"dataVersion":"5","needsCore":"1"})", R"({"format":1,"module":"wards","dataVersion":5,"needsCore":"1"})",
				 R"({"format":1,"module":"wards","dataVersion":"5","needsCore":"new"})",
				 R"({"format":1,"module":"wards","dataVersion":"5","needsCore":"1","needsCoreMax":3})",
				 R"({"format":1,"module":"wards","dataVersion":"5","needsCore":"1","needs":"fading"})",
				 R"({"format":1,"module":"wards","dataVersion":"5","needsCore":"1","needs":[1]})" }) {
			const auto read = Parse(bad);
			CHECK(!read.manifest.has_value() && !read.problem.empty() && Plain(read.problem, 200));
		}
		// a module id too long for a box, and a needs list too long, are refused
		CHECK(!Parse(R"({"format":1,"module":")" + std::string(40, 'm') + R"(","dataVersion":"5","needsCore":"1"})").manifest.has_value());
		std::string many = R"({"format":1,"module":"wards","dataVersion":"5","needsCore":"1","needs":[)";
		for (int i = 0; i < 40; ++i) {
			many += i ? ",\"n\"" : "\"n\"";
		}
		CHECK(!Parse(many + "]}").manifest.has_value());
		// deep nesting and binary bytes are only a broken file
		CHECK(!Parse(std::string(5000, '[')).manifest.has_value());
		CHECK(!Parse(std::string_view("\x00\xFF\xFE{", 4)).manifest.has_value());
	}

	void Judging()
	{
		CHECK(JudgeText(R"({"format":1,"module":"illuminated","dataVersion":"4.0","needsCore":"1.0"})") == Verdict::kRuns);
		CHECK(JudgeText(R"({"format":1,"module":"illuminated","dataVersion":"4.1","needsCore":"1.2","needsCoreMax":"1.2"})") == Verdict::kRuns);
		CHECK(JudgeText(R"({"format":1,"module":"arcane","dataVersion":"1","needsCore":"1"})") == Verdict::kUnknownModule);
		CHECK(JudgeText(R"({"format":1,"module":"illuminated","dataVersion":"3.9","needsCore":"1"})") == Verdict::kDataTooOld);
		CHECK(JudgeText(R"({"format":1,"module":"illuminated","dataVersion":"4","needsCore":"1.2.1"})") == Verdict::kCoreTooOld);
		CHECK(JudgeText(R"({"format":1,"module":"illuminated","dataVersion":"4","needsCore":"1","needsCoreMax":"1.1.9"})") == Verdict::kCoreTooNew);
		CHECK(JudgeText(R"({"format":1,"module":"illuminated","dataVersion":"4","needsCore":"1","needs":["fading","object-lights"]})") ==
			  Verdict::kNeedsMissing);
		// the older data wins over every later test: the lowest data version is the no-in-between rule
		CHECK(JudgeText(R"({"format":1,"module":"wards","dataVersion":"4.3","needsCore":"9"})") == Verdict::kDataTooOld);
	}

	void Lines()
	{
		const auto line = [](const char* a_text) {
			const auto read = Parse(a_text);
			return read.manifest ? Line(*read.manifest, Judge(*read.manifest, kKnown, kCoreNow, kFeatures), kKnown, kCoreNow, kFeatures) : std::string("?");
		};
		CHECK(line(R"({"format":1,"module":"illuminated","dataVersion":"4.0","needsCore":"1.0"})").empty());
		CHECK(line(R"({"format":1,"module":"illuminated","title":"Illuminated","dataVersion":"3.9","needsCore":"1"})") ==
			  "Illuminated 3.9.0 is older than this Illuminated - Core runs (4.0.0 or newer). It stays off until it is updated.");
		CHECK(line(R"({"format":1,"module":"illuminated","title":"Illuminated","dataVersion":"4","needsCore":"1.3"})") ==
			  "Illuminated 4.0.0 needs Illuminated - Core 1.3.0 or newer; this is 1.2.0. It stays off until Illuminated - Core is updated.");
		// the data mod's own line is used when the core is too old, and only then
		CHECK(line(R"({"format":1,"module":"illuminated","dataVersion":"4","needsCore":"1.3","notice":"Its own line."})") == "Its own line.");
		CHECK(line(R"({"format":1,"module":"illuminated","title":"Illuminated","dataVersion":"3","needsCore":"1","notice":"Its own line."})") !=
			  "Its own line.");
		CHECK(line(R"({"format":1,"module":"illuminated","title":"Illuminated","dataVersion":"4","needsCore":"1","needsCoreMax":"1.1"})") ==
			  "Illuminated 4.0.0 was made for Illuminated - Core up to 1.1.0; this is 1.2.0. It stays off until it is updated.");
		CHECK(line(R"({"format":1,"module":"illuminated","title":"Illuminated","dataVersion":"4","needsCore":"1","needs":["object-lights"]})") ==
			  "Illuminated 4.0.0 needs \"object-lights\", which this Illuminated - Core does not have. It stays off.");
		CHECK(line(R"({"format":1,"module":"arcane","dataVersion":"1","needsCore":"1"})") ==
			  "arcane 1.0.0 names a part (\"arcane\") this Illuminated - Core does not have. It stays off.");
		// no line carries a personal word
		for (const char* text : { R"({"format":1,"module":"illuminated","dataVersion":"3","needsCore":"1"})", R"({"format":1,"module":"x","dataVersion":"1","needsCore":"1"})",
				 R"({"format":1,"module":"illuminated","dataVersion":"4","needsCore":"2"})" }) {
			const auto shown = " " + line(text) + " ";
			for (const char* word : { " you ", " your ", " I ", " my ", " he ", " she ", " they " }) {
				CHECK(shown.find(word) == std::string::npos);
			}
		}
	}

	void Switches()
	{
		using namespace SwitchesText;
		constexpr std::array<std::string_view, 3> kModules{ "illuminated", "wards", "creatures" };
		CHECK(OnOff("on") == true && OnOff("TRUE") == true && OnOff("Yes") == true && OnOff("1") == true);
		CHECK(OnOff("off") == false && OnOff("False") == false && OnOff("NO") == false && OnOff("0") == false);
		CHECK(!OnOff("").has_value() && !OnOff("maybe").has_value() && !OnOff("onn").has_value());
		// the way people edit a file: a mark, any case, spaces, notes; another section's lines are not ours
		const auto read = Parse("\xEF\xBB\xBF; a note\n[Other]\nwards = off\n[ modules ]\r\n  Illuminated = OFF ; mine\nWARDS=on\n# done\n", kModules);
		CHECK(read.problems.empty() && read.switches.size() == 2);
		CHECK(!IsOn(read, "illuminated") && IsOn(read, "wards") && IsOn(read, "creatures"));
		// nothing read: every part is on
		CHECK(IsOn(Parse("", kModules), "wards") && Parse("", kModules).problems.empty());
		// each line that cannot be used is named with its number, and the rest is still read
		const auto bad = Parse("[Modules]\nwards\narcane = on\ncreatures = perhaps\nwards = off\nwards = on\n", kModules);
		CHECK(bad.problems.size() == 4 && bad.switches.size() == 1 && !IsOn(bad, "wards") && IsOn(bad, "creatures"));
		CHECK(bad.problems[0] == "line 2: no = on this line" && bad.problems[1] == "line 3: no part of this name");
		CHECK(bad.problems[2] == "line 4: creatures is neither on nor off; it stays on");
		CHECK(bad.problems[3] == "line 6: wards is written twice; the first stays");
		// what is written reads back the same
		constexpr std::array<std::pair<std::string_view, bool>, 3> kSaved{ { { "illuminated", true }, { "wards", false }, { "creatures", true } } };
		const auto                                                 text = Text(kSaved);
		const auto                                                 back = Parse(text, kModules);
		CHECK(back.problems.empty() && back.switches.size() == 3 && IsOn(back, "illuminated") && !IsOn(back, "wards") && IsOn(back, "creatures"));
		CHECK(text.find('\t') == std::string::npos && text.find("you") == std::string::npos);
	}

	void Surveys()
	{
		using namespace SurveyText;
		constexpr std::array<Known, 4>            kParts{ { { "illuminated", Version{ { 4, 0, 0 } }, true }, { "relight", Version{ { 5, 0, 0 } }, true },
			{ "wards", Version{ { 5, 0, 0 } }, false }, { "creatures", Version{ { 2, 0, 0 } }, false } } };
		constexpr std::array<std::string_view, 1> kFeatures{ "fading" };
		constexpr Version                         kCoreNow{ { 1, 0, 0 } };
		const auto                                manifest = [](std::string_view a_module, std::string_view a_title, std::string_view a_data) {
			return std::format(R"({{"format":1,"module":"{}","title":"{}","dataVersion":"{}","needsCore":"1.0.0"}})", a_module, a_title, a_data);
		};
		const auto look = [&](std::span<const File> a_files, std::string_view a_switches = {}, std::span<const Old> a_old = {}) {
			return Look({ a_files, a_switches, kParts, kCoreNow, kFeatures, a_old });
		};
		const auto state = [](const Survey& a_survey, std::string_view a_module) {
			const auto it = std::ranges::find(a_survey.rows, a_module, &Row::module);
			return it == a_survey.rows.end() ? State::kStaysOff : it->state;
		};

		// nothing installed: no row, no line, no box, every part's switch on
		const auto none = look({});
		CHECK(none.rows.empty() && none.problems.empty() && BoxText(none).empty() && Awake(none, kParts).empty());
		CHECK(none.switches.size() == 4 && std::ranges::all_of(none.switches, [](const auto& a_s) { return a_s.second; }));

		// two mods, whatever order the folder gives them: the parts' order, both run
		const std::vector<File> two{ { "wards.json", manifest("wards", "Dynamic Wards", "5.0") },
			{ "illuminated.json", manifest("illuminated", "Illuminated - Spell Lighting NG", "4.0") } };
		const auto              both = look(two);
		CHECK(both.rows.size() == 2 && both.rows[0].module == "illuminated" && both.rows[1].module == "wards");
		CHECK(both.problems.empty() && BoxText(both).empty());
		CHECK(Awake(both, kParts) == std::vector<std::string_view>{ "illuminated", "wards" });
		CHECK(both.rows[0].known && both.rows[0].title == "Illuminated - Spell Lighting NG" && both.rows[0].dataVersion == Version{ { 4, 0, 0 } });

		// a part switched off in the file is no problem: it is off, and its switch reads off
		const auto off = look(two, "[Modules]\nwards = off\n");
		CHECK(state(off, "wards") == State::kSwitchedOff && state(off, "illuminated") == State::kRuns && off.problems.empty());
		CHECK(Awake(off, kParts) == std::vector<std::string_view>{ "illuminated" });
		CHECK(off.switches[2] == std::pair<std::string_view, bool>{ "wards", false } && off.switches[0].second);

		// the two lighting mods together: neither runs, one line names both, the other parts run
		const std::vector<File> lit{ { "relight.json", manifest("relight", "RELight - Spell Lighting NG", "5.0") },
			{ "illuminated.json", manifest("illuminated", "Illuminated - Spell Lighting NG", "4.0") },
			{ "wards.json", manifest("wards", "Dynamic Wards", "5.0") } };
		const auto              clash = look(lit);
		CHECK(state(clash, "illuminated") == State::kStaysOff && state(clash, "relight") == State::kStaysOff && state(clash, "wards") == State::kRuns);
		CHECK(clash.problems.size() == 1 && clash.problems[0] ==
												"Illuminated - Spell Lighting NG and RELight - Spell Lighting NG are both installed. Both light the same "
												"spells, so neither runs. One of the two has to be removed.");
		// switching one of the two off does not lift it: the files both mods' light frameworks read are still both installed
		CHECK(state(look(lit, "[Modules]\nrelight = off\n"), "illuminated") == State::kStaysOff);

		// one mod installed twice (an old and a new copy): that part stays off, said once
		const std::vector<File> twice{ { "wards.json", manifest("wards", "Dynamic Wards", "5.0") }, { "wards old.json", manifest("wards", "Dynamic Wards", "5.1") },
			{ "creatures.json", manifest("creatures", "Illuminated - Creature Lighting NG", "2.0") } };
		const auto              dup = look(twice);
		CHECK(dup.rows.size() == 3 && state(dup, "wards") == State::kStaysOff && state(dup, "creatures") == State::kRuns);
		CHECK(dup.problems.size() == 1 && dup.problems[0].find("\"wards\"") != std::string::npos);

		// data older than this core runs, a part this core does not have, a broken file, a file that could not be read
		const std::vector<File> bad{ { "wards.json", manifest("wards", "Dynamic Wards", "4.3") }, { "arcane.json", manifest("arcane", "Arcane Overdrive", "1.0") },
			{ "broken.json", "{" }, { "locked.json", std::nullopt }, { std::string("bad\x01name.json"), "[]" },
			{ "creatures.json", manifest("creatures", "Illuminated - Creature Lighting NG", "2.0") } };
		const auto              mixed = look(bad, "[Modules]\ncreatures = perhaps\n");
		CHECK(mixed.rows.size() == 3 && state(mixed, "wards") == State::kStaysOff && state(mixed, "arcane") == State::kStaysOff);
		CHECK(state(mixed, "creatures") == State::kRuns && Awake(mixed, kParts) == std::vector<std::string_view>{ "creatures" });
		CHECK(mixed.problems.size() == 6);
		// a part this core does not have carries no switch
		CHECK(!std::ranges::find(mixed.rows, std::string_view("arcane"), &Row::module)->known);
		const auto has = [&](std::string_view a_text) {
			return std::ranges::any_of(mixed.problems, [&](const std::string& a_line) { return a_line.find(a_text) != std::string::npos; });
		};
		CHECK(has("Dynamic Wards 4.3.0 is older than this Illuminated - Core runs (5.0.0 or newer)"));
		CHECK(has("Arcane Overdrive 1.0.0 names a part (\"arcane\")"));
		CHECK(has("\"broken.json\" is left out: the file is not one { } of settings."));
		CHECK(has("\"locked.json\" could not be read. It is left out."));
		CHECK(has("A file in the Modules folder is left out"));
		CHECK(has("Settings.ini, line 2: creatures is neither on nor off; it stays on."));

		// a version from before the core: its plugin still loaded keeps that part off; its data alone is named
		constexpr std::array<Old, 2> kOld{ { { "wards", "Dynamic Wards", "DynamicWards.dll", true },
			{ "illuminated", "Illuminated - Spell Lighting NG", "Illuminated.ini", false } } };
		// (beside new data of the same mod, old files alone are an update's leftovers: not named, and the part runs)
		const auto old = look(two, {}, kOld);
		CHECK(state(old, "wards") == State::kStaysOff && state(old, "illuminated") == State::kRuns && old.problems.size() == 1);
		CHECK(old.problems[0] ==
			  "A version of Dynamic Wards from before Illuminated - Core is still installed and keeps running (\"DynamicWards.dll\"). "
			  "Its part of Illuminated - Core stays off until that version is removed.");
		// the old versions alone, with no new data: both named
		const auto alone = look({}, {}, kOld);
		CHECK(alone.problems.size() == 2 && alone.rows.empty());
		CHECK(alone.problems[1] ==
			  "Files of Illuminated - Spell Lighting NG from a version before Illuminated - Core are installed (\"Illuminated.ini\"). "
			  "That version does not run with Illuminated - Core: it has to be updated or removed.");

		// the box: the first lines in full, the rest counted, where the whole list is; every line plain and free of personal words
		const auto box = BoxText(mixed, 2);
		CHECK(box.starts_with("Illuminated - Core\n\n- ") && box.find("And 4 more.") != std::string::npos);
		CHECK(box.ends_with("The whole list is on the Problems found page of the menu and in IlluminatedCore.log."));
		CHECK(BoxText(clash).find("And ") == std::string::npos);
		for (const auto* survey : { &clash, &dup, &mixed, &old, &alone }) {
			for (const auto& line : survey->problems) {
				const auto shown = " " + line + " ";
				CHECK(std::ranges::all_of(line, [](char a_c) { return a_c >= ' ' && a_c <= '~'; }) && line.size() <= 320);
				for (const char* word : { " you ", " your ", " I ", " my ", " he ", " she ", " they ", " them " }) {
					CHECK(shown.find(word) == std::string::npos);
				}
			}
		}

		// many files, all hostile: nothing throws, every one is named
		std::vector<File> flood;
		for (int i = 0; i < 300; ++i) {
			flood.push_back({ std::format("f{}.json", i), i % 3 == 0 ? std::optional<std::string>() : std::string(static_cast<std::size_t>(i), '[') });
		}
		CHECK(look(flood).problems.size() == 300 && look(flood).rows.empty());
	}

	void Twice()
	{
		std::vector<Manifest> all(3);
		all[0].module = "illuminated";
		all[1].module = "wards";
		all[2].module = "illuminated";
		const auto twice = Duplicates(all);
		CHECK(twice.size() == 1 && twice[0] == "illuminated");
		all[2].module = "creatures";
		CHECK(Duplicates(all).empty());
		CHECK(Duplicates({}).empty());
	}
}

int main()
{
	Versions();
	Reading();
	Judging();
	Lines();
	Twice();
	Switches();
	Surveys();
	const int own = gChecks;
	RowParkChecks::Run(Check);
	NoteRingChecks::Run(Check);
	EnbShaderLightsChecks::Run(Check);
	FloorLightChecks::Run(Check);
	Po3TweaksChecks::Run(Check);
	AdvancedSettingsChecks::Run(Check);
	const int shared = RowParkChecks::kCount + NoteRingChecks::kCount + EnbShaderLightsChecks::kCount + FloorLightChecks::kCount +
	                   Po3TweaksChecks::kCount + AdvancedSettingsChecks::kCount;
	// each set names a floor: a set that ran fewer checks than it names was cut short
	const int ran = gChecks - own;
	Check(ran >= shared, "the shared check sets ran at least as many checks as they name", __LINE__);
	std::printf("testcore: %d checks, %d failed (%d of the kernel's own, %d from the shared check sets, which name %d)\n", gChecks, gFails, own, ran, shared);
	return gFails ? 1 : 0;
}
