# as_vip (Plugify)

Admin module granting VIP to admins on connection (port of the Pisex admin_system module 'VIP'): the vip.ini 'VIP' block maps admin permissions to vip groups - '@admin/root' holders get the '@admin/root' group, every other admin the group of the first permission he holds - and the grant goes through the vip core's VIP_GiveClientVIP (permanent for the session, no vip_users write). Requires admin_system and vip.

Plugify port (fork) of **VIP** by Pisex (Metamod (C++)).
Original: https://github.com/Pisex/cs2-admin_system_modules
The code was rewritten for Plugify / s2sdk; behaviour follows the original.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugify dependencies: s2sdk, admin_system, vip, hudmenu (optional)

## Installation
Download `as_vip-1.0.0.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/as_vip`.

`plugins/admin_system`, `plugins/vip` carry only the API headers of the plugins this one uses.

## License
GPL-3.0, see LICENSE.

