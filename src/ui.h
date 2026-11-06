#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include "config.h"
#include "miner_client.h"

class UI {
 public:
  enum class Page : uint8_t {
    Dashboard = 0,
    BestEver = 1,
    BestSession = 2,
    Hashrate = 3,
    Miners = 4,
    Stats = 5,
    History = 6,
  };

  void begin(uint8_t orientation, bool flip, uint8_t brightness);
  void applyDisplaySettings(uint8_t orientation, bool flip, uint8_t brightness);
  void setNightSettings(bool enabled, uint8_t start, uint8_t end, uint8_t nightBrightness,
                        uint16_t timeoutSec);
  void showBoot(const String& title, const String& line1, const String& line2, int progress = -1);
  void drawStatic();
  void update(const MinerData& data);
  void pollTouch();
  void resetRecords();
  void showPage(uint8_t page);

 private:
  struct HashRateText {
    String value;
    String unit;
  };

  static uint16_t blend(uint16_t a, uint16_t b, uint8_t amount);
  static String formatDiff(double value, bool valid);
  static HashRateText formatHashRate(double gh, bool valid);
  static String formatTemp(float value, bool valid, String& unit);
  static uint16_t tempColor(float value);
  static uint8_t rotationFor(uint8_t orientation, bool flip);

  void updateLayout();
  void setBrightness(uint8_t percent);
  void drawBackground();
  void clearBgRect(int x, int y, int w, int h);
  void drawTopBarStatic();
  void drawStatus(bool online, bool wifi, int rssiLevel, uint8_t onlineCount, uint8_t minerCount);
  void drawHeroCardStatic();
  void drawSideCardsStatic();
  void drawBottomCardsStatic();
  void drawPageCard();
  void drawPageDots();

  void drawHeroValue(const String& value);
  void drawSideValue(int x, int y, int w, const String& value, uint16_t color);
  void drawBottomValues(const HashRateText& hr, const String& temp, const String& tempUnit,
                        uint16_t tempColorValue);
  void updateFooter(bool online, bool wifi, const char* ip, unsigned long lastUpdateMs, bool valid);

  void drawCurrentPageStatic();
  void drawDashboardStatic();
  void drawFullPageStatic(Page page);
  void drawMinersPageStatic();
  void drawStatsPageStatic();
  void drawHistoryPageStatic();
  void drawBigValue(const String& value, Page page);
  void drawStatRow(int y, const char* label, const String& value, uint16_t color,
                   const uint8_t* font);
  void drawHistoryGraphs();
  void drawHistoryGraph(int x, int y, int w, int h, bool temp);
  void updateDashboard(const MinerData& data);
  void updateBestPage(const MinerData& data);
  void updateHashratePage(const MinerData& data);
  void updateMinersPage(const MinerData& data);
  void updateStatsPage(const MinerData& data);
  void updateHistoryPage(const MinerData& data);
  static String formatUptime(uint32_t seconds);
  bool isNightNow() const;
  void applyPowerState();

  void drawOfflineAlert(const String& text);
  void drawExclamation(int cx, int top, int height, uint16_t color);

  void drawCelebration(int blockHeight);
  void startCelebration(int blockHeight);
  void startRecordFlash(bool led);
  void drawNewOverlay(bool showText);

  void setLed(bool on);
  void startLedBlink();
  void updateLed();

  void setPage(Page page);
  void nextPage();
  void prevPage();
  int mapTouchX(int raw) const;
  int mapTouchY(int raw) const;

  void setFont(const uint8_t* font, uint16_t color);
  int16_t textWidth(const char* text);
  void drawTextAt(const char* text, int x, int yTop, int h, const uint8_t* font, uint16_t color);
  void drawTextCentered(const char* text, int x, int yTop, int w, int h, const uint8_t* font,
                        uint16_t color);
  void drawTextRight(const char* text, int x, int yTop, int w, int h, const uint8_t* font,
                     uint16_t color);
  void drawTextCenteredAt(const char* text, int cx, int cy, const uint8_t* font, uint16_t color);
  int16_t baselineFor(int yTop, int h);

  TFT_eSPI _tft;
  U8g2_for_TFT_eSPI _u8f;
  XPT2046_Touchscreen _ts{TOUCH_CS_PIN, 255};

  uint16_t _bg[SCREEN_MAX_DIM] = {0};

  uint8_t _orientation = DEFAULT_ORIENTATION;
  bool _flip = DEFAULT_FLIP != 0;
  uint8_t _brightness = DEFAULT_BRIGHTNESS;
  uint8_t _userBrightness = DEFAULT_BRIGHTNESS;
  bool _nightEnabled = false;
  uint8_t _nightStart = 22;
  uint8_t _nightEnd = 7;
  uint8_t _nightBrightness = 15;
  uint16_t _screenTimeoutSec = 0;
  bool _nightActive = false;
  bool _screenOff = false;
  unsigned long _lastTouchMs = 0;
  unsigned long _lastPowerCheckMs = 0;
  uint8_t _rotation = 0;
  int _screenW = SCREEN_W;
  int _screenH = SCREEN_H;
  bool _landscape = false;

  int _heroX = 10, _heroY = 46, _heroW = 220, _heroH = 116;
  int _heroCx = 120, _heroCy = 103;
  int _side1X = 10, _side1Y = 170, _side2X = 125, _side2Y = 170;
  int _sideW = 105, _sideH = 58;
  int _bot1X = 10, _bot1Y = 236, _bot2X = 125, _bot2Y = 236;
  int _botW = 105, _botH = 58;
  int _footerY = 302, _footerH = 14;
  int _statusTextX = 158, _statusTextW = 72, _rssiX = 196;

  Page _page = Page::Dashboard;

  bool _staticDrawn = false;
  bool _forceAll = true;

  bool _lastOnline = false;
  bool _lastWifi = false;
  int _lastRssiLevel = -1;
  uint8_t _lastOnlineCount = 0;
  uint8_t _lastMinerCount = 0;

  String _lastBest;
  String _lastSession;
  String _lastNetwork;
  String _lastHrValue;
  String _lastHrUnit;
  String _lastTemp;
  String _lastTempUnit;
  uint16_t _lastTempColor = 0;
  String _lastBigValue;
  String _lastMinersSig;
  String _lastStatsSig;

  static constexpr uint8_t HISTORY_LEN = 60;
  double _historyHashrate[HISTORY_LEN] = {0};
  float _historyTemp[HISTORY_LEN] = {0};
  uint8_t _historyCount = 0;
  uint8_t _historyIndex = 0;
  unsigned long _lastHistoryMs = 0;
  bool _historyDirty = true;

  String _lastFooterLeft;
  String _lastFooterRight;
  unsigned long _lastFooterMs = 0;

  bool _blockInit = false;
  int _lastBlockFound = 0;
  bool _celebrating = false;
  unsigned long _celebrateUntil = 0;

  bool _recordInit = false;
  double _lastBestDiff = 0;
  double _lastSessionDiff = 0;
  bool _flashing = false;
  uint8_t _flashPhase = 0;
  unsigned long _flashNext = 0;

  bool _ledBlinking = false;
  unsigned long _ledUntil = 0;
  unsigned long _ledNext = 0;
  bool _ledOn = false;

  bool _alertActive = false;
  String _alertText;

  bool _touchWasDown = false;
  bool _suppressTouch = false;
  unsigned long _touchStartMs = 0;
  int _touchStartX = 0;
  int _touchStartY = 0;
  int _touchLastX = 0;
  int _touchLastY = 0;
};
