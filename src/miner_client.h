#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "config.h"

struct MinerEntry {
  bool online = false;
  bool everConnected = false;
  bool miningPaused = false;
  bool pausedByMonitor = false;
  double hashRate1m = 0;
  float temp = -1;
  float power = 0;
  char ip[16] = {0};
};

struct MinerData {
  bool online = false;
  bool everConnected = false;
  bool wifiConnected = false;
  bool miningPaused = false;
  bool miningPausedKnown = false;
  bool pausedByMonitor = false;

  double bestDiff = 0;
  double bestSessionDiff = 0;
  double networkDiff = 0;
  double hashRate = 0;
  double hashRate1m = 0;
  float temp = -1;
  float power = 0;

  int sharesAccepted = 0;
  int sharesRejected = 0;
  int blockHeight = 0;
  int blockFound = 0;
  uint32_t uptime = 0;

  int rssi = 0;
  unsigned long lastUpdateMs = 0;
  char deviceIp[16] = {0};

  uint8_t minerCount = 0;
  uint8_t onlineCount = 0;
  uint8_t pausedCount = 0;
  uint8_t ledMode = DEFAULT_LED_MODE;
  uint8_t hotTemp = DEFAULT_HOT_TEMP;
  uint8_t criticalTemp = DEFAULT_CRITICAL_TEMP;
  bool autoPause = DEFAULT_AUTO_PAUSE != 0;
  MinerEntry miners[MAX_MINERS];
};

class MinerClient {
 public:
  void begin();

  bool getData(MinerData& out);

  bool isWifiConnected();
  String bitaxeIp();
  uint16_t pollSec();
  uint8_t ledMode() const { return _ledMode; }
  bool nightEnabled() const { return _nightEnabled; }
  uint8_t nightStart() const { return _nightStart; }
  uint8_t nightEnd() const { return _nightEnd; }
  uint8_t nightBrightness() const { return _nightBrightness; }
  uint16_t screenTimeoutSec() const { return _screenTimeoutSec; }

  void loadSettings();
  uint8_t orientation() const { return _orientation; }
  bool flip() const { return _flip; }
  uint8_t brightness() const { return _brightness; }
  bool consumeDisplayDirty();
  bool consumePageRequest(uint8_t& page);

 private:
  static void taskEntry(void* arg);
  void taskLoop();
  void poll();
  void pollOne(const String& ip, MinerData& out);
  void updateThermalProtection(uint8_t index, unsigned long now);
  bool sendMiningCommand(const String& ip, bool pause);
  MinerData buildAggregate(bool wifi) const;
  void checkResetButton();

  bool applyIpList(const String& raw);
  void resetMinerStates();
  String ipListText(const char* separator) const;

  void saveSettings();
  void savePauseState();
  void setupWiFi();
  void setupWebServer();

  void handleRoot();
  void handleSave();
  void handlePage();
  void handleStatus();
  void handleRestart();
  void handleUpdatePage();
  void handleUpdateUpload();
  void handleUpdateFinish();

  bool updateData(const MinerData& data);
  static double parseDiff(JsonVariant value);

  Preferences _prefs;
  SemaphoreHandle_t _mutex = nullptr;
  TaskHandle_t _task = nullptr;
  WebServer _server{80};

  String _ips[MAX_MINERS];
  uint8_t _ipCount = 1;
  uint8_t _ledMode = DEFAULT_LED_MODE;
  uint16_t _pollSec = DEFAULT_POLL_SEC;
  uint8_t _orientation = DEFAULT_ORIENTATION;
  bool _flip = DEFAULT_FLIP != 0;
  uint8_t _brightness = DEFAULT_BRIGHTNESS;
  bool _nightEnabled = false;
  uint8_t _nightStart = 22;
  uint8_t _nightEnd = 7;
  uint8_t _nightBrightness = 15;
  uint16_t _screenTimeoutSec = 0;
  uint8_t _hotTemp = DEFAULT_HOT_TEMP;
  uint8_t _criticalTemp = DEFAULT_CRITICAL_TEMP;
  bool _autoPause = DEFAULT_AUTO_PAUSE != 0;

  volatile bool _displayDirty = false;
  volatile bool _forcePoll = true;
  volatile uint8_t _requestedPage = 0xFF;
  String _otaError;
  bool _otaStarted = false;
  unsigned long _lastAttemptMs = 0;
  bool _wasOnline = false;

  MinerData _miners[MAX_MINERS];
  bool _pausedByMonitor[MAX_MINERS] = {false};
  unsigned long _pauseCoolSince[MAX_MINERS] = {0};
  unsigned long _lastTempCmdMs[MAX_MINERS] = {0};
  uint32_t _lastBlockFound[MAX_MINERS] = {0};
  bool _blockInit[MAX_MINERS] = {false};
  uint32_t _blockCounter = 0;
  int _lastBlockHeight = 0;

  MinerData _data;
};
