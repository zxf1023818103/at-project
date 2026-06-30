/*
 * fleet_provisioning_config.h — at-project configuration for AWS IoT Fleet Provisioning
 * (component fleetprovisioning_config).
 *
 * Active because fleetprovisioning_config does NOT define
 * FLEET_PROVISIONING_DO_NOT_USE_CUSTOM_CONFIG; anything not overridden here falls back to
 * fleet_provisioning_config_defaults.h (just the assert/log macros).
 *
 * Logging: LogError / LogWarn / LogInfo / LogDebug default to no-ops in
 * fleet_provisioning_config_defaults.h. Map them to the platform console here when wanted, e.g.
 *     #include <stdio.h>
 *     #define LogError( message )  do { printf( "[FleetProv] " ); printf message; printf( "\r\n" ); } while( 0 )
 * (Fleet Provisioning passes the format+args already parenthesised, so `printf message` works.)
 */
#ifndef FLEET_PROVISIONING_CONFIG_H
#define FLEET_PROVISIONING_CONFIG_H

/* Defaults are sufficient; add overrides below. */

#endif /* FLEET_PROVISIONING_CONFIG_H */
