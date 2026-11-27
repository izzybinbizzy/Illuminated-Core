// Illuminated Core Framework - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// What is installed: every data mod's manifest (SKSE\Plugins\IlluminatedCore\Modules\<part>.json) and the switches file read once
// at post-load, judged by the game-free SurveyText.h, and kept for the menu, DevBench and the log. It is read that early because
// a part's hooks are written at post-load too, and only an awake part's are written. The one box waits for data load, when the
// game can show it. A switch moved in the menu is written back here.

#include "Kernel.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>

namespace Core
{
	namespace
	{
		constexpr const char* kModules = "Data/SKSE/Plugins/IlluminatedCore/Modules";
		constexpr const char* kSwitches = "Data/SKSE/Plugins/IlluminatedCore/Settings.ini";
		// a manifest is a few lines: a file past this size is not one, and is never read whole
		constexpr std::uintmax_t kMostBytes = 64 * 1024;
		// one file per mod: a folder holding more than this is not read past it
		constexpr std::size_t kMostFiles = 64;

		// the parts built into this version, each with the lowest data version it runs, and what a manifest may ask for by name
		constexpr std::span<const ManifestText::Known> kParts{};
		constexpr std::span<const std::string_view>    kFeatures{};
		// each part's work per moment of the session, in the order the parts wake; a part joins both lists together
		constexpr std::span<const Lifecycle::Part> kWork{};
		// the two lists are one list told twice: a part named in one and not the other, or in another place, never builds
		[[nodiscard]] consteval bool OneList()
		{
			if (kParts.size() != kWork.size()) {
				return false;
			}
			for (std::size_t i = 0; i < kParts.size(); ++i) {
				if (kParts[i].id != kWork[i].known.id) {
					return false;
				}
			}
			return true;
		}
		static_assert(OneList(), "kParts and kWork name the same parts in the same order");

		void PartFault(std::string_view a_part, Lifecycle::Moment a_moment) noexcept
		{
			// the log line itself may throw (low memory): never out of a callback of the game
			try {
				SKSE::log::error("{}: its work at {} stopped by an error", a_part, Lifecycle::NameOf(a_moment));
			} catch (...) {
			}
		}

		struct State
		{
			std::mutex                                     lock;
			std::shared_ptr<const SurveyText::Survey>      survey{ std::make_shared<const SurveyText::Survey>() };
			std::vector<std::pair<std::string_view, bool>> switches;
			// the one box's text, kept from the survey until the game can show it
			std::string box;
			bool        saveQueued{ false };
		};

		[[nodiscard]] State& Get()
		{
			static State state;
			return state;
		}

		// a path's text as UTF-8 bytes in a plain string
		[[nodiscard]] std::string Bytes(const std::filesystem::path& a_path)
		{
			const auto text = a_path.generic_u8string();
			return { reinterpret_cast<const char*>(text.data()), text.size() };
		}

		// a small file's whole text, or nothing when it is missing, too large or cannot be read
		[[nodiscard]] std::optional<std::string> FileText(const std::filesystem::path& a_path)
		{
			std::error_code ec;
			const auto      size = std::filesystem::file_size(a_path, ec);
			if (ec) {
				return std::nullopt;
			}
			// the box says only "could not be read": the log says why, so the one who made the file can find it
			if (size > kMostBytes) {
				SKSE::log::warn("{} is {} KB, past the {} KB a file of the core may be: it is not read", Bytes(a_path), size / 1024, kMostBytes / 1024);
				return std::nullopt;
			}
			std::ifstream in(a_path, std::ios::binary);
			if (!in) {
				return std::nullopt;
			}
			std::string text(static_cast<std::size_t>(size), '\0');
			in.read(text.data(), static_cast<std::streamsize>(text.size()));
			if (static_cast<std::uintmax_t>(in.gcount()) != size) {
				return std::nullopt;
			}
			return text;
		}

