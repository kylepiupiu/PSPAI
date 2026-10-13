#pragma once

// PSP dedicated OPEN Wi-Fi: fixed as agreed. No password.
#define PSP_AP_SSID "wifi4psp"
#define PSP_AP_CHANNEL 1
#define PSP_AP_MAX_CLIENTS 4

// Optional: set your HOME 2.4 GHz router Wi-Fi to enable ESP32 -> Mac Relay.
// Leave blank for standalone ESP32 dashboard + PSP AP compatibility tests.
// Configure only through the USB sketch, NOT through the open hotspot page.
#define UPSTREAM_SSID ""
#define UPSTREAM_PASSWORD ""

// Previous PSPAI Mac Relay was at 192.168.1.2:8088. Check the Mac's CURRENT IP.
// The Mac must be reachable from the upstream network.
#define MAC_RELAY_HOST "192.168.1.2"
#define MAC_RELAY_PORT 8088
#define PSPAI_BRIDGE_PORT 8088

// Only a local test appliance; use an isolated/test upstream network.
// Admin / report tokens are generated on first boot and printed via USB Serial.
