// as_vip unit tests — the engine-free part: the self-contained KV1 reader, the vip.ini ("VIP"
// block) group map, the candidate config paths, the OnAdminConnected group selection (including
// its asymmetries), and the real vip.ini files the package ships.
//
// Run by ../../test.sh (inside the Steam Runtime image, cwd = build/tests). No pisexcompat and no
// Plugify binding is involved: only src/vip_groups.hpp is compiled in.

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "vip_groups.hpp"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(expr)                                                                            \
	do {                                                                                       \
		++g_checks;                                                                            \
		if (!(expr)) {                                                                         \
			++g_failures;                                                                      \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);                        \
		}                                                                                      \
	} while (false)

void CheckEq(std::string_view actual, std::string_view expected, const char* what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL %s: got '%s', expected '%s'\n", what, std::string(actual).c_str(),
					std::string(expected).c_str());
	}
}

// The value SelectGroup picked for an admin holding `permissions`, or a marker for "none".
std::string Picked(const as_vip::GroupMap& groups, const std::vector<std::string>& permissions) {
	const std::string* group = as_vip::SelectGroup(groups, [&permissions](const std::string& p) {
		for (const std::string& held : permissions) {
			if (held == p) {
				return true;
			}
		}
		return false;
	});
	if (group == nullptr) {
		return "<none>";
	}
	return "<" + *group + ">";
}

// --- the KV1 reader ----------------------------------------------------------------------------

void TestParseTable() {
	std::printf("-- KV1 reader: quoted/unquoted tokens and comments\n");

	// A hand-edited vip.ini: both comment styles, a trailing comment, an unquoted value, the empty
	// "@admin/root" value (the entry must still exist in the map) and blank lines.
	const std::string_view text = R"(
// The Pisex layout, with the comments an admin leaves behind.
"VIP"
{
    "@admin/root"       ""                 // empty group: the grant still happens
    /* the next entry is unquoted on purpose */
    @admin/megavip      MEGA
    "@admin/ultravip"   "ULTRA"
})";

	as_vip::GroupMap map;
	CHECK(as_vip::LoadGroupMapFromText(text, map));
	CHECK(map.size() == 3);
	CheckEq(map["@admin/root"], "", "map[@admin/root]");
	CheckEq(map["@admin/megavip"], "MEGA", "unquoted value");
	CheckEq(map["@admin/ultravip"], "ULTRA", "quoted value");

	// The map is std::map: iteration is lexicographic by permission, which is the order the
	// original's g_mapGroups walked and SelectGroup walks.
	std::string order;
	for (const auto& entry : map) {
		order += entry.first + ";";
	}
	CheckEq(order, "@admin/megavip;@admin/root;@admin/ultravip;", "iteration order");
}

void TestParseEscapesAndBom() {
	std::printf("-- KV1 reader: escapes, BOM, block lookup\n");

	as_vip::GroupMap map;
	// \" \\ \n \r \t are decoded inside quotes; an unknown escape keeps its backslash.
	const std::string_view escaped = R"("VIP"
{
    "@admin/quote"   "a\"b\\c"
    "@admin/tab"     "x\ty"
    "@admin/newline" "a\nb"
    "@admin/unknown" "back\slash"
})";
	CHECK(as_vip::LoadGroupMapFromText(escaped, map));
	CHECK(map.size() == 4);
	CheckEq(map["@admin/quote"], "a\"b\\c", "escaped quote and backslash");
	CheckEq(map["@admin/tab"], "x\ty", "escaped tab");
	CheckEq(map["@admin/newline"], "a\nb", "escaped newline");
	CheckEq(map["@admin/unknown"], "back\\slash", "unknown escape keeps the backslash");

	// A UTF-8 BOM before the block is skipped instead of becoming part of the "VIP" name.
	const std::string bom = std::string("\xEF\xBB\xBF") + R"("VIP" { "@admin/root" "PROVIP" })";
	map.clear();
	CHECK(as_vip::LoadGroupMapFromText(bom, map));
	CheckEq(map["@admin/root"], "PROVIP", "after BOM");

	// The block name is matched case-insensitively, like Valve's FindKey.
	map.clear();
	CHECK(as_vip::LoadGroupMapFromText(R"("vip" { "@admin/root" "PROVIP" })", map));
	CheckEq(map["@admin/root"], "PROVIP", "lower-case block name");

	// A block child of "VIP" is a KeyValues sub-key, not a permission entry (the original's
	// GetFirstValue skipped it too); only nodes carrying a value are entries.
	const std::string_view nested = R"("VIP"
{
    "@admin/root"   "PROVIP"
    "@admin/groups" { "@admin/vip" "LITE" }
})";
	map.clear();
	CHECK(as_vip::LoadGroupMapFromText(nested, map));
	CHECK(map.size() == 1);
	CheckEq(map["@admin/root"], "PROVIP", "root next to a nested block");
	CHECK(map.find("@admin/groups") == map.end());

	// #base / #include / #define lines are skipped, not followed.
	const std::string_view directives = R"(#base "admin_system/base.ini"
