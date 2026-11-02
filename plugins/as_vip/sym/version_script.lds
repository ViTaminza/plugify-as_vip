{
    global:
        Plugify_*;

        /* admin_system binding slots (defined by the generated consumer bindings, filled by the
           plugify module from the admin_system plugin's exported methods) */
        __admin_system_*;

        /* vip binding slots (defined by the generated vip.cpp, filled by the plugify module from
           the vip plugin's exported methods) */
        __vip_*;

        /* No s2sdk / hudmenu slots: the plugin makes no s2sdk calls and has no menu of its own,
           so it exports no __s2sdk_* / __hudmenu_* globals (docs/PLUGIN_STANDARD.md §7). */

        /* No exported methods: as_vip exposes nothing to other plugins. */

    local: *;
};
