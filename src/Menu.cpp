// Illuminated Core Framework - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// The SKSE Menu Framework section "Illuminated Core Framework": the Installed page (each installed mod, its version, whether its part
// runs, and its switch for the next game start) and the Problems found page (every line the box at the main menu listed).
// Each mod's own pages join this section in a folder of the mod's name when its part is built in.

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
// the build defines it too (xmake.lua)
#	define NOMINMAX
#endif
#include "Kernel.h"

// SKSE Menu Framework's own header (theirs, MIT): its warnings are not ours, and ours are errors (xmake.lua) - under MSVC and
// under clang-cl, which checks every file too
#pragma warning(push, 0)
#if defined(__clang__)
#	pragma clang diagnostic push
#	pragma clang diagnostic ignored "-Weverything"
#endif
#include "SKSEMenuFramework.h"
#if defined(__clang__)
#	pragma clang diagnostic pop
#endif
#pragma warning(pop)
#include "MenuStyle.h"
#include "Translation.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

namespace Core
{
	namespace
	{
		using namespace ImGuiMCP;
		using MenuStyle::Header;
		using MenuStyle::kMuted;
		using Translation::Fill;
		using Translation::T;
		namespace Icon = MenuStyle::Icon;

		// the core's own pages wear Illuminated's candle gold (his checklist 925); each mod's folder will set its own accent at the
		// top of its pages, so the core's is set again at the top of each of its own
		constexpr std::uint32_t kCandleGold = 0xFFC94D;

		// ImGui's pushes paired by scope: a throw between a push and its pop (a translated line allocates) is caught by Guarded
		// outside the page, and would leave the push on ImGui's stack for every frame after
		struct ScopedID
		{
			explicit ScopedID(int a_id) { PushID(a_id); }
			~ScopedID() { PopID(); }
			ScopedID(const ScopedID&) = delete;
			ScopedID& operator=(const ScopedID&) = delete;
		};
		struct ScopedWrap
		{
			ScopedWrap() { PushTextWrapPos(0.0f); }
			~ScopedWrap() { PopTextWrapPos(); }
			ScopedWrap(const ScopedWrap&) = delete;
			ScopedWrap& operator=(const ScopedWrap&) = delete;
		};

		// a part's state this session, in the page's words
		[[nodiscard]] const char* StateWords(SurveyText::State a_state)
		{
			switch (a_state) {
			case SurveyText::State::kRuns:
				return T("runs");
			case SurveyText::State::kSwitchedOff:
				return T("switched off");
			case SurveyText::State::kStaysOff:
			default:
				return T("stays off - the reason is on the Problems found page");
			}
		}

		void RenderInstalled()
		{
			MenuStyle::gTheme = MenuStyle::MakeTheme(kCandleGold);
			MenuStyle::Page page;
			const auto      survey = SurveyNow();
			const auto      switches = SwitchesNow();

			Header(Icon::kList, T("Installed"));
			TextColored(kMuted, "%s", Fill(T("Illuminated Core Framework {}"), ManifestText::Shown(VersionNow())).c_str());
			if (survey->rows.empty()) {
				// nothing installed, or files that could not be used: the second is sent on to the page that says why
				MenuStyle::Note(survey->filesFound ? T("No mod's part runs. What was wrong when the game started is on the Problems found page.") :
													 T("No mod that needs Illuminated Core Framework is installed. Nothing runs."));
				return;
			}
			int index = 0;
			for (const auto& row : survey->rows) {
				const auto name = std::format("{} {}", row.title, ManifestText::Shown(row.dataVersion));
				const auto it = std::ranges::find(switches, std::string_view(row.module), &std::pair<std::string_view, bool>::first);
				// each row its own id: two mods may carry one name
				const ScopedID id{ index++ };
				// a part this core does not have, and one named by two files, carry no switch
				if (!row.known || row.twice || it == switches.end()) {
					MenuStyle::Status(false, name.c_str());
				} else {
					bool on = it->second;
					if (Checkbox(name.c_str(), &on)) {
						static_cast<void>(SetSwitch(row.module, on));
					}
					MenuStyle::Tip(T("Off, this mod's part of Illuminated Core Framework does not run. A change takes hold from the next game start."));
					// what the next start does, only where it differs from this session. With Settings.ini unreadable a moved switch is
					// kept for this session only and never written (Kernel.cpp SetSwitch): the next start does not change, and it says so
					if (on != (row.state != SurveyText::State::kSwitchedOff)) {
						SameLine(0.0f, 12.0f);
						if (survey->switchesUnread) {
							TextColored(MenuStyle::kBad, "%s", T("not saved: Settings.ini could not be read"));
						} else {
							TextColored(kMuted, "%s", on ? T("on from the next game start") : T("off from the next game start"));
						}
					}
				}
				SameLine(0.0f, 12.0f);
				TextColored(row.state == SurveyText::State::kStaysOff ? MenuStyle::kBad : kMuted, "%s", StateWords(row.state));
			}
		}

		void RenderProblems()
		{
			MenuStyle::gTheme = MenuStyle::MakeTheme(kCandleGold);
			MenuStyle::Page page;
			const auto      survey = SurveyNow();

			Header(Icon::kList, T("Problems found"));
			if (survey->problems.empty()) {
				MenuStyle::Note(T("No problem was found when the game started."));
				return;
			}
			MenuStyle::Note(T("Found when the game started. The same lines are in IlluminatedCore.log."));
			const ScopedWrap wrap;
			for (const auto& problem : survey->problems) {
				Bullet();
				SameLine();
				TextUnformatted(problem.c_str());
			}
		}

		// SKSE Menu Framework calls these from its own DLL on the render thread: nothing may be thrown back across
		void __stdcall Installed()
		{
			Guarded("menu page Installed", [] { RenderInstalled(); });
		}

		void __stdcall Problems()
		{
			Guarded("menu page Problems found", [] { RenderProblems(); });
		}
	}

	void RegisterMenu()
	{
		// its file being there is not enough: a framework that did not load takes every registration and does nothing with it
		if (!SKSEMenuFramework::IsInstalled() || !GetMenuFrameworkModule()) {
			SKSE::log::warn("SKSE Menu Framework is not installed or did not load, so there is no menu; Settings.ini still applies");
			return;
		}
		SKSEMenuFramework::SetSection(Translation::MenuName("Illuminated Core Framework"));
		SKSEMenuFramework::AddSectionItem(Translation::MenuName("Installed"), Installed);
		SKSEMenuFramework::AddSectionItem(Translation::MenuName("Problems found"), Problems);
		SKSE::log::info("menu: the section Illuminated Core Framework added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