#include "shared.ini"
#define VIP
"VIP" { "@admin/root" "PROVIP" })";
	map.clear();
	CHECK(as_vip::LoadGroupMapFromText(directives, map));
	CheckEq(map["@admin/root"], "PROVIP", "after preprocessor lines");

	// An empty table parses (and replaces whatever was there with nothing).
	map = {{"@admin/stale", "STALE"}};
	CHECK(as_vip::LoadGroupMapFromText(R"("VIP" { })", map));
	CHECK(map.empty());
}

void TestParseMalformed() {
	std::printf("-- KV1 reader: malformed files are rejected whole\n");

	// Whatever the map held before must survive a rejected file: the caller may still try the
	// next candidate config.
	as_vip::GroupMap map = {{"@admin/root", "KEEP"}};

	const std::string_view missingBrace = R"("VIP" { "@admin/root" "PROVIP")";
	CHECK(!as_vip::LoadGroupMapFromText(missingBrace, map));

	const std::string_view unterminated = R"("VIP" { "@admin/root" "PROVIP })";
	CHECK(!as_vip::LoadGroupMapFromText(unterminated, map));

	const std::string_view strayBrace = R"("VIP" { "@admin/root" "PROVIP" } })";
	CHECK(!as_vip::LoadGroupMapFromText(strayBrace, map));

	// Well-formed KeyValues without a "VIP" block: not this module's table.
	CHECK(!as_vip::LoadGroupMapFromText(R"("Groups" { "@admin/root" "PROVIP" })", map));
	// A plain "VIP" key is not a table either.
	CHECK(!as_vip::LoadGroupMapFromText(R"("VIP" "PROVIP")", map));
	// No top-level block at all.
	CHECK(!as_vip::LoadGroupMapFromText("not keyvalues at all", map));
	// Empty input.
	CHECK(!as_vip::LoadGroupMapFromText("", map));

	CHECK(map.size() == 1);
	CheckEq(map["@admin/root"], "KEEP", "map untouched by rejected files");
}

// --- the shipped config files ------------------------------------------------------------------

void TestProdConfig() {
	std::printf("-- production vip.ini\n");

	CHECK(as_vip::FileExists(AS_VIP_TEST_PROD_CONFIG));

	as_vip::GroupMap map;
	CHECK(as_vip::LoadGroupMapFromFile(AS_VIP_TEST_PROD_CONFIG, map));
	CHECK(map.size() == 3);
	CheckEq(map["@admin/root"], "PROVIP", "prod map[@admin/root]");
	CheckEq(map["@admin/vip"], "LITE", "prod map[@admin/vip]");
	CheckEq(map["@admin/warn"], "WARN", "prod map[@admin/warn]");

	CheckEq(Picked(map, {"@admin/root"}), "<PROVIP>", "prod root");
	CheckEq(Picked(map, {"@admin/warn"}), "<WARN>", "prod warn");
}

void TestLiveConfig() {
	std::printf("-- live vip.ini\n");

	CHECK(as_vip::FileExists(AS_VIP_TEST_LIVE_CONFIG));

	as_vip::GroupMap map;
	CHECK(as_vip::LoadGroupMapFromFile(AS_VIP_TEST_LIVE_CONFIG, map));
	CHECK(map.size() == 3);
	// The shipped file maps @admin/root to the empty group: the grant still happens (with an empty
	// group), exactly as the original would have sent it.
	CheckEq(map["@admin/root"], "", "live map[@admin/root]");
	CheckEq(Picked(map, {"@admin/root"}), "<>", "live root");
	CheckEq(Picked(map, {"@admin/ultravip"}), "<ULTRA>", "live ultravip");
}

