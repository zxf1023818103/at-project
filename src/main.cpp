// ===================================================================================
// Shared, platform-NEUTRAL AT-firmware example entry point (every platform compiles this file).
// The only conditionals here are FEATURE gates (AT_PROJECT_*) + one capability flag (AT_SDK_OWNS_MAIN)
// -- NO chip/platform macros (CONFIG_PLATFORM_8721D / BL602 / ...). All platform-specific bring-up
// (Realtek / Bouffalo WiFi, wifi_port HAL, IPv6, event handlers, the app_platform_* /
// app_console_register_platform / app_network_up_start_services overrides) lives in a per-platform PORT
// COMPONENT compiled as its own translation unit: AmebaD -> component/amebad/app_port/amebad_app.cpp
// (a source of the KM4 image); Bouffalo & Ameba-newer -> component/at_app_port/at_app_port.cpp. main()
// drives the shared startup skeleton app_startup_run() (component/app_startup_config).
// ===================================================================================
#include "app_startup.h"   // app_startup_run() / app_startup_run_headless()

// AmebaD + Bouffalo own main(); Ameba-newer's SDK provides main() (its port exports app_pre_example
// instead), so gate on the capability flag AT_SDK_OWNS_MAIN -- not a chip macro.
#ifndef AT_SDK_OWNS_MAIN
extern "C" int main(void)
{
#ifdef AT_PROJECT_QUIC
    app_startup_run_headless();   // console-less self-test image (layout-sensitive)
#else
    app_startup_run();
#endif
    return 0;
}
#endif // !AT_SDK_OWNS_MAIN
