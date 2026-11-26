// Illuminated - Core: the object-light library's data and choices, with nothing of the game in them
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// One library lights objects that stand still - Soul Gem Hub's gems, Creatures' torchbugs and jars, the Misc Addon's glowing
// objects - on every lighting, with no Light Placer (his 857). Its rows are files of [object] blocks, one light each:
//   format=1                       the file's format (first, before any block)
//   order=0                        which file wins where two name the same object: the higher order, then the later name
//   [object]
//   name=Dwemer lamp               for the log and the menu
//   model=dwemer\lamp01.nif        an object by its model (any number); or form=0x1234~Plugin.esp / Plugin.esp|0x1234 / EditorID
//   on=cs|enb                      the lightings it shows on: cs (Community Shaders), enb, vanilla; all three when left out
//   color=255,200,140              Light Placer's key names; each may carry one value per lighting (his 938):
//   color.enb=255,190,120          color.cs= / .enb= / .vanilla= win over the plain key on that lighting
//   radius=220 / cutoff=0.5 / size=1.5 / fade=1.2 / flags=PortalStrict|Linear
//   point=0,0,24                   where it hangs in the object's own frame (any number: one light per point)
//   pulse=fire / pulsedepth=0.5    a pulse shape of the kernel's (Pulse.h), played by the game's own dimmer: fire, frost, shock
//                                  and poison as a 12-second loop (his 927), the others as their own loop
// Learned from Light Placer (its key and flag names, powerof3, GPL-3.0) and XEMI (the last file naming a thing wins, Mini,
// GPL-3.0) - their wording and methods, none of their code (his 928). A fault is never silent: every line left out is named by
// its line number, the word and, for an unknown key, the key it most likely misspells (875).
// Tested off the game (ObjectRowsChecks.h). The game side (finding the objects, hanging the lights, the dimmer) comes with the
// first module that hands the library its rows.

#pragma once

