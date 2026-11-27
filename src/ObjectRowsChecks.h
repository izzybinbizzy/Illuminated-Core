// Illuminated Core Framework: the off-game checks of ObjectRows.h (the object-light library's data and choices)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.

#pragma once

#include "ObjectRows.h"

#include <cmath>
#include <string>
#include <vector>

namespace ObjectRowsChecks
{
	using Check = void (*)(bool a_ok, const char* a_what, int a_line);

	// the checks one Run makes (raise it when checks are added)
	inline constexpr int kCount = 89;

#define CORE_CHECK(...) a_check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

	inline void Run(Check a_check)
	{
		using namespace ObjectRows;
		using L = Lighting;

		// a whole file: a BOM, Windows line ends, comments, two blocks, one light per lighting where it has its own
		const auto good = Read(
			"\xEF\xBB\xBF"
			"format=1\r\norder=5\r\n# a comment\r\n; another\r\n"
			"[object]\r\nname=Dwemer lamp\r\nmodel=Meshes/Dwemer/Lamp01.NIF\r\ncolor=255,200,140\r\ncolor.enb=255,190,120\r\n"
			"radius=220\r\nradius.cs=300\r\ncutoff.cs=0.5\r\nsize.cs=1.5\r\nfade=1.2\r\nflags=PortalStrict|linear\r\nflags.cs=InverseSquare\r\n"
			"point=0,0,24\r\npoint=10, -4, 30\r\non=cs|ENB\r\n"
			"[object]\r\nform=0x1A2B~Some Mod.esp\r\nform=Other.esm|0xFF\r\nform=MyLamp_01\r\ncolor=10,20,30\r\npulse=fire\r\npulsedepth=0.5\r\n"
			"flags=RandomAnimStart\r\nlight=0x55~Some Mod.esp\r\n");
		CORE_CHECK(good.format == 1 && good.order == 5 && good.rows.size() == 2 && good.faults.empty());
		// the rows below are read by place: a short list stops the set here, its count then names the cut (RunSet)
		if (good.rows.size() < 2) {
			return;
		}
		const auto& lamp = good.rows[0];
		CORE_CHECK(lamp.name == "Dwemer lamp" && lamp.models == std::vector<std::string>{ "dwemer\\lamp01.nif" } && lamp.line == 5);
		CORE_CHECK(lamp.points.size() == 2 && lamp.points[1].x == 10.0f && lamp.points[1].y == -4.0f && lamp.points[1].z == 30.0f);
		CORE_CHECK(lamp.on[0] && lamp.on[1] && !lamp.on[2]);
		const auto& bugs = good.rows[1];
		CORE_CHECK(bugs.forms.size() == 3 && bugs.forms[0] == FormText{ "some mod.esp", 0x1A2B, {} } && bugs.forms[1] == FormText{ "other.esm", 0xFF, {} } &&
				   bugs.forms[2] == FormText{ {}, 0, "mylamp_01" });
		CORE_CHECK(bugs.pulse == Pulse::Kind::kFire && bugs.pulseDepth == 0.5f && bugs.points.size() == 1 && bugs.points[0].z == 0.0f);
		CORE_CHECK(bugs.on[0] && bugs.on[1] && bugs.on[2]);

		// each lighting gets its own value where the row has one, else the plain one (his 938)
		const auto cs = LookOn(lamp, L::kCommunityShaders);
		const auto enb = LookOn(lamp, L::kENB);
		CORE_CHECK(cs && cs->color == Rgb{ 255, 200, 140 } && cs->radius == 300.0f && cs->cutoff == 0.5f && cs->size == 1.5f && cs->fade == 1.2f);
		CORE_CHECK(cs && cs->flags == (1u << 2));
		CORE_CHECK(enb && enb->color == Rgb{ 255, 190, 120 } && enb->radius == 220.0f && enb->cutoff == 0.0f && enb->size == 1.0f);
		CORE_CHECK(enb && enb->flags == ((1u << 0) | (1u << 1)));
		// a lighting the row is not on gets no light at all
		CORE_CHECK(!LookOn(lamp, L::kVanilla));
		// a value the row never names keeps the library's own
		const auto plain = LookOn(bugs, L::kVanilla);
		CORE_CHECK(plain && plain->color == Rgb{ 10, 20, 30 } && plain->radius == 256.0f && plain->fade == 1.0f && plain->flags == kRandomAnimStart);
		// a row with only one lighting's color shows only there
		const auto csOnly = Read("format=1\n[object]\nmodel=a.nif\ncolor.cs=1,2,3\n");
		CORE_CHECK(csOnly.rows.size() == 1 && LookOn(csOnly.rows[0], L::kCommunityShaders) && !LookOn(csOnly.rows[0], L::kENB) &&
				   !LookOn(csOnly.rows[0], L::kVanilla));

		// the format comes first, or nothing of the file is read
		const auto noFormat = Read("[object]\nmodel=a.nif\ncolor=1,2,3\n");
		CORE_CHECK(noFormat.rows.empty() && noFormat.faults.size() == 1 && noFormat.faults[0].text.find("no format line") != std::string::npos);
		const auto later = Read("format=2\n[object]\nmodel=a.nif\ncolor=1,2,3\n");
		CORE_CHECK(later.rows.empty() && later.faults.size() == 1 && later.faults[0].text.find("format is 2") != std::string::npos);
		const auto formatLate = Read("[object]\nmodel=a.nif\ncolor=1,2,3\nformat=1\n");
		CORE_CHECK(formatLate.rows.empty() && !formatLate.faults.empty());
		const auto empty = Read("");
		CORE_CHECK(empty.rows.empty() && empty.faults.size() == 1);
		const auto onlyFormat = Read("format=1\n");
		CORE_CHECK(onlyFormat.rows.empty() && onlyFormat.faults.empty());

		// a bad line leaves its own block out, named by line, and the next block is still read
		const auto typo = Read("format=1\n[object]\nmodel=a.nif\ncolour=1,2,3\ncolor=1,2,3\n[object]\nmodel=b.nif\ncolor=4,5,6\n");
		CORE_CHECK(typo.rows.size() == 1 && typo.rows[0].models[0] == "b.nif" && typo.faults.size() == 1);
		CORE_CHECK(typo.faults.size() == 1 && typo.faults[0].line == 2 && typo.faults[0].text.find("line 4: colour is no key (the nearest is color)") != std::string::npos);
		const auto badSuffix = Read("format=1\n[object]\nmodel=a.nif\ncolor.skyrim=1,2,3\ncolor=1,2,3\n");
		CORE_CHECK(badSuffix.rows.empty() && badSuffix.faults.size() == 1 && badSuffix.faults[0].text.find("color.skyrim is no key") != std::string::npos);
		const auto suffixNotPer = Read("format=1\n[object]\nmodel.cs=a.nif\ncolor=1,2,3\n");
		CORE_CHECK(suffixNotPer.rows.empty() && suffixNotPer.faults.size() == 1);
		const auto farKey = Read("format=1\n[object]\nmodel=a.nif\nwhatever=1\ncolor=1,2,3\n");
		CORE_CHECK(farKey.rows.empty() && farKey.faults.size() == 1 && farKey.faults[0].text.find("nearest") == std::string::npos);
		// every value that does not read is named and leaves its block out
		for (const char* line : { "color=256,0,0", "color=1,2", "color=1,2,3,4", "color=1.5,2,3", "color=a,b,c", "radius=-1", "radius=5000", "radius=nan",
				 "cutoff=1.5", "size=-2", "fade=17", "fade=inf", "point=1,2", "pulse=lightning", "pulsedepth=3", "on=cs|skyrim", "form=0x1~nofile",
				 "form=0x1000000~a.esp", "form=bad id", "model=", "light=???" }) {
			const auto bad = Read(std::string("format=1\n[object]\nmodel=a.nif\ncolor=1,2,3\n") + line + "\n");
			a_check(bad.rows.empty() && bad.faults.size() == 1 && bad.faults[0].text.find("line 5") != std::string::npos, line, __LINE__);
		}
		const auto badFlag = Read("format=1\n[object]\nmodel=a.nif\ncolor=1,2,3\nflags=Linear|Shadow\n");
		CORE_CHECK(badFlag.rows.empty() && badFlag.faults.size() == 1 && badFlag.faults[0].text.find("the flag Shadow") != std::string::npos);
		const auto noColor = Read("format=1\n[object]\nmodel=a.nif\n");
		CORE_CHECK(noColor.rows.empty() && noColor.faults.size() == 1 && noColor.faults[0].text.find("no color") != std::string::npos);
		const auto noObject = Read("format=1\n[object]\ncolor=1,2,3\n");
		CORE_CHECK(noObject.rows.empty() && noObject.faults.size() == 1 && noObject.faults[0].text.find("no model and no form") != std::string::npos);
		const auto notALine = Read("format=1\n[object]\nmodel=a.nif\njust words\ncolor=1,2,3\n");
		CORE_CHECK(notALine.rows.empty() && notALine.faults.size() == 1);
		const auto fileKey = Read("format=1\nspeed=3\n[object]\nmodel=a.nif\ncolor=1,2,3\n");
		CORE_CHECK(fileKey.rows.size() == 1 && fileKey.faults.size() == 1 && fileKey.faults[0].line == 2);
		std::string many = "format=1\n[object]\nmodel=a.nif\ncolor=1,2,3\n";
		for (std::size_t i = 0; i <= kMostPoints; ++i) {
			many += "point=0,0," + std::to_string(i) + "\n";
		}
		const auto tooMany = Read(many);
		CORE_CHECK(tooMany.rows.empty() && tooMany.faults.size() == 1);

		// a model as compared everywhere
		CORE_CHECK(NormalModel("Data/Meshes/Clutter/Jar.NIF") == "clutter\\jar.nif" && NormalModel("meshes\\a.nif") == "a.nif" && NormalModel("x/Y.nif") == "x\\y.nif" &&
				   NormalModel("\\Meshes\\A.nif") == "a.nif" && NormalModel("/data/meshes/a.nif") == "a.nif" && NormalModel("\\\\").empty());
		// a note after a value is passed over, as every other reader of ours does; a # or ; with no space before it is part of the
		// value, so a model path that holds one stays whole (the audit's C11)
		const auto noted = Read("format=1\n[object]\nmodel=a.nif\ncolor=255,0,0 ; red\n");
		CORE_CHECK(noted.rows.size() == 1 && noted.faults.empty());
		const auto formNote = Read("format=1 ; the format\n[object]\nmodel=a.nif\ncolor=1,2,3\n");
		CORE_CHECK(formNote.rows.size() == 1 && formNote.faults.empty());
		const auto keptWhole = Read("format=1\n[object]\nmodel=a#b;c.nif\ncolor=1,2,3\n");
		CORE_CHECK(keptWhole.rows.size() == 1 && keptWhole.rows[0].models.size() == 1 && keptWhole.rows[0].models[0] == "a#b;c.nif");
		// the three spellings of a form
		CORE_CHECK(ReadForm(" 0x00ABC~Skyrim.esm ") == FormText{ "skyrim.esm", 0xABC, {} } && ReadForm("Skyrim.ESM|ABC") == FormText{ "skyrim.esm", 0xABC, {} });
		CORE_CHECK(ReadForm("DweLamp01") == FormText{ {}, 0, "dwelamp01" } && !ReadForm("") && !ReadForm("0x1~") && !ReadForm("a.txt|0x1") && !ReadForm("0xZZ~a.esp"));
		// a bare id names no plugin: refused, never taken as an editor ID
		CORE_CHECK(!ReadForm("0x0001A2B3") && !ReadForm("0X12"));
		// a row that shows on no lighting is named, not read as fine
		const auto nowhere = Read("format=1\n[object]\nmodel=a.nif\non=enb\ncolor.cs=1,2,3\n");
		CORE_CHECK(nowhere.rows.empty() && nowhere.faults.size() == 1 && nowhere.faults[0].text.find("shows on no lighting") != std::string::npos);
		// the light record a row names is kept for the game side, apart from the object it lights
		CORE_CHECK(bugs.light == FormText{ "some mod.esp", 0x55, {} } && !lamp.light);
		// format and order are whole numbers written as such
		const auto almost = Read("format=1.00000001\n[object]\nmodel=a.nif\ncolor=1,2,3\n");
		const auto halfOrder = Read("format=1\norder=2.5\n[object]\nmodel=a.nif\ncolor=1,2,3\n");
		CORE_CHECK(almost.rows.empty() && !almost.faults.empty() && halfOrder.rows.size() == 1 && halfOrder.order == 0 && halfOrder.faults.size() == 1);
		// ... and a point followed by zeros still names the whole number; a point alone does not
		const auto pointZero = Read("format=1.0\norder=10.00\n[object]\nmodel=a.nif\ncolor=1,2,3\n");
		const auto pointAlone = Read("format=1.\n[object]\nmodel=a.nif\ncolor=1,2,3\n");
		CORE_CHECK(pointZero.rows.size() == 1 && pointZero.faults.empty() && pointZero.format == 1 && pointZero.order == 10 && pointAlone.rows.empty());
		// a file of another format with no block is left out too
		const auto lonely = Read("format=2\n");
		CORE_CHECK(lonely.rows.empty() && lonely.faults.size() == 1 && lonely.faults[0].text.find("format is 2") != std::string::npos);
		// a folder alone names no model
		const auto folder = Read("format=1\n[object]\nmodel=Meshes/\ncolor=1,2,3\n");
		CORE_CHECK(folder.rows.empty() && folder.faults.size() == 1);
		// a block head with spaces or a note after it is still a block head
		const auto heads = Read("format=1\n[ Object ] ; lamps\nmodel=a.nif\ncolor=1,2,3\n[object]# jars\nmodel=b.nif\ncolor=1,2,3\n");
		CORE_CHECK(heads.rows.size() == 2 && heads.faults.empty());

		// the index: an object led to its rows; several rows per object are several lights
		const auto two = Read("format=1\n[object]\nmodel=a.nif\ncolor=1,1,1\n[object]\nmodel=A.NIF\ncolor=2,2,2\n[object]\nmodel=b.nif\ncolor=3,3,3\n");
		const auto over = Read("format=1\norder=2\n[object]\nmodel=a.nif\ncolor=9,9,9\n");
		const auto under = Read("format=1\norder=-1\n[object]\nmodel=b.nif\ncolor=8,8,8\n");
		const auto forms = Read("format=1\n[object]\nform=0x1~x.esp\nmodel=c.nif\ncolor=7,7,7\n");
		Index      index({ { "zz.txt", over }, { "a.txt", two }, { "b.txt", under }, { "c.txt", forms } });
		const auto a = index.Of(std::nullopt, "", "Meshes\\a.nif");
		const auto b = index.Of(std::nullopt, "", "b.nif");
		// the higher order wins a's object whatever the names; two's two rows for it are dropped, never added to
		CORE_CHECK(a.size() == 1 && index.RowAt(a[0]).color.plain == Rgb{ 9, 9, 9 } && index.FileOf(a[0]) == "zz.txt");
		// b's object: two (order 0) beats under (order -1) - two's row stands
		CORE_CHECK(b.size() == 1 && index.RowAt(b[0]).color.plain == Rgb{ 3, 3, 3 } && index.FileOf(b[0]) == "a.txt");
		// one file naming an object by form and by model: its form row answers; a higher-order file's model row beats another file's form
		// row (zz.txt, order 2, over c.txt, order 0); a form no row names falls to the model
		const auto byForm = index.Of(FormText{ "x.esp", 1, {} }, "", "c.nif");
		const auto higher = index.Of(FormText{ "x.esp", 1, {} }, "", "a.nif");
		CORE_CHECK(byForm.size() == 1 && index.RowAt(byForm[0]).color.plain == Rgb{ 7, 7, 7 } && higher.size() == 1 && index.RowAt(higher[0]).color.plain == Rgb{ 9, 9, 9 });
		CORE_CHECK(index.Of(FormText{ "x.esp", 2, {} }, "", "a.nif").size() == 1 && index.Of(std::nullopt, "", "none.nif").empty());
		CORE_CHECK(index.Of(std::nullopt, "", "c.nif").size() == 1 && index.Models() == 3 && index.Forms() == 1);
		// the same order: the later file name wins
		const auto first = Read("format=1\n[object]\nmodel=a.nif\ncolor=1,0,0\n");
		const auto second = Read("format=1\n[object]\nmodel=a.nif\ncolor=0,1,0\n");
		Index      tie({ { "b.txt", second }, { "a.txt", first } });
		const auto tied = tie.Of(std::nullopt, "", "a.nif");
		CORE_CHECK(tied.size() == 1 && tie.FileOf(tied[0]) == "b.txt");
		// several rows of one file for one object stay together
		Index one({ { "a.txt", two } });
		CORE_CHECK(one.Of(std::nullopt, "", "a.nif").size() == 2);
		// a higher-order file that names the object by model wins over a lower one that named it by form; one file naming it both
		// ways keeps its form rows first
		const auto byBoth = Read("format=1\n[object]\nform=0x12~lamps.esp\ncolor=1,1,1\n[object]\nmodel=lamp.nif\ncolor=2,2,2\n");
		const auto patch = Read("format=1\norder=5\n[object]\nmodel=lamp.nif\ncolor=5,5,5\n");
		Index      ranked({ { "a.txt", byBoth }, { "b.txt", patch } });
		const auto won = ranked.Of(FormText{ "lamps.esp", 0x12, {} }, "", "lamp.nif");
		Index      alone({ { "a.txt", byBoth } });
		const auto mine = alone.Of(FormText{ "lamps.esp", 0x12, {} }, "", "lamp.nif");
		CORE_CHECK(won.size() == 1 && ranked.RowAt(won[0]).color.plain == Rgb{ 5, 5, 5 } && mine.size() == 1 && alone.RowAt(mine[0]).color.plain == Rgb{ 1, 1, 1 });

		// one object named by plugin and id in one file and by editor ID in another: the higher order wins it, whichever way asked
		const auto byId = Read("format=1\n[object]\nform=0x12~lamps.esp\ncolor=1,1,1\n");
		const auto byName = Read("format=1\norder=5\n[object]\nform=MyLamp\ncolor=5,5,5\n");
		Index      spelled({ { "a.txt", byId }, { "b.txt", byName } });
		const auto asked = spelled.Of(FormText{ "lamps.esp", 0x12, {} }, "MyLamp", "lamp.nif");
		CORE_CHECK(asked.size() == 1 && spelled.RowAt(asked[0]).color.plain == Rgb{ 5, 5, 5 });
		// a plugin asked by its file's own spelling still finds the rows that named it
		const auto cased = alone.Of(FormText{ "Lamps.ESP", 0x12, {} }, "", "");
		CORE_CHECK(cased.size() == 1 && alone.RowAt(cased[0]).color.plain == Rgb{ 1, 1, 1 });
		// a row naming one object twice is one light
		const auto twiceNamed = Read("format=1\n[object]\nmodel=a.nif\nmodel=Meshes/A.NIF\ncolor=1,2,3\n");
		Index      once({ { "a.txt", twiceNamed } });
		CORE_CHECK(once.Of(std::nullopt, "", "a.nif").size() == 1);

		// who gets a light: the nearest up to the cap
		const std::vector<Candidate> near5{ { 1, 100.0f, false }, { 2, 50.0f, false }, { 3, 400.0f, false }, { 4, 10.0f, false }, { 5, 200.0f, false } };
		CORE_CHECK(Choose(near5, 2, false) == std::vector<std::uint32_t>{ 2, 4 });
		CORE_CHECK(Choose(near5, 10, false) == std::vector<std::uint32_t>{ 1, 2, 3, 4, 5 } && Choose(near5, 0, false).empty());
		CORE_CHECK(Choose(near5, 0, true).size() == 5);
		// one already lit keeps its light until another is clearly nearer: a fifth nearer counts
		const std::vector<Candidate> edge{ { 1, 100.0f, true }, { 2, 90.0f, false } };
		CORE_CHECK(Choose(edge, 1, false) == std::vector<std::uint32_t>{ 1 });
		const std::vector<Candidate> passed{ { 1, 100.0f, true }, { 2, 60.0f, false } };
		CORE_CHECK(Choose(passed, 1, false) == std::vector<std::uint32_t>{ 2 });
		// a broken distance is no candidate; a tie goes to the lower id, the same every time
		const std::vector<Candidate> broken{ { 1, std::nanf(""), false }, { 2, -1.0f, false }, { 3, 5.0f, false }, { 4, 5.0f, false } };
		CORE_CHECK(Choose(broken, 1, false) == std::vector<std::uint32_t>{ 3 } && Choose(broken, 0, true) == std::vector<std::uint32_t>{ 3, 4 });
		// an object handed in twice takes one place, at its nearest
		const std::vector<Candidate> twice{ { 1, 50.0f, false }, { 1, 5.0f, false }, { 2, 20.0f, false }, { 3, 30.0f, false } };
		CORE_CHECK(Choose(twice, 2, false) == std::vector<std::uint32_t>{ 1, 2 } && Choose(twice, 0, true) == std::vector<std::uint32_t>{ 1, 2, 3 });

		// the pulse handed to the game's dimmer: fire as its 12-second loop (his 927)
		const auto fire = PulseOf(bugs, L::kENB, 0x1234, 0.0);
		CORE_CHECK(fire && fire->period == static_cast<float>(DimmerKeys::kNoiseLoop) && fire->keys.size() == 361);
		// RandomAnimStart: each placement its own place in the shape; without it every copy pulses together
		const auto fireOther = PulseOf(bugs, L::kENB, 0x5678, 0.0);
		bool       apart = false;
		for (int i = 0; fire && fireOther && i < 120; ++i) {
			apart = apart || std::abs(DimmerKeys::At(*fire, i * 0.1) - DimmerKeys::At(*fireOther, i * 0.1)) > 0.01f;
		}
		CORE_CHECK(apart);
		auto together = bugs;
		together.flags = {};
		const auto t1 = PulseOf(together, L::kENB, 0x1234, 0.0);
		const auto t2 = PulseOf(together, L::kENB, 0x5678, 0.0);
		CORE_CHECK(t1 && t2 && t1->keys.size() == t2->keys.size() && DimmerKeys::At(*t1, 3.3) == DimmerKeys::At(*t2, 3.3));
		// a steady row hands nothing to the dimmer; a repeating shape bakes its own loop
		CORE_CHECK(!PulseOf(lamp, L::kCommunityShaders, 1, 0.0));
		auto healing = lamp;
		healing.pulse = Pulse::Kind::kHealing;
		const auto heal = PulseOf(healing, L::kCommunityShaders, 1, 0.0);
		CORE_CHECK(heal && heal->period == static_cast<float>(DimmerKeys::Period(Pulse::Kind::kHealing)));
		auto still = bugs;
		still.pulseDepth = 0.0f;
		CORE_CHECK(!PulseOf(still, L::kENB, 1, 0.0));
	}

#undef CORE_CHECK
}