		// every *.json of the Modules folder (the ending in any case), by name so two starts read them in one order; a_leftOut
		// counts the ones past the most a start reads; a_unread is set when the folder is there but its listing stopped on an error
		[[nodiscard]] std::vector<SurveyText::File> ReadManifests(std::size_t& a_leftOut, bool& a_unread)
		{
			std::vector<SurveyText::File> out;
			std::error_code               ec;
			// no Modules folder at all is no mod installed, asked plainly: a missing folder seen through a virtual file system
			// (MO2) can fail the listing with a code other than "no such file", and that is no folder, not one that could not be opened
			if (const bool there = std::filesystem::exists(kModules, ec); !there && !ec) {
				return out;
			}
			std::filesystem::directory_iterator it(kModules, ec);
			if (ec) {
				a_unread = true;
				return out;
			}
			for (const std::filesystem::directory_iterator end; !ec && it != end; it.increment(ec)) {
				const auto& path = it->path();
				if (!SwitchesText::Same(Bytes(path.extension()), ".json")) {
					continue;
				}
				// its own error: one entry that cannot be looked at must not end the read of the folder
				std::error_code entry;
				const bool      file = it->is_regular_file(entry);
				if (!entry && !file) {
					continue;
				}
				if (out.size() >= kMostFiles) {
					++a_leftOut;
					continue;
				}
				// an entry that could not be looked at goes on as a file that could not be read, so it is named
				out.push_back({ Bytes(path.filename()), entry ? std::nullopt : FileText(path) });
			}
			// the listing ended on an error and not at its end: what it did reach is still judged, and the survey says so
			a_unread = static_cast<bool>(ec);
			std::ranges::sort(out, {}, &SurveyText::File::name);
			return out;
		}

		// written beside the file and moved over it: a crash or a full disk mid-write leaves the old file, never half of one
		void SaveSwitches()
		{
			std::string text;
			{
				auto&            state = Get();
				std::scoped_lock l{ state.lock };
				state.saveQueued = false;
				text = SwitchesText::Text(state.switches);
			}
			const std::string tmp = std::string(kSwitches) + ".tmp";
			std::error_code   ec;
			std::filesystem::create_directories(std::filesystem::path(kSwitches).parent_path(), ec);
			{
				std::ofstream file(tmp, std::ios::trunc);
				file << text;
				file.close();
				if (!file) {
					SKSE::log::warn("switches: {} could not be written", kSwitches);
					std::filesystem::remove(tmp, ec);
					return;
				}
			}
			if (std::filesystem::rename(tmp, kSwitches, ec); ec) {
				SKSE::log::warn("switches: {} could not be replaced ({})", kSwitches, ec.message());
				std::filesystem::remove(tmp, ec);
			}
		}
	}

	ManifestText::Version VersionNow()
	{
		const auto* declared = SKSE::PluginDeclaration::GetSingleton();
		if (!declared) {
			return {};
		}
		const auto version = declared->GetVersion();
		return { { version.major(), version.minor(), version.patch() } };
	}

	std::span<const ManifestText::Known> Parts()
	{
		return kParts;
	}

	namespace
	{
		// the files read and judged; throws when a read does (low memory, a path that cannot be turned to text)
		[[nodiscard]] std::shared_ptr<const SurveyText::Survey> Surveyed()
		{
			std::size_t     leftOut = 0;
			bool            folderUnread = false;
			const auto      files = ReadManifests(leftOut, folderUnread);
			const auto      read = FileText(kSwitches);
			std::error_code ec;
			// no file is every switch on; a file that is there and cannot be read is said so and never written over, and a file
			// that cannot even be looked at counts as there: an error of exists() must never let a move in the menu write over it
			const bool wide = read && SwitchesText::Wide(*read);
			const bool unread = wide || (!read && (std::filesystem::exists(kSwitches, ec) || ec));
			// a UTF-16 file reads as no switch at all, every part on and no word why: it is a file that could not be read instead
			const auto switches = wide ? std::string{} : read.value_or(std::string{});
			// an old version's plugin or files are looked for per part; no part is built in yet
			auto survey = std::make_shared<const SurveyText::Survey>(
				SurveyText::Look({ files, switches, kParts, VersionNow(), kFeatures, {}, unread, leftOut, folderUnread }));
			if (survey->rows.empty()) {
				SKSE::log::info("{}", survey->filesFound ? "no file of the Modules folder could be used: nothing runs" :
														   "no mod that needs Illuminated Core Framework is installed: nothing runs");
			}
			for (const auto& row : survey->rows) {
				SKSE::log::info("{} {}: {}", row.title, ManifestText::Shown(row.dataVersion),
					row.state == SurveyText::State::kRuns ? "its part runs" : row.state == SurveyText::State::kSwitchedOff ? "switched off in Settings.ini" :
																															 "stays off");
			}
			return survey;
		}
	}

