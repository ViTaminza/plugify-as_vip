// as_vip — Plugify port of the Pisex admin_system module "VIP"
// (the original 175-line module, built as addons/admin_modules/as_vip.so).
//
// Behaviour kept identical. The module reads the "VIP" block of vip.ini (permission -> vip group)
// and listens for the admin core's OnAdminConnected event; for the admin who just connected:
//
//   * an "@admin/root" holder is granted the "@admin/root" entry of vip.ini - and nothing when
//     the file has no such entry (the original's loop does not run for root admins; an empty
//     entry like `"@admin/root" ""` is passed through unchanged);
//   * every other admin is granted the group of the first permission he holds, in map order
//     (std::map order, like the original's g_mapGroups).
//
//   VIP_GiveClientVIP(iSlot, 0, group, false) - the original's arguments: permanent for the
//   session, no `vip_users` write.
//
// Port notes (differences to the Metamod original):
//   * IUtilsApi/IAdminApi/IVIPApi become the admin_system/vip consumer wrappers (admin_api.hpp /
//     vip_api.hpp) - the only two APIs this module uses. There is no pisexcompat and no s2sdk
//     call at all: the config is read by the self-contained KV1 reader in vip_groups.hpp, so
//     neither s2sdk_bindings nor a hudmenu binding is linked or exported
//     (docs/PLUGIN_STANDARD.md §7). The manifest declares the two cores as dependencies, so they
//     are loaded before this plugin and the original's "missing interface -> meta unload"
//     branches have no counterpart.
//   * "addons/configs/admin_system/vip.ini" resolves through plg::GetConfigsDir():
//     <configs>/admin_system/vip.ini first (the directory every admin module shares, i.e. the
//     Pisex layout), then <configs>/as_vip/vip.ini (the plugin's own folder, PLUGIN_STANDARD §3).
//   * The original's StartupServer() (entity-system pointers kept in globals, unused by this
//     module) and Unload()'s ConVar_Unregister() (no cvars) are dropped, and so is its per-frame
//     compat poll - the module has no per-frame work, so OnPluginUpdate is not overridden. The
//     admin listener is removed in OnPluginEnd instead.

#include <plg/plugin.hpp>
#include <plugin_export.h>

#include <admin_api.hpp>
#include <vip_api.hpp>

#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include "vip_groups.hpp"

namespace {

// GetLogTag() of the original.
constexpr const char* kLogTag = "VIP";

// The original's g_mapGroups.
as_vip::GroupMap g_mapGroups;

// The original's g_pAdminApi / g_pVIPApi.
admin_system::AdminApi* g_pAdminApi = &admin_system::AdminApi::Get();
vip::IVIPApi* g_pVIPApi = &vip::IVIPApi::Get();

void Log(std::string_view message, plg::Severity severity) {
	if (plg::Log) {
		plg::Log(std::string("[") + kLogTag + "] " + std::string(message), severity,
				 plg::source_location::current());
	}
}

// <configs> as the plugify module reported it; empty when the host exposes no configs directory.
std::string ConfigsDir() {
	try {
		return plg::GetConfigsDir ? std::string(plg::GetConfigsDir()) : std::string();
	} catch (...) {
		return {};
	}
}

// LoadConfig() of the original: it created KeyValues("VIP"), loaded the file and copied every
// value into g_mapGroups (FOR_EACH_VALUE). A missing or malformed file is logged and leaves the map
// empty - the module then never grants VIP, exactly like the original.
//
// The two locations are tried in order. A file that exists but does not parse is reported and the
// next candidate is tried; a file that parses but has no "VIP" block counts as malformed too.
void LoadConfig() {
	const std::string configsDir = ConfigsDir();
	const std::vector<std::string> candidates = as_vip::ConfigCandidates(configsDir);
	if (candidates.empty()) {
		Log("The plugify configs directory is unavailable - VIP will not be granted",
			plg::Severity::Error);
		return;
	}

	std::string lookedAt;
	bool sawFile = false;
	for (const std::string& path : candidates) {
		if (!lookedAt.empty()) {
			lookedAt += ", ";
		}
		lookedAt += path;
		if (!as_vip::FileExists(path)) {
			continue;
		}
		sawFile = true;
		if (as_vip::LoadGroupMapFromFile(path, g_mapGroups)) {
			Log("loaded " + path, plg::Severity::Info);
			return;
		}
		// g_pUtils->ErrorLog("[%s] Failed to load %s", GetLogTag(), pszPath) of the original.
		Log("Failed to load " + path, plg::Severity::Error);
	}

	if (sawFile) {
		Log("Failed to load the \"VIP\" table (looked at " + lookedAt + ")", plg::Severity::Error);
	} else {
		Log("vip.ini not found (looked at " + lookedAt + ") - VIP will not be granted",
			plg::Severity::Error);
	}
}

// OnAdminConnected() of the original.
void OnAdminConnected(int iSlot) {
	const std::string* szGroup = as_vip::SelectGroup(g_mapGroups, [iSlot](const std::string& szPermission) {
		return g_pAdminApi->HasPermission(iSlot, szPermission.c_str());
	});
	if (szGroup != nullptr) {
		// VIP_GiveClientVIP(iSlot, 0, group.c_str(), false) of the original.
		g_pVIPApi->VIP_GiveClientVIP(iSlot, 0, szGroup->c_str(), false);
	}
}

class AsVipPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override {
		try {
			// Never Failed: a plugin that fails OnPluginStart stops every Plugify plugin.
			LoadConfig();
			// The original registered the listener in AllPluginsLoaded; the admin core is a
			// declared dependency, so it is already running here and calls back on every admin
			// connection.
			g_pAdminApi->OnAdminConnected(OnAdminConnected);
			Log("ready", plg::Severity::Info);
		} catch (const std::exception& ex) {
			Log(std::string("OnPluginStart failed: ") + ex.what(), plg::Severity::Error);
		} catch (...) {
			Log("OnPluginStart failed with an unknown exception", plg::Severity::Error);
		}
		return {};
	}

	plg::PluginResult OnPluginEnd() override {
		try {
			g_pAdminApi->OnAdminConnectedUnregister();
		} catch (...) {
			// nothing left to do at unload
		}
		return {};
	}
};

AsVipPlugin g_plugin;

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_plugin)
