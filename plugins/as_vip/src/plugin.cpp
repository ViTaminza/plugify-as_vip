// as_vip — Plugify port of the Pisex admin_system module "VIP"
// (ref/pisex/cs2-admin_system_modules/VIP/as_vip.cpp, 175 lines; addons/admin_modules/as_vip.so
// on the production server).
//
// Behaviour kept identical. The module loads vip.ini (pisex KeyValues; "VIP" block with
// permission -> vip group entries, ref/prod-27016/addons/configs/admin_system/vip.ini) and
// listens for the admin core's OnAdminConnected event; for the admin who just connected:
//
//   * an "@admin/root" holder is granted the "@admin/root" entry of vip.ini - and nothing when
//     the file has no such entry (the original's loop does not run for root admins; the empty
//     group of the production config `"@admin/root" ""` is passed through unchanged);
//   * every other admin is granted the group of the first permission he holds, in file order
//     (std::map order, like the original's g_mapGroups).
//
//   VIP_GiveClientVIP(iSlot, 0, group, false) - the original's arguments: permanent for the
//   session, no `vip_users` write.
//
// Port notes (differences to the Metamod original):
//   * IUtilsApi/IAdminApi/IVIPApi become pisexcompat and the admin_system/vip consumer wrappers
//     (admin_api.hpp / vip_api.hpp). The manifest declares both cores as dependencies, so they
//     are loaded before this plugin and the original's "missing interface -> meta unload"
//     branches have no counterpart.
//   * "addons/configs/admin_system/vip.ini" resolves through pisexcompat
//     (pisex::Init("admin_system")) to <configs>/admin_system/vip.ini - the directory every
//     admin module shares, i.e. the production layout.
//   * The original's StartupServer() (entity-system pointers kept in globals, unused by this
//     module) and Unload()'s ConVar_Unregister() (no cvars) are dropped; the admin listener is
//     removed in OnPluginEnd instead.

#include <pisex/pisex.hpp>

#include <plg/plugin.hpp>
#include <plugin_export.h>

#include <admin_api.hpp>
#include <vip_api.hpp>

#include <chrono>
#include <exception>
#include <string>
#include <string_view>

#include "vip_groups.hpp"

namespace {

// GetLogTag() of the original.
constexpr const char* kLogTag = "VIP";

// LoadConfig() of the original.
constexpr const char* kConfigPath = "addons/configs/admin_system/vip.ini";

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

// LoadConfig(): the original created KeyValues("VIP"), loaded the file and copied every value
// into g_mapGroups (FOR_EACH_VALUE). A missing file is logged and leaves the map empty - the
// module then never grants VIP, exactly like the original.
void LoadConfig() {
	pisex::KeyValues::AutoDelete kv("VIP");
	if (!kv->LoadFromFile(kConfigPath)) {
		// g_pUtils->ErrorLog("[%s] Failed to load %s", GetLogTag(), pszPath) of the original.
		pisex::ErrorLog("[%s] Failed to load %s", kLogTag, kConfigPath);
		return;
	}
	as_vip::LoadGroupMap(*kv, g_mapGroups);
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
			// "admin_system" names the shared <configs>/admin_system config directory.
			pisex::Init("admin_system");
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

	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds) override {
		try {
			pisex::Poll();
		} catch (...) {
			// the compat layer never lets a poll failure escape; nothing else to do here
		}
		return {};
	}

	plg::PluginResult OnPluginEnd() override {
		try {
			g_pAdminApi->OnAdminConnectedUnregister();
			pisex::Shutdown();
		} catch (...) {
			// nothing left to do at unload
		}
		return {};
	}
};

AsVipPlugin g_plugin;

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_plugin)