#include "DimmerKeys.h"
#include "Pulse.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ObjectRows
{
	// the lighting the game draws with, in the kernel's own order (GameLighting::Lighting: Community Shaders 0, ENB 1, Vanilla 2)
	enum class Lighting : std::uint8_t
	{
		kCommunityShaders,
		kENB,
		kVanilla
	};
	inline constexpr std::size_t                     kLightings = 3;
	inline constexpr std::array<std::string_view, 3> kLightingWords{ "cs", "enb", "vanilla" };
	inline constexpr int                             kFormat = 1;
	// a light's flags by Light Placer's own names; a flag these do not hold is a named fault, never dropped silently
	inline constexpr std::array<std::string_view, 6> kFlagNames{ "PortalStrict", "Linear", "InverseSquare", "IgnoreScale", "Simple",
		"RandomAnimStart" };
	inline constexpr std::uint16_t                   kRandomAnimStart = 1u << 5;
	// the numbers a light may carry (outside them the row is left out and named): Light Placer's own ranges as its files use them
	inline constexpr float       kMostRadius = 4096.0f;
	inline constexpr float       kMostFade = 16.0f;
	inline constexpr float       kMostSize = 64.0f;
	inline constexpr float       kMostPoint = 4096.0f;
	inline constexpr std::size_t kMostPoints = 8;

	struct Rgb
	{
		std::uint8_t r{ 255 };
		std::uint8_t g{ 255 };
		std::uint8_t b{ 255 };
		friend bool  operator==(const Rgb&, const Rgb&) = default;
	};

	struct Point
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
	};

	// one value of a light, with what each lighting has of its own (his 938): the lighting's own wins, then the plain one
	template <class T>
	struct PerLighting
	{
		std::optional<T>                         plain;
		std::array<std::optional<T>, kLightings> own;

		[[nodiscard]] std::optional<T> On(Lighting a_lighting) const
		{
			const auto& mine = own[static_cast<std::size_t>(a_lighting)];
			return mine ? mine : plain;
		}
	};

	// an object named by its form: a plugin and an id, or an editor ID (the game side resolves it)
	struct FormText
	{
		std::string   plugin;    // lower case
		std::uint32_t id{ 0 };   // the id within its plugin
		std::string   editorId;  // lower case; empty when a plugin and an id were given
		friend bool   operator==(const FormText&, const FormText&) = default;
	};

	struct Row
	{
		std::string                  name;
		std::vector<std::string>     models;  // NormalModel
		std::vector<FormText>        forms;
		std::array<bool, kLightings> on{ true, true, true };
		PerLighting<Rgb>             color;
		PerLighting<float>           radius;
		PerLighting<float>           cutoff;
		PerLighting<float>           size;
		PerLighting<float>           fade;
		PerLighting<std::uint16_t>   flags;
		std::vector<Point>           points;
		Pulse::Kind                  pulse{ Pulse::Kind::kNone };
		float                        pulseDepth{ 1.0f };
		std::optional<FormText>      light;      // light=: the light record the row's light is made from, when it names one
		std::size_t                  line{ 0 };  // where its block starts, for the log
	};

	struct Fault
	{
		std::size_t line{ 0 };
		std::string text;
	};

	struct File
	{
		int                format{ 0 };
		int                order{ 0 };
		std::vector<Row>   rows;
		std::vector<Fault> faults;
	};

	// a model path as compared everywhere: lower case, backslashes, no leading "data\" or "meshes\"
	[[nodiscard]] inline std::string NormalModel(std::string_view a_path)
	{
		std::string out(a_path);
		for (auto& c : out) {
			c = c == '/' ? '\\' : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
		}
		for (const std::string_view lead : { "data\\", "meshes\\" }) {
			if (std::string_view(out).starts_with(lead)) {
				out.erase(0, lead.size());
			}
		}
		return out;
	}

	namespace Detail
	{
		[[nodiscard]] inline std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			for (auto& c : out) {
				c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
			}
			return out;
		}

		[[nodiscard]] inline std::string_view Trim(std::string_view a_text) noexcept
		{
			if (a_text.starts_with("\xEF\xBB\xBF")) {
				a_text.remove_prefix(3);
			}
			const auto first = a_text.find_first_not_of(" \t\r");
			if (first == std::string_view::npos) {
				return {};
			}
			return a_text.substr(first, a_text.find_last_not_of(" \t\r") - first + 1);
		}

		// the numbers of a comma list: exactly a_count finite numbers, or nothing
		[[nodiscard]] inline std::optional<std::vector<float>> Numbers(std::string_view a_text, std::size_t a_count)
		{
			// a number too many or too few is no list of a_count, never its first a_count
			if (a_count == 0 || static_cast<std::size_t>(std::ranges::count(a_text, ',')) != a_count - 1) {
				return std::nullopt;
			}
			std::vector<float> out;
			while (out.size() < a_count) {
				const auto comma = a_text.find(',');
				const auto part = Trim(a_text.substr(0, comma));
				float      v = 0.0f;
				const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), v);
				if (part.empty() || ec != std::errc{} || end != part.data() + part.size() || !std::isfinite(v)) {
					return std::nullopt;
				}
				out.push_back(v);
				if (comma == std::string_view::npos) {
					break;
				}
				a_text.remove_prefix(comma + 1);
			}
			return out.size() == a_count ? std::optional{ out } : std::nullopt;
		}

		// a whole number, every character read (format=1.00000001 is not 1; 1.0 and 1.00 are), within a million either way
		[[nodiscard]] inline std::optional<int> Integer(std::string_view a_text)
		{
			a_text = Trim(a_text);
			int        v = 0;
			const auto last = a_text.data() + a_text.size();
			auto [end, ec] = std::from_chars(a_text.data(), last, v);
			if (a_text.empty() || ec != std::errc{} || v < -1000000 || v > 1000000) {
				return std::nullopt;
			}
			// a point followed by zeros only still names the whole number
			if (end != last && *end == '.' && end + 1 != last && std::all_of(end + 1, last, [](char a_c) { return a_c == '0'; })) {
				end = last;
			}
			return end == last ? std::optional{ v } : std::nullopt;
		}

		[[nodiscard]] inline std::optional<float> Number(std::string_view a_text, float a_least, float a_most)
		{
			const auto n = Numbers(a_text, 1);
			return n && (*n)[0] >= a_least && (*n)[0] <= a_most ? std::optional{ (*n)[0] } : std::nullopt;
		}

		[[nodiscard]] inline std::optional<Rgb> Color(std::string_view a_text)
		{
			const auto n = Numbers(a_text, 3);
			if (!n || std::ranges::any_of(*n, [](float a_v) { return a_v < 0.0f || a_v > 255.0f || a_v != std::floor(a_v); })) {
				return std::nullopt;
			}
			return Rgb{ static_cast<std::uint8_t>((*n)[0]), static_cast<std::uint8_t>((*n)[1]), static_cast<std::uint8_t>((*n)[2]) };
		}

		// "a|b|c" split on '|', each part trimmed; an empty part is kept so the caller names it
		[[nodiscard]] inline std::vector<std::string_view> Parts(std::string_view a_text)
		{
			std::vector<std::string_view> out;
			while (true) {
				const auto bar = a_text.find('|');
				out.push_back(Trim(a_text.substr(0, bar)));
				if (bar == std::string_view::npos) {
					return out;
				}
				a_text.remove_prefix(bar + 1);
			}
		}

		[[nodiscard]] inline std::optional<std::uint16_t> Flags(std::string_view a_text, std::string& a_bad)
		{
			std::uint16_t out = 0;
			for (const auto part : Parts(a_text)) {
				const auto at = std::ranges::find_if(kFlagNames, [&](std::string_view a_name) { return Lower(a_name) == Lower(part); });
				if (at == kFlagNames.end()) {
					a_bad = std::string(part);
					return std::nullopt;
				}
				out = static_cast<std::uint16_t>(out | (1u << static_cast<unsigned>(at - kFlagNames.begin())));
			}
			return out;
		}

		[[nodiscard]] inline std::optional<std::uint32_t> Hex(std::string_view a_text)
		{
			if (a_text.starts_with("0x") || a_text.starts_with("0X")) {
				a_text.remove_prefix(2);
			}
			std::uint32_t v = 0;
			const auto [end, ec] = std::from_chars(a_text.data(), a_text.data() + a_text.size(), v, 16);
			return !a_text.empty() && ec == std::errc{} && end == a_text.data() + a_text.size() && v <= 0xFFFFFF ? std::optional{ v } : std::nullopt;
		}

		[[nodiscard]] inline bool PluginName(std::string_view a_name)
		{
			const auto lower = Lower(a_name);
			return lower.size() > 4 && (lower.ends_with(".esp") || lower.ends_with(".esm") || lower.ends_with(".esl"));
		}

		// a block's opening line: [object] in any case, spaces inside the brackets and a # or ; note after it allowed
		[[nodiscard]] inline bool IsBlockHead(std::string_view a_line)
		{
			if (const auto note = a_line.find_first_of("#;"); note != std::string_view::npos) {
				a_line = Trim(a_line.substr(0, note));
			}
			return a_line.size() >= 2 && a_line.front() == '[' && a_line.back() == ']' && Lower(Trim(a_line.substr(1, a_line.size() - 2))) == "object";
		}

		// a key as the reader knows it: its name, and the lighting it is for (none = plain)
		struct Key
		{
			std::string                name;
			std::optional<std::size_t> lighting;
		};

		[[nodiscard]] inline std::optional<Key> SplitKey(std::string_view a_key)
		{
			const auto dot = a_key.find('.');
			if (dot == std::string_view::npos) {
				return Key{ Lower(a_key), std::nullopt };
			}
			const auto word = Lower(a_key.substr(dot + 1));
			const auto at = std::ranges::find(kLightingWords, std::string_view(word));
			if (at == kLightingWords.end()) {
				return std::nullopt;
			}
			return Key{ Lower(a_key.substr(0, dot)), static_cast<std::size_t>(at - kLightingWords.begin()) };
		}

		inline constexpr std::array<std::string_view, 14> kKeys{ "name", "model", "form", "on", "color", "radius", "cutoff", "size", "fade",
			"flags", "point", "pulse", "pulsedepth", "light" };
		inline constexpr std::array<std::string_view, 6>  kPerLighting{ "color", "radius", "cutoff", "size", "fade", "flags" };

		// letters to change to turn one word into the other (a short word only: a key is a dozen letters at most)
		[[nodiscard]] inline std::size_t Distance(std::string_view a_one, std::string_view a_two)
		{
			std::vector<std::size_t> row(a_two.size() + 1);
			for (std::size_t j = 0; j < row.size(); ++j) {
				row[j] = j;
			}
			for (std::size_t i = 1; i <= a_one.size(); ++i) {
				std::size_t diagonal = row[0];
				row[0] = i;
				for (std::size_t j = 1; j <= a_two.size(); ++j) {
					const std::size_t up = row[j];
					row[j] = (std::min)({ row[j] + 1, row[j - 1] + 1, diagonal + (a_one[i - 1] == a_two[j - 1] ? 0u : 1u) });
					diagonal = up;
				}
			}
			return row.back();
		}

		// the key a misspelt one most likely meant, when it is near enough
		[[nodiscard]] inline std::optional<std::string_view> Nearest(std::string_view a_key)
		{
			std::optional<std::string_view> best;
			std::size_t                     least = 3;
			for (const auto key : kKeys) {
				const auto d = Distance(a_key, key);
				if (d < least) {
					least = d;
					best = key;
				}
			}
			return best;
		}
	}

	// an object named by its form, in the three spellings in use: 0x1234~Plugin.esp, Plugin.esp|0x1234, an editor ID
	[[nodiscard]] inline std::optional<FormText> ReadForm(std::string_view a_text)
	{
		a_text = Detail::Trim(a_text);
		if (const auto tilde = a_text.find('~'); tilde != std::string_view::npos) {
			const auto id = Detail::Hex(Detail::Trim(a_text.substr(0, tilde)));
			const auto plugin = Detail::Trim(a_text.substr(tilde + 1));
			return id && Detail::PluginName(plugin) ? std::optional{ FormText{ Detail::Lower(plugin), *id, {} } } : std::nullopt;
		}
		if (const auto bar = a_text.find('|'); bar != std::string_view::npos) {
			const auto plugin = Detail::Trim(a_text.substr(0, bar));
			const auto id = Detail::Hex(Detail::Trim(a_text.substr(bar + 1)));
			return id && Detail::PluginName(plugin) ? std::optional{ FormText{ Detail::Lower(plugin), *id, {} } } : std::nullopt;
		}
		// a bare 0x id names no plugin: refused, so it is named, never looked up as an editor ID that cannot be there
		if (a_text.starts_with("0x") || a_text.starts_with("0X")) {
			return std::nullopt;
		}
		const bool word = !a_text.empty() && std::ranges::all_of(a_text, [](char a_c) {
			return (a_c >= 'a' && a_c <= 'z') || (a_c >= 'A' && a_c <= 'Z') || (a_c >= '0' && a_c <= '9') || a_c == '_';
		});
		return word ? std::optional{ FormText{ {}, 0, Detail::Lower(a_text) } } : std::nullopt;
	}

	// one file of [object] blocks. A bad line leaves its whole block out, named; a file of another format is read not at all
	[[nodiscard]] inline File Read(std::string_view a_text)
	{
		File               file;
		std::optional<Row> row;
		std::string        bad;  // why the open block is left out, empty while it is good
		std::size_t        number = 0;
		bool               formatSeen = false;
		const auto         finish = [&]() {
			if (!row) {
				return;
			}
			if (bad.empty() && row->models.empty() && row->forms.empty()) {
				bad = "it names no model and no form";
			}
			if (bad.empty() && !row->color.plain && std::ranges::none_of(row->color.own, [](const auto& a_c) { return a_c.has_value(); })) {
				bad = "it has no color";
			}
			// a light on no lighting at all would read fine and never show: named instead
			bool shows = false;
			for (std::size_t i = 0; i < kLightings; ++i) {
				shows = shows || (row->on[i] && row->color.On(static_cast<Lighting>(i)).has_value());
			}
			if (bad.empty() && !shows) {
				bad = "it shows on no lighting (its on= leaves out every lighting it has a color for)";
			}
			if (bad.empty()) {
				if (row->points.empty()) {
					row->points.push_back({});
				}
				file.rows.push_back(std::move(*row));
			} else {
				file.faults.push_back({ row->line, "the block from line " + std::to_string(row->line) + " is left out: " + bad });
			}
			row.reset();
			bad.clear();
		};
		while (!a_text.empty()) {
			const auto end = a_text.find('\n');
			auto       line = Detail::Trim(a_text.substr(0, end));
			a_text.remove_prefix(end == std::string_view::npos ? a_text.size() : end + 1);
			++number;
			if (line.empty() || line.starts_with('#') || line.starts_with(';')) {
				continue;
			}
			if (Detail::IsBlockHead(line)) {
				finish();
				if (file.format != kFormat) {
					file.faults.push_back({ number, formatSeen ? "the file's format is " + std::to_string(file.format) + "; this core reads format " +
																	 std::to_string(kFormat) + ": the file is left out" :
																 "the file has no format line before its first block: the file is left out" });
					file.rows.clear();
					return file;
				}
				row = Row{};
				row->line = number;
				continue;
			}
			const auto equals = line.find('=');
			if (equals == std::string_view::npos) {
				if (row && bad.empty()) {
					bad = "line " + std::to_string(number) + " is no key=value line";
				} else if (!row) {
					file.faults.push_back({ number, "line " + std::to_string(number) + " is no key=value line" });
				}
				continue;
			}
			const auto rawKey = Detail::Trim(line.substr(0, equals));
			const auto value = Detail::Trim(line.substr(equals + 1));
			if (!row) {
				const auto key = Detail::Lower(rawKey);
				const auto n = Detail::Integer(value);
				if ((key == "format" || key == "order") && n) {
					(key == "format" ? file.format : file.order) = *n;
					formatSeen = formatSeen || key == "format";
				} else {
					file.faults.push_back({ number, "line " + std::to_string(number) + ": " + std::string(rawKey) + " is no file setting (format, order)" });
				}
				continue;
			}
			if (!bad.empty()) {
				continue;
			}
			const auto key = Detail::SplitKey(rawKey);
			const auto known = key && std::ranges::find(Detail::kKeys, std::string_view(key->name)) != Detail::kKeys.end();
			if (!key || !known || (key->lighting && std::ranges::find(Detail::kPerLighting, std::string_view(key->name)) == Detail::kPerLighting.end())) {
				const auto nearest = key ? Detail::Nearest(key->name) : std::nullopt;
				bad = "line " + std::to_string(number) + ": " + std::string(rawKey) + " is no key" +
				      (nearest && (!key || *nearest != key->name) ? " (the nearest is " + std::string(*nearest) + ")" : std::string{});
				continue;
			}
			const auto& name = key->name;
			const auto  where = key->lighting;
			const auto  wrong = [&]() { bad = "line " + std::to_string(number) + ": " + std::string(rawKey) + "=" + std::string(value) + " does not read"; };
			const auto  put = [&](auto& a_field, auto a_value) {
				if (where) {
					a_field.own[*where] = a_value;
				} else {
					a_field.plain = a_value;
				}
			};
			if (name == "name") {
				row->name = value;
			} else if (name == "model") {
				// a folder alone ("Meshes/") names no model: it would match an object with no model at all
				const auto model = NormalModel(value);
				model.empty() || model.ends_with('\\') ? wrong() : row->models.push_back(model);
			} else if (name == "form") {
				const auto form = ReadForm(value);
				form ? row->forms.push_back(*form) : wrong();
			} else if (name == "on") {
				std::array<bool, kLightings> on{ false, false, false };
				bool                         read = true;
				for (const auto part : Detail::Parts(value)) {
					const auto at = std::ranges::find(kLightingWords, std::string_view(Detail::Lower(part)));
					read = read && at != kLightingWords.end();
					if (at != kLightingWords.end()) {
						on[static_cast<std::size_t>(at - kLightingWords.begin())] = true;
					}
				}
				read ? void(row->on = on) : wrong();
			} else if (name == "color") {
				const auto c = Detail::Color(value);
				c ? put(row->color, *c) : wrong();
			} else if (name == "radius") {
				const auto v = Detail::Number(value, 0.0f, kMostRadius);
				v ? put(row->radius, *v) : wrong();
			} else if (name == "cutoff") {
				const auto v = Detail::Number(value, 0.0f, 1.0f);
				v ? put(row->cutoff, *v) : wrong();
			} else if (name == "size") {
				const auto v = Detail::Number(value, 0.0f, kMostSize);
				v ? put(row->size, *v) : wrong();
			} else if (name == "fade") {
				const auto v = Detail::Number(value, 0.0f, kMostFade);
				v ? put(row->fade, *v) : wrong();
			} else if (name == "flags") {
				std::string flag;
				const auto  f = Detail::Flags(value, flag);
				if (f) {
					put(row->flags, *f);
				} else {
					bad = "line " + std::to_string(number) + ": the flag " + flag + " is not one of Light Placer's this core reads";
				}
			} else if (name == "point") {
				const auto n = Detail::Numbers(value, 3);
				if (!n || row->points.size() >= kMostPoints || std::ranges::any_of(*n, [](float a_v) { return std::abs(a_v) > kMostPoint; })) {
					wrong();
				} else {
					row->points.push_back({ (*n)[0], (*n)[1], (*n)[2] });
				}
			} else if (name == "pulse") {
				const auto kind = Pulse::KindOf(Detail::Lower(value));
				kind ? void(row->pulse = *kind) : wrong();
			} else if (name == "pulsedepth") {
				const auto v = Detail::Number(value, 0.0f, DimmerKeys::kMostDepth);
				v ? void(row->pulseDepth = *v) : wrong();
			} else {
				// light=: a light record named as the row's own, kept for the game side (form= names the object, light= its light)
				const auto light = ReadForm(value);
				light ? void(row->light = *light) : wrong();
			}
		}
		finish();
		if (!formatSeen) {
			file.faults.push_back({ 0, "the file has no format line: the file is left out" });
			file.rows.clear();
		} else if (file.format != kFormat) {
			// a file of another format with no block reaches here: left out like one with blocks
			file.faults.push_back({ 0, "the file's format is " + std::to_string(file.format) + "; this core reads format " + std::to_string(kFormat) +
										   ": the file is left out" });
			file.rows.clear();
		}
		return file;
	}

	// what the light kit gets for one row on the lighting found: nothing where the row does not show on it
	struct Look
	{
		Rgb           color;
		float         radius{ 256.0f };
		float         cutoff{ 0.0f };
		float         size{ 1.0f };
		float         fade{ 1.0f };
		std::uint16_t flags{ 0 };
	};

	[[nodiscard]] inline std::optional<Look> LookOn(const Row& a_row, Lighting a_lighting)
	{
		const auto color = a_row.color.On(a_lighting);
		if (!a_row.on[static_cast<std::size_t>(a_lighting)] || !color) {
			return std::nullopt;
		}
		Look look;
		look.color = *color;
		look.radius = a_row.radius.On(a_lighting).value_or(look.radius);
		look.cutoff = a_row.cutoff.On(a_lighting).value_or(look.cutoff);
		look.size = a_row.size.On(a_lighting).value_or(look.size);
		look.fade = a_row.fade.On(a_lighting).value_or(look.fade);
		look.flags = a_row.flags.On(a_lighting).value_or(look.flags);
		return look;
	}

	// every row of every file, each object led to its rows; where two files name the same object, the higher order wins, then the
	// later file name, and the loser's rows for that object are dropped (XEMI's last-loaded-wins, by order, never by load timing)
	class Index
	{
	public:
		struct Source
		{
			std::string name;  // the file's name, for the order and the log
			File        file;
		};

		explicit Index(std::vector<Source> a_sources)
		{
			std::ranges::sort(a_sources, [](const Source& a_one, const Source& a_two) {
				return a_one.file.order != a_two.file.order ? a_one.file.order < a_two.file.order : a_one.name < a_two.name;
			});
			for (std::size_t rank = 0; rank < a_sources.size(); ++rank) {
				auto&                                                        source = a_sources[rank];
				std::map<std::string, std::vector<std::size_t>, std::less<>> models;
				std::map<std::string, std::vector<std::size_t>, std::less<>> forms;
				for (auto& r : source.file.rows) {
					const auto at = rows.size();
					rows.push_back(std::move(r));
					files.push_back(source.name);
					ranks.push_back(rank);
					// a row that names one object twice (model=a.nif and model=Meshes/A.NIF) is still one light: indexed once per key
					const auto once = [at](std::vector<std::size_t>& a_list) {
						if (a_list.empty() || a_list.back() != at) {
							a_list.push_back(at);
						}
					};
					for (const auto& m : rows.back().models) {
						once(models[m]);
					}
					for (const auto& f : rows.back().forms) {
						once(forms[FormKey(f)]);
					}
				}
				// this file names these objects: what an earlier file had for them is replaced, never added to
				for (auto& [model, list] : models) {
					byModel.insert_or_assign(model, std::move(list));
				}
				for (auto& [form, list] : forms) {
					byForm.insert_or_assign(form, std::move(list));
				}
			}
		}

		// the rows that light an object, asked by every name it has: its plugin and id, its editor ID (the game side reads both off
		// the base form) and its model, so a file that names it one way and a file that names it another still meet. Every list holds
		// the rows of one file (a later file replaces it whole): the file ranked last of them wins the object; within one file, its
		// rows by plugin and id first, then by editor ID, then by model
		[[nodiscard]] std::span<const std::size_t> Of(const std::optional<FormText>& a_form, std::string_view a_editorId, std::string_view a_model) const
		{
			const std::vector<std::size_t>* best = nullptr;
			std::size_t                     bestRank = 0;
			int                             bestWay = -1;
			const auto                      weigh = [&](const std::vector<std::size_t>* a_list, int a_way) {
				if (!a_list || a_list->empty()) {
					return;
				}
				const auto rank = ranks[a_list->front()];
				if (!best || rank > bestRank || (rank == bestRank && a_way > bestWay)) {
					best = a_list;
					bestRank = rank;
					bestWay = a_way;
				}
			};
			if (a_form && a_form->editorId.empty()) {
				// the index holds plugin names in lower case: a caller may hand the file's own spelling
				const auto it = byForm.find(FormKey(FormText{ Detail::Lower(a_form->plugin), a_form->id, {} }));
				weigh(it != byForm.end() ? &it->second : nullptr, 2);
			}
			if (const auto editorId = a_form && !a_form->editorId.empty() ? std::string_view(a_form->editorId) : a_editorId; !editorId.empty()) {
				const auto it = byForm.find(FormKey(FormText{ {}, 0, Detail::Lower(editorId) }));
				weigh(it != byForm.end() ? &it->second : nullptr, 1);
			}
			if (!a_model.empty()) {
				const auto it = byModel.find(NormalModel(a_model));
				weigh(it != byModel.end() ? &it->second : nullptr, 0);
			}
			return best ? std::span<const std::size_t>(*best) : std::span<const std::size_t>{};
		}

		[[nodiscard]] const Row&         RowAt(std::size_t a_row) const { return rows.at(a_row); }
		[[nodiscard]] const std::string& FileOf(std::size_t a_row) const { return files.at(a_row); }
		[[nodiscard]] std::size_t        Models() const noexcept { return byModel.size(); }
		[[nodiscard]] std::size_t        Forms() const noexcept { return byForm.size(); }

		[[nodiscard]] static std::string FormKey(const FormText& a_form)
		{
			return a_form.editorId.empty() ? a_form.plugin + "|" + std::to_string(a_form.id) : "#" + a_form.editorId;
		}

	private:
		std::vector<Row>                                             rows;
		std::vector<std::string>                                     files;
		std::vector<std::size_t>                                     ranks;  // each row's file, in the winning order
		std::map<std::string, std::vector<std::size_t>, std::less<>> byModel;
		std::map<std::string, std::vector<std::size_t>, std::less<>> byForm;
	};

	// which objects get a light: the nearest a_cap of those that want one. One already lit counts as a fifth nearer, so two objects
	// at the edge never trade a light as the player moves (Soul Gem Hub's shipped rule). a_cap 0 with a_unlimited lights every one
	// (RE::Light loaded: it decides how many show, Creatures' shipped rule)
	struct Candidate
	{
		std::uint32_t id{ 0 };
		float         distance{ 0.0f };  // squared, to the player
		bool          lit{ false };
	};

	inline constexpr float kLitCloser = 0.64f;

	[[nodiscard]] inline std::vector<std::uint32_t> Choose(std::vector<Candidate> a_candidates, std::size_t a_cap, bool a_unlimited)
	{
		std::erase_if(a_candidates, [](const Candidate& a_c) { return !std::isfinite(a_c.distance) || a_c.distance < 0.0f; });
		const auto weighed = [](const Candidate& a_c) { return a_c.distance * (a_c.lit ? kLitCloser : 1.0f); };
		// an object handed in twice takes one place, at its nearest
		std::ranges::sort(a_candidates, [&](const Candidate& a_one, const Candidate& a_two) {
			return a_one.id != a_two.id ? a_one.id < a_two.id : weighed(a_one) < weighed(a_two);
		});
		const auto repeats = std::ranges::unique(a_candidates, {}, &Candidate::id);
		a_candidates.erase(repeats.begin(), repeats.end());
		if (!a_unlimited && a_candidates.size() > a_cap) {
			std::ranges::stable_sort(a_candidates, [&](const Candidate& a_one, const Candidate& a_two) {
				return weighed(a_one) != weighed(a_two) ? weighed(a_one) < weighed(a_two) : a_one.id < a_two.id;
			});
			a_candidates.resize(a_cap);
		}
		std::vector<std::uint32_t> out;
		out.reserve(a_candidates.size());
		for (const auto& c : a_candidates) {
			out.push_back(c.id);
		}
		std::ranges::sort(out);
		return out;
	}

	// the loop of keys the game's own dimmer plays for a row's pulse on one placed object: nothing for a steady light. The keys are
	// shares of the light's steady fade (the kit multiplies them by it, DimmerKit::MakeKeys). With RandomAnimStart each placement
	// takes its own place in the shape from its reference's id (Light Placer's flag); without it every copy pulses together
	[[nodiscard]] inline std::optional<DimmerKeys::Loop> PulseOf(const Row& a_row, Lighting a_lighting, std::uint32_t a_reference, double a_now)
	{
		if (a_row.pulse == Pulse::Kind::kNone || !(a_row.pulseDepth > 0.0f)) {
			return std::nullopt;
		}
		const auto flags = a_row.flags.On(a_lighting).value_or(0);
		const auto seed = (flags & kRandomAnimStart) != 0 ? a_reference : 0u;
		return DimmerKeys::Bake(a_row.pulse, seed, a_row.pulseDepth, a_now);
	}
}
