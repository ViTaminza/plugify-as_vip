// as_vip — the engine-free part of the module: the "VIP" config map (admin permission -> vip
// group) and the group selection of the original's OnAdminConnected.
//
// Kept in a header so tests/as_vip_tests.cpp can exercise it (and the shipped vip.ini)
// without the engine.

#pragma once

#include <pisex/keyvalues.hpp>

#include <functional>
#include <map>
#include <string>

namespace as_vip {

// g_mapGroups of the original. std::map on purpose: the non-root branch of OnAdminConnected
// walks the entries in map order (lexicographic by permission string), and the port keeps it.
using GroupMap = std::map<std::string, std::string>;
using PermissionCheck = std::function<bool(const std::string&)>;

// LoadConfig() of the original: FOR_EACH_VALUE over the KeyValues loaded from vip.ini.
// The pisex parser merges the file's single top-level block ("VIP") into the object, so the
// iterated values are the permission entries themselves.
inline void LoadGroupMap(pisex::KeyValues& kv, GroupMap& out) {
	for (pisex::KeyValues* pValue = kv.GetFirstValue(); pValue != nullptr; pValue = pValue->GetNextValue()) {
		out[pValue->GetName()] = pValue->GetString(nullptr);
	}
}

// OnAdminConnected() of the original with the permission lookups passed in: an admin holding
// "@admin/root" gets that entry of the map when it exists, every other admin gets the first
// entry whose permission he holds.
//
// (The is-root test is computed with the same callback, so it and the loop see exactly the
// permissions HasPermission reports.)
//
// Consequences kept:
//   * a root admin without an "@admin/root" entry in vip.ini gets *nothing* - the else-branch
//     does not run for root admins;
//   * the mapped group is passed through unchanged, including an empty one ("@admin/root" "").
// Returns the group to grant, or nullptr when the admin gets none.
inline const std::string* SelectGroup(const GroupMap& groups, const PermissionCheck& hasPermission) {
	if (hasPermission("@admin/root")) {
		const auto it = groups.find("@admin/root");
		return it != groups.end() ? &it->second : nullptr;
	}
	for (const auto& entry : groups) {
		if (hasPermission(entry.first)) {
			return &entry.second;
		}
	}
	return nullptr;
}

} // namespace as_vip
