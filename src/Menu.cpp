// Illuminated - Core - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// The SKSE Menu Framework section "Illuminated - Core": the Installed page (each installed mod, its version, whether its part
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
			MenuStyle::Page page;
			const auto      survey = SurveyNow();
			const auto      switches = SwitchesNow();

			Header(Icon::kList, T("Installed"));
			TextColored(kMuted, "%s", Fill(T("Illuminated - Core {}"), ManifestText::Shown(VersionNow())).c_str());
			if (survey->rows.empty()) {
				MenuStyle::Note(T("No mod that needs Illuminated - Core is installed. Nothing runs."));
				return;
			}
			for (const auto& row : survey->rows) {
				const auto name = std::format("{} {}", row.title, ManifestText::Shown(row.dataVersion));
				const auto it = std::ranges::find(switches, std::string_view(row.module), &std::pair<std::string_view, bool>::first);
				if (!row.known || it == switches.end()) {
					MenuStyle::Status(false, name.c_str());
				} else {
					// the mod's switch; its label is the mod's own name, and two rows never share one (a part named twice stays off)
					bool on = it->second;
					PushID(row.module.c_str());
					if (Checkbox(name.c_str(), &on)) {
						static_cast<void>(SetSwitch(row.module, on));
					}
					PopID();
					MenuStyle::Tip(T("Off, this mod's part of Illuminated - Core does not run. A change takes hold from the next game start."));
					// what the next start does, only where it differs from this session
					if (on != (row.state != SurveyText::State::kSwitchedOff)) {
						SameLine(0.0f, 12.0f);
						TextColored(kMuted, "%s", on ? T("on from the next game start") : T("off from the next game start"));
					}
				}
				SameLine(0.0f, 12.0f);
				TextColored(row.state == SurveyText::State::kStaysOff ? MenuStyle::kBad : kMuted, "%s", StateWords(row.state));
			}
		}

		void RenderProblems()
		{
			MenuStyle::Page page;
			const auto      survey = SurveyNow();

			Header(Icon::kList, T("Problems found"));
			if (survey->problems.empty()) {
				MenuStyle::Note(T("No problem was found when the game started."));
				return;
			}
			MenuStyle::Note(T("Found when the game started. The same lines are in IlluminatedCore.log."));
			PushTextWrapPos(0.0f);
			for (const auto& problem : survey->problems) {
				Bullet();
				SameLine();
				TextUnformatted(problem.c_str());
			}
			PopTextWrapPos();
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
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::warn("SKSE Menu Framework is not installed, so there is no menu; Settings.ini still applies");
			return;
		}
		SKSEMenuFramework::SetSection(T("Illuminated - Core"));
		SKSEMenuFramework::AddSectionItem(T("Installed"), Installed);
		SKSEMenuFramework::AddSectionItem(T("Problems found"), Problems);
		SKSE::log::info("menu: the section Illuminated - Core added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
