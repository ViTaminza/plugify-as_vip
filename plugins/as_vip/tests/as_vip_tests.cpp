// as_vip unit tests — the engine-free part: the vip.ini ("VIP" block) group map, the
// OnAdminConnected group selection (including its asymmetries), and the real vip.ini files the
// package ships.
//
// Run by ../../test.sh (inside the Steam Runtime image, cwd = build/tests).

#include <plg/plugin.hpp>

#include <pisex/keyvalues.hpp>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "vip_groups.hpp"

// A real plugin gets these from PLUGIFY_PLUGIN; the test leaves them null (only KeyValues and
// the path resolver run here, and the resolver checks the file system, not plg::GetConfigsDir).
namespace plg {
GetBaseDirFn GetBaseDir{nullptr};
GetExtensionsDirFn GetExtensionsDir{nullptr};
GetConfigsDirFn GetConfigsDir{nullptr};
GetDataDirFn GetDataDir{nullptr};
GetLogsDirFn GetLogsDir{nullptr};
GetCacheDirFn GetCacheDir{nullptr};
IsLoadedFn IsLoaded{nullptr};
LogFn Log{nullptr};
BeginZoneFn BeginZone{nullptr};
EndZoneFn EndZone{nullptr};
} // namespace plg

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

// --- the config parser -------------------------------------------------------------------------

void TestParseInMemory() {
	std::printf("-- LoadGroupMap (in-memory)\n");

	// A real vip.ini verbatim, including the empty value of "@admin/root": the entry must still
	// exist in the map.
	pisex::KeyValues kv("VIP");
	const bool ok = kv.LoadFromBuffer(R"("VIP"
{
    "@admin/root"       ""
    "@admin/megavip"    "MEGA"
    "@admin/ultravip"   "ULTRA"
}
)");
	CHECK(ok);

	as_vip::GroupMap map;
	as_vip::LoadGroupMap(kv, map);

	CHECK(map.size() == 3);
	CheckEq(map["@admin/root"], "", "map[@admin/root]");
	CheckEq(map["@admin/megavip"], "MEGA", "map[@admin/megavip]");
	CheckEq(map["@admin/ultravip"], "ULTRA", "map[@admin/ultravip]");

	// The map is std::map: iteration is lexicographic by permission, which is the order the
	// original's g_mapGroups walked and SelectGroup walks.
	std::string order;
	for (const auto& entry : map) {
		order += entry.first + ";";
	}
	CheckEq(order, "@admin/megavip;@admin/root;@admin/ultravip;", "iteration order");
}

// --- the selection logic ------------------------------------------------------------------------

void TestSelectGroup() {
	std::printf("-- SelectGroup\n");

	// The original's four branches, with the file above.
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

// --- the shipped config files -------------------------------------------------------------------

void TestProdConfig() {
	std::printf("-- production vip.ini\n");

	pisex::KeyValues kv("VIP");
	CHECK(kv.LoadFromFile(AS_VIP_TEST_PROD_CONFIG));

	as_vip::GroupMap map;
	as_vip::LoadGroupMap(kv, map);
	CHECK(map.size() == 3);
	CheckEq(map["@admin/root"], "PROVIP", "prod map[@admin/root]");
	CheckEq(map["@admin/vip"], "LITE", "prod map[@admin/vip]");
	CheckEq(map["@admin/warn"], "WARN", "prod map[@admin/warn]");

	CheckEq(Picked(map, {"@admin/root"}), "<PROVIP>", "prod root");
	CheckEq(Picked(map, {"@admin/warn"}), "<WARN>", "prod warn");
}

void TestLiveConfig() {
	std::printf("-- live vip.ini\n");

	pisex::KeyValues kv("VIP");
	CHECK(kv.LoadFromFile(AS_VIP_TEST_LIVE_CONFIG));

	as_vip::GroupMap map;
	as_vip::LoadGroupMap(kv, map);
	CHECK(map.size() == 3);
	// The shipped file maps @admin/root to the empty group: the grant still happens (with an empty
	// group), exactly as the original would have sent it.
	CheckEq(map["@admin/root"], "", "live map[@admin/root]");
	CheckEq(Picked(map, {"@admin/root"}), "<>", "live root");
	CheckEq(Picked(map, {"@admin/ultravip"}), "<ULTRA>", "live ultravip");
}

} // namespace

int main() {
	std::printf("as_vip_tests\n");
	TestParseInMemory();
	TestSelectGroup();
	TestProdConfig();
	TestLiveConfig();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
