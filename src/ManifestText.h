// Illuminated Core Framework: a data mod's manifest as text - what it names, and whether this core runs it
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// Each data mod ships SKSE\Plugins\IlluminatedCore\Modules\<module>.json. Nothing of the game in here, so the off-game tests
// read it as it is (tests\testcore.cpp). A file is hostile input: nothing here throws, and every text that reaches a message
// box is plain, short and one line.

#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ManifestText
{
	inline constexpr int              kFormat = 1;
	inline constexpr std::string_view kCore = "Illuminated Core Framework";
	inline constexpr std::size_t      kMostNeeds = 32;

	// up to three parts (1, 1.2, 1.2.3); a part left out is 0
	struct Version
	{
		std::array<std::uint32_t, 3> parts{};

		[[nodiscard]] constexpr auto operator<=>(const Version&) const = default;
	};

	[[nodiscard]] inline std::optional<Version> ParseVersion(std::string_view a_text) noexcept
	{
		Version     out;
		std::size_t part = 0;
		while (true) {
			const auto dot = a_text.find('.');
			const auto piece = a_text.substr(0, dot);
			if (part >= out.parts.size() || piece.empty() || piece.size() > 9) {
				return std::nullopt;
			}
			std::uint32_t value = 0;
			const auto*   end = piece.data() + piece.size();
			if (const auto [stop, ec] = std::from_chars(piece.data(), end, value); ec != std::errc{} || stop != end) {
				return std::nullopt;
			}
			out.parts[part++] = value;
			if (dot == std::string_view::npos) {
				return out;
			}
			a_text.remove_prefix(dot + 1);
		}
	}

	[[nodiscard]] inline std::string Shown(const Version& a_version)
	{
		return std::format("{}.{}.{}", a_version.parts[0], a_version.parts[1], a_version.parts[2]);
	}

	// printable ASCII on one line, not empty, at most a_most long, and no < or > (the game's message box reads those as markup):
	// the only text of a file a message box shows
	[[nodiscard]] inline bool Plain(std::string_view a_text, std::size_t a_most) noexcept
	{
		return !a_text.empty() && a_text.size() <= a_most &&
		       std::ranges::all_of(a_text, [](char a_c) { return a_c >= ' ' && a_c <= '~' && a_c != '<' && a_c != '>'; });
	}

	struct Manifest
	{
		std::string              module;
		std::string              title;
		Version                  dataVersion{};
		Version                  needsCore{};
		std::optional<Version>   needsCoreMax{};
		std::vector<std::string> needs{};
		std::string              notice{};
	};

	// the manifest, or what is wrong with the file in one plain sentence
	struct Read
	{
		std::optional<Manifest> manifest{};
		std::string             problem{};
	};

	namespace Detail
	{
		using json = nlohmann::json;

		[[nodiscard]] inline std::optional<std::string> Text(const json& a_object, const char* a_key)
		{
			const auto it = a_object.find(a_key);
			if (it == a_object.end() || !it->is_string()) {
				return std::nullopt;
			}
			return it->get<std::string>();
		}

		[[nodiscard]] inline std::optional<Version> VersionAt(const json& a_object, const char* a_key)
		{
			const auto text = Text(a_object, a_key);
			return text ? ParseVersion(*text) : std::nullopt;
		}

		[[nodiscard]] inline Read Problem(std::string a_what)
		{
			return { std::nullopt, std::move(a_what) };
		}
	}

	[[nodiscard]] inline Read Parse(std::string_view a_text)
	{
		using Detail::Problem;
		if (a_text.starts_with("\xEF\xBB\xBF")) {
			a_text.remove_prefix(3);
		}
		const auto root = Detail::json::parse(a_text, nullptr, false);
		if (root.is_discarded() || !root.is_object()) {
			return Problem("the file is not one { } of settings");
		}
		const auto format = root.find("format");
		if (format == root.end() || !format->is_number_integer() || format->get<std::int64_t>() != kFormat) {
			return Problem(std::format("its \"format\" is not {}", kFormat));
		}
		Manifest out;
		auto     module = Detail::Text(root, "module");
		if (!module || !Plain(*module, 32)) {
			return Problem("it names no \"module\"");
		}
		out.module = std::move(*module);
		auto title = Detail::Text(root, "title");
		out.title = title && Plain(*title, 80) ? std::move(*title) : out.module;
		const auto data = Detail::VersionAt(root, "dataVersion");
		const auto core = Detail::VersionAt(root, "needsCore");
		if (!data || !core) {
			return Problem("its \"dataVersion\" or \"needsCore\" is not a version such as 1.0.0");
		}
		out.dataVersion = *data;
		out.needsCore = *core;
		if (root.contains("needsCoreMax")) {
			out.needsCoreMax = Detail::VersionAt(root, "needsCoreMax");
			if (!out.needsCoreMax) {
				return Problem("its \"needsCoreMax\" is not a version such as 1.0.0");
			}
		}
		if (const auto needs = root.find("needs"); needs != root.end()) {
			if (!needs->is_array() || needs->size() > kMostNeeds) {
				return Problem("its \"needs\" is not a short list of names");
			}
			for (const auto& need : *needs) {
				if (!need.is_string() || !Plain(need.get_ref<const std::string&>(), 32)) {
					return Problem("its \"needs\" is not a short list of names");
				}
				out.needs.push_back(need.get<std::string>());
			}
		}
		if (auto notice = Detail::Text(root, "notice"); notice && Plain(*notice, 200)) {
			out.notice = std::move(*notice);
		}
		return { std::move(out), {} };
	}

	// what this core knows of a module: its id and the lowest data version it runs
	struct Known
	{
		std::string_view id;
		Version          lowestData{};
		// a lighting mod: two of them installed light the same spells twice, so neither runs
		bool lighting{ false };
	};

	enum class Verdict : std::uint8_t
	{
		kRuns,
		kUnknownModule,
		kDataTooOld,
		kCoreTooOld,
		kCoreTooNew,
		kNeedsMissing
	};

	[[nodiscard]] inline const Known* Find(std::span<const Known> a_known, std::string_view a_module) noexcept
	{
		const auto it = std::ranges::find(a_known, a_module, &Known::id);
		return it == a_known.end() ? nullptr : &*it;
	}

	// the first need this core does not have, or nothing
	[[nodiscard]] inline std::optional<std::string_view> Missing(const Manifest& a_manifest, std::span<const std::string_view> a_features) noexcept
	{
		for (const auto& need : a_manifest.needs) {
			if (std::ranges::find(a_features, std::string_view(need)) == a_features.end()) {
				return std::string_view(need);
			}
		}
		return std::nullopt;
	}

	[[nodiscard]] inline Verdict Judge(const Manifest& a_manifest, std::span<const Known> a_known, const Version& a_core,
		std::span<const std::string_view> a_features) noexcept
	{
		const auto* known = Find(a_known, a_manifest.module);
		if (!known) {
			return Verdict::kUnknownModule;
		}
		if (a_manifest.dataVersion < known->lowestData) {
			return Verdict::kDataTooOld;
		}
		if (a_manifest.needsCore > a_core) {
			return Verdict::kCoreTooOld;
		}
		if (a_manifest.needsCoreMax && a_core > *a_manifest.needsCoreMax) {
			return Verdict::kCoreTooNew;
		}
		return Missing(a_manifest, a_features) ? Verdict::kNeedsMissing : Verdict::kRuns;
	}

	// the line the box, the menu and the log show for a module that stays off; empty for one that runs
	[[nodiscard]] inline std::string Line(const Manifest& a_manifest, Verdict a_verdict, std::span<const Known> a_known, const Version& a_core,
		std::span<const std::string_view> a_features)
	{
		const auto name = std::format("{} {}", a_manifest.title, Shown(a_manifest.dataVersion));
		switch (a_verdict) {
		case Verdict::kUnknownModule:
			return std::format("{} names a part (\"{}\") this {} does not have. It stays off.", name, a_manifest.module, kCore);
		case Verdict::kDataTooOld:
			{
				const auto* known = Find(a_known, a_manifest.module);
				return std::format("{} is older than this {} runs ({} or newer). It stays off until it is updated.", name, kCore,
					Shown(known ? known->lowestData : Version{}));
			}
		case Verdict::kCoreTooOld:
			if (!a_manifest.notice.empty()) {
				return a_manifest.notice;
			}
			return std::format("{} needs {} {} or newer; this is {}. It stays off until {} is updated.", name, kCore, Shown(a_manifest.needsCore),
				Shown(a_core), kCore);
		case Verdict::kCoreTooNew:
			return std::format("{} was made for {} up to {}; this is {}. It stays off until it is updated.", name, kCore,
				Shown(a_manifest.needsCoreMax.value_or(Version{})), Shown(a_core));
		case Verdict::kNeedsMissing:
			return std::format("{} needs \"{}\", which this {} does not have. It stays off.", name, Missing(a_manifest, a_features).value_or("?"), kCore);
		case Verdict::kRuns:
		default:
			return {};
		}
	}

	// the module ids named by more than one manifest: an old and a new copy of one mod both installed keep that module off
	[[nodiscard]] inline std::vector<std::string> Duplicates(std::span<const Manifest> a_manifests)
	{
		std::vector<std::string> out;
		for (std::size_t i = 0; i < a_manifests.size(); ++i) {
			const auto& id = a_manifests[i].module;
			const auto  same = std::ranges::count(a_manifests, id, &Manifest::module);
			if (same > 1 && std::ranges::find(out, id) == out.end()) {
				out.push_back(id);
			}
		}
		return out;
	}
}