void TestMissingFile() {
	std::printf("-- missing config\n");

	CHECK(!as_vip::FileExists("/nonexistent/as_vip/vip.ini"));

	as_vip::GroupMap map = {{"@admin/root", "KEEP"}};
	CHECK(!as_vip::LoadGroupMapFromFile("/nonexistent/as_vip/vip.ini", map));
	CHECK(map.size() == 1);
	CheckEq(map["@admin/root"], "KEEP", "map untouched by a missing file");
}

// --- the selection logic ------------------------------------------------------------------------

void TestSelectGroup() {
	std::printf("-- SelectGroup\n");

	// The original's four branches, with the live file above.
	const as_vip::GroupMap live = {
		{"@admin/megavip", "MEGA"},
		{"@admin/root", ""},
		{"@admin/ultravip", "ULTRA"},
	};

	// root: the "@admin/root" entry - even when its value is empty.
	CheckEq(Picked(live, {"@admin/root"}), "<>", "root with entry");
	// root without the entry: nothing, even when other permissions match (the else-branch does
	// not run for root admins).
	const as_vip::GroupMap no_root_entry = {{"@admin/megavip", "MEGA"}};
	CheckEq(Picked(no_root_entry, {"@admin/megavip", "@admin/root"}), "<none>", "root without entry");
	// Non-root with that same permission set minus root: the loop runs.
	CheckEq(Picked(no_root_entry, {"@admin/megavip"}), "<MEGA>", "non-root with entry");

	// non-root: first matching entry in map order.
	CheckEq(Picked(live, {"@admin/megavip"}), "<MEGA>", "megavip");
	CheckEq(Picked(live, {"@admin/ultravip"}), "<ULTRA>", "ultravip");
	// Both held: "@admin/megavip" < "@admin/root" < "@admin/ultravip", so megavip wins.
	CheckEq(Picked(live, {"@admin/ultravip", "@admin/megavip"}), "<MEGA>", "first in map order");
	// The empty root entry wins over a later permission the admin also holds.
	CheckEq(Picked(live, {"@admin/ultravip", "@admin/root"}), "<>", "empty entry wins by order");
	// No permission held: nothing.
	CheckEq(Picked(live, {}), "<none>", "no permissions");
	CheckEq(Picked(live, {"@admin/warn"}), "<none>", "no match");
}

// --- the config path candidates -----------------------------------------------------------------

void TestConfigCandidates() {
	std::printf("-- config candidates\n");

	const std::vector<std::string> candidates =
		as_vip::ConfigCandidates("/srv/cs2/game/csgo/addons/plugify/configs");
	CHECK(candidates.size() == 2);
	// The Pisex layout first: every admin module shares configs/admin_system, and production
	// keeps vip.ini there.
	CheckEq(candidates[0], "/srv/cs2/game/csgo/addons/plugify/configs/admin_system/vip.ini",
			"primary candidate");
	// The plugin's own folder (PLUGIN_STANDARD §3) is the fallback.
	CheckEq(candidates[1], "/srv/cs2/game/csgo/addons/plugify/configs/as_vip/vip.ini", "fallback candidate");

	// A trailing slash on the host's configs directory makes no difference.
	const std::vector<std::string> trailing = as_vip::ConfigCandidates("/srv/configs/");
	CHECK(trailing.size() == 2);
	CheckEq(trailing[0], "/srv/configs/admin_system/vip.ini", "trailing slash primary");
	CheckEq(trailing[1], "/srv/configs/as_vip/vip.ini", "trailing slash fallback");

	// No configs directory from the host -> no candidates (the plugin logs that once).
	CHECK(as_vip::ConfigCandidates("").empty());

	// JoinPath edge cases used by the candidates above.
	CheckEq(as_vip::JoinPath("", "vip.ini"), "vip.ini", "JoinPath empty base");
	CheckEq(as_vip::JoinPath("/a/b", ""), "/a/b", "JoinPath empty name");
	CheckEq(as_vip::JoinPath("/a/b", "vip.ini"), "/a/b/vip.ini", "JoinPath plain");
	CheckEq(as_vip::JoinPath("/a/b/", "vip.ini"), "/a/b/vip.ini", "JoinPath trailing slash");
	CheckEq(as_vip::JoinPath("/a/b", "/vip.ini"), "/a/b/vip.ini", "JoinPath leading slash");
}

} // namespace

int main() {
	std::printf("as_vip_tests\n");
	TestParseTable();
	TestParseEscapesAndBom();
	TestParseMalformed();
	TestProdConfig();
	TestLiveConfig();
	TestMissingFile();
	TestSelectGroup();
	TestConfigCandidates();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