	void Look()
	{
		std::shared_ptr<const SurveyText::Survey> survey;
		// a look that failed is said wherever a survey shows: an empty one would read as "nothing installed"
		try {
			survey = Surveyed();
		} catch (const std::exception& e) {
			SKSE::log::error("what is installed could not be worked out ({})", e.what());
			survey = std::make_shared<const SurveyText::Survey>(SurveyText::Failed());
		} catch (...) {
			SKSE::log::error("what is installed could not be worked out (an unknown error)");
			survey = std::make_shared<const SurveyText::Survey>(SurveyText::Failed());
		}
		for (const auto& problem : survey->problems) {
			SKSE::log::error("{}", problem);
		}
		auto             box = SurveyText::BoxText(*survey);
		auto&            state = Get();
		std::scoped_lock l{ state.lock };
		state.switches = survey->switches;
		state.box = std::move(box);
		state.survey = std::move(survey);
	}

	void TellPlayer()
	{
		std::string box;
		{
			auto&            state = Get();
			std::scoped_lock l{ state.lock };
			box = std::move(state.box);
			state.box.clear();
		}
		// one box per start, shown by the game at the main menu; never a chain of them
		if (!box.empty()) {
			RE::DebugMessageBox(box.c_str());
		}
	}

	bool AnyAwake()
	{
		const auto survey = SurveyNow();
		return std::ranges::any_of(survey->rows, [](const SurveyText::Row& a_row) { return a_row.state == SurveyText::State::kRuns; });
	}

	namespace
	{
		std::atomic<std::uint32_t> gLoad{ 0 };
	}

	std::uint32_t LoadNow() noexcept
	{
		return gLoad.load(std::memory_order_acquire);
	}

	void Tell(Lifecycle::Moment a_moment, bool a_ok)
	{
		// the old world's work is stale from here: the number moves before any part is told, so a loader thread that reads it
		// after this sees the new number even while the parts still drop the old lists
		if (a_moment == Lifecycle::Moment::kPreLoadGame || a_moment == Lifecycle::Moment::kNewGame) {
			gLoad.fetch_add(1, std::memory_order_acq_rel);
		}
		Lifecycle::Run(kWork, a_moment, a_ok, [](std::string_view a_id) { return IsAwake(a_id); }, PartFault);
	}

	bool IsAwake(std::string_view a_part)
	{
		const auto survey = SurveyNow();
		return std::ranges::any_of(survey->rows,
			[a_part](const SurveyText::Row& a_row) { return a_row.state == SurveyText::State::kRuns && a_row.module == a_part; });
	}

	std::shared_ptr<const SurveyText::Survey> SurveyNow()
	{
		auto&            state = Get();
		std::scoped_lock l{ state.lock };
		return state.survey;
	}

	std::vector<std::pair<std::string_view, bool>> SwitchesNow()
	{
		auto&            state = Get();
		std::scoped_lock l{ state.lock };
		return state.switches;
	}

	bool SetSwitch(std::string_view a_part, bool a_on)
	{
		{
			auto&            state = Get();
			std::scoped_lock l{ state.lock };
			const auto       it = std::ranges::find(state.switches, a_part, &std::pair<std::string_view, bool>::first);
			if (it == state.switches.end()) {
				return false;
			}
			it->second = a_on;
			// a file that could not be read at the start holds switches this session never saw: it is not written over
			if (state.saveQueued || state.survey->switchesUnread) {
				return true;
			}
			state.saveQueued = true;
		}
		// the menu draws on the render thread and DevBench answers on its own: the file is written by one main-thread task
		if (!Later("switches save", []() { SaveSwitches(); })) {
			auto&            state = Get();
			std::scoped_lock l{ state.lock };
			state.saveQueued = false;
			SKSE::log::warn("switches: the change could not reach the main thread - it is kept for this session but not saved");
		}
		return true;
	}
}
