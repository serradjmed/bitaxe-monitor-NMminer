#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Firmware
// ---------------------------------------------------------------------------
#define FW_NAME "Bitaxe Monitor"
#define FW_VERSION "1.2.0"

// ---------------------------------------------------------------------------
// Display / board
// ---------------------------------------------------------------------------
#define SCREEN_W 240
#define SCREEN_H 320
#define SCREEN_MAX_DIM 320

#define BOOT_BUTTON_PIN 0

#define LED_R_PIN 4
#define LED_G_PIN 16
#define LED_B_PIN 17
#define LED_ACTIVE_LOW 1
#define LED_BLINK_MS 2000UL

#define MAX_MINERS 8
#define DEFAULT_LED_MODE 3

// XPT2046 touch controller (separate SPI bus from the TFT)
#define TOUCH_CS_PIN 33
#define TOUCH_IRQ 36
#define TOUCH_CLK 25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39

// ---------------------------------------------------------------------------
// Defaults (overridable from the web page)
// ---------------------------------------------------------------------------
#define DEFAULT_BITAXE_IP "192.168.1.100"
#define DEFAULT_POLL_SEC 3
#define DEFAULT_ORIENTATION 0  // 0 = portrait, 1 = landscape
#define DEFAULT_FLIP 0         // 0 = normal, 1 = rotated 180 degrees
#define DEFAULT_BRIGHTNESS 100 // 0..100
#define WIFI_PORTAL_NAME "BitaxeMonitor"
#define WIFI_PORTAL_TIMEOUT 180  // seconds

// ---------------------------------------------------------------------------
// Palette (RGB565)
// ---------------------------------------------------------------------------
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t C_BG_TOP = rgb565(7, 10, 17);
constexpr uint16_t C_BG_BOTTOM = rgb565(13, 18, 28);
constexpr uint16_t C_CARD = rgb565(17, 24, 38);
constexpr uint16_t C_CARD_SOFT = rgb565(22, 31, 47);
constexpr uint16_t C_BORDER = rgb565(30, 42, 61);
constexpr uint16_t C_ORANGE = rgb565(247, 147, 26);
constexpr uint16_t C_ORANGE_LIGHT = rgb565(255, 183, 77);
constexpr uint16_t C_WHITE = rgb565(242, 246, 255);
constexpr uint16_t C_MUTED = rgb565(122, 138, 160);
constexpr uint16_t C_DIM = rgb565(80, 96, 122);
constexpr uint16_t C_CYAN = rgb565(79, 209, 197);
constexpr uint16_t C_VIOLET = rgb565(167, 139, 250);
constexpr uint16_t C_GREEN = rgb565(52, 211, 153);
constexpr uint16_t C_AMBER = rgb565(251, 191, 36);
constexpr uint16_t C_RED = rgb565(248, 113, 113);

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
#define BOOT_WAIT_MS 10000UL
