#include "ui.h"

#include <time.h>

namespace {

constexpr uint8_t BL_LEDC_CHANNEL = 0;
constexpr unsigned long BEST_EVER_BLINK_MS = 500UL;
constexpr uint16_t C_BEST_EVER_BG = rgb565(105, 0, 0);
constexpr uint16_t C_BEST_EVER_RED = rgb565(185, 16, 16);

const uint8_t* const FONT_TINY = u8g2_font_helvB08_tf;
const uint8_t* const FONT_SMALL = u8g2_font_helvB10_tf;
const uint8_t* const FONT_VALUE = u8g2_font_helvB18_tf;
const uint8_t* const FONT_BIG = u8g2_font_helvB24_tf;
const uint8_t* const FONT_HERO = u8g2_font_fub42_tf;
const uint8_t* const FONT_MEGA = u8g2_font_logisoso58_tf;

}  // namespace

uint8_t UI::rotationFor(uint8_t orientation, bool flip) {
  if (orientation > 1) orientation = 0;
  if (orientation == 0) return flip ? 2 : 0;
  return flip ? 3 : 1;
}

void UI::begin(uint8_t orientation, bool flip, uint8_t brightness) {
  _orientation = orientation > 1 ? 0 : orientation;
  _flip = flip;
  _brightness = brightness > 100 ? 100 : brightness;
  _userBrightness = _brightness;
  _lastTouchMs = millis();
  _lastPowerCheckMs = millis();
  _lastHistoryMs = millis();
  _rotation = rotationFor(_orientation, _flip);

  _tft.init();
  _tft.setRotation(_rotation);
  _screenW = _tft.width();
  _screenH = _tft.height();
  _landscape = _screenW > _screenH;
  updateLayout();

  ledcSetup(BL_LEDC_CHANNEL, 5000, 8);
  ledcAttachPin(TFT_BL, BL_LEDC_CHANNEL);
  setBrightness(_brightness);

  _tft.fillScreen(C_BG_TOP);
  _u8f.begin(_tft);
  _u8f.setFontMode(1);
  _u8f.setFontDirection(0);

  SPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, -1);
  _ts.begin();
  _ts.setRotation(_rotation);

  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  setLed(false);

  drawBackground();
}

void UI::applyDisplaySettings(uint8_t orientation, bool flip, uint8_t brightness) {
  _userBrightness = brightness > 100 ? 100 : brightness;
  applyPowerState();

  uint8_t normalized = orientation > 1 ? 0 : orientation;
  uint8_t rotation = rotationFor(normalized, flip);
  if (rotation == _rotation && normalized == _orientation && flip == _flip) return;

  _orientation = normalized;
  _flip = flip;
  _rotation = rotation;
  _tft.setRotation(_rotation);
  _screenW = _tft.width();
  _screenH = _tft.height();
  _landscape = _screenW > _screenH;
  updateLayout();
  _ts.setRotation(_rotation);

  _flashing = false;
  _bestEverActive = false;
  _bestEverPhase = false;
  _bestEverNext = 0;
  _bestEverValue = "";
  _celebrating = false;
  _ledBlinking = false;
  setLed(false);
  _staticDrawn = false;
  _forceAll = true;
  _alertActive = false;
  _alertText = "";
  _lastFooterLeft = "";
  _lastFooterRight = "";
  _lastFooterMs = 0;
  _lastBigValue = "";

  drawCurrentPageStatic();
}

void UI::updateLayout() {
  if (!_landscape) {
    _heroX = 10;
    _heroY = 46;
    _heroW = 220;
    _heroH = 116;
    _heroCx = 120;
    _heroCy = 103;

    _side1X = 10;
    _side1Y = 170;
    _side2X = 125;
    _side2Y = 170;
    _sideW = 105;
    _sideH = 58;

    _bot1X = 10;
    _bot1Y = 236;
    _bot2X = 125;
    _bot2Y = 236;
    _botW = 105;
    _botH = 58;

    _footerY = 302;
    _footerH = 14;
    _statusTextX = 158;
    _statusTextW = 72;
    _rssiX = 196;
  } else {
    _heroX = 10;
    _heroY = 46;
    _heroW = 190;
    _heroH = 112;
    _heroCx = 105;
    _heroCy = 102;

    _side1X = 210;
    _side1Y = 46;
    _side2X = 210;
    _side2Y = 106;
    _sideW = 100;
    _sideH = 54;

    _bot1X = 10;
    _bot1Y = 166;
    _bot2X = 108;
    _bot2Y = 166;
    _botW = 92;
    _botH = 50;

    _footerY = 226;
    _footerH = 12;
    _statusTextX = 238;
    _statusTextW = 72;
    _rssiX = 276;
  }
}

void UI::setBrightness(uint8_t percent) {
  _brightness = percent > 100 ? 100 : percent;
  uint8_t duty = static_cast<uint8_t>((static_cast<uint16_t>(_brightness) * 255U) / 100U);
  ledcWrite(BL_LEDC_CHANNEL, duty);
}

void UI::setNightSettings(bool enabled, uint8_t start, uint8_t end, uint8_t nightBrightness,
                          uint16_t timeoutSec) {
  _nightEnabled = enabled;
  _nightStart = start > 23 ? 0 : start;
  _nightEnd = end > 23 ? 0 : end;
  _nightBrightness = nightBrightness > 100 ? 100 : nightBrightness;
  _screenTimeoutSec = timeoutSec;
  _nightActive = false;
  _screenOff = false;
  _lastTouchMs = millis();
  applyPowerState();
}

bool UI::isNightNow() const {
  if (!_nightEnabled || _nightStart == _nightEnd) return false;
  time_t now = time(nullptr);
  if (now < 1700000000) return false;

  struct tm local;
  gmtime_r(&now, &local);
  int hour = local.tm_hour;
  if (_nightStart < _nightEnd) return hour >= _nightStart && hour < _nightEnd;
  return hour >= _nightStart || hour < _nightEnd;
}

void UI::applyPowerState() {
  unsigned long now = millis();
  _lastPowerCheckMs = now;

  bool night = isNightNow();
  _nightActive = night;
  bool timedOut = !_bestEverActive && _screenTimeoutSec > 0 &&
                  (now - _lastTouchMs) >= static_cast<unsigned long>(_screenTimeoutSec) * 1000UL;
  _screenOff = timedOut;

  uint8_t target = _screenOff ? 0 : (night ? _nightBrightness : _userBrightness);
  if (target != _brightness) setBrightness(target);
}

void UI::showBoot(const String& title, const String& line1, const String& line2, int progress) {
  drawBackground();
  _staticDrawn = false;

  int cx = _screenW / 2;
  int logoY = _landscape ? 30 : 62;
  _tft.fillRoundRect(cx - 25, logoY, 50, 50, 12, C_ORANGE);
  drawTextCenteredAt("B", cx, logoY + 25, FONT_HERO, C_BG_TOP);

  int titleY = _landscape ? 105 : 140;
  drawTextCenteredAt(title.c_str(), cx, titleY, FONT_VALUE, C_WHITE);
  drawTextCenteredAt(line1.c_str(), cx, titleY + 30, FONT_SMALL, C_MUTED);
  if (line2.length() > 0) {
    drawTextCenteredAt(line2.c_str(), cx, titleY + 50, FONT_TINY, C_DIM);
  }

  const int barX = cx - 80;
  const int barY = _landscape ? 180 : 230;
  const int barW = 160;
  const int barH = 6;
  _tft.drawRoundRect(barX, barY, barW, barH, 3, C_BORDER);
  if (progress >= 0) {
    int filled = (barW - 2) * constrain(progress, 0, 100) / 100;
    if (filled > 0) {
      _tft.fillRoundRect(barX + 1, barY + 1, filled, barH - 2, 2, C_ORANGE);
    }
  } else {
    _tft.fillRoundRect(barX + 1, barY + 1, (barW - 2) / 3, barH - 2, 2, C_ORANGE);
  }

  drawTextCenteredAt("v" FW_VERSION, cx, _landscape ? 218 : 300, FONT_TINY, C_DIM);
}

void UI::drawStatic() {
  drawCurrentPageStatic();
}

void UI::drawCurrentPageStatic() {
  drawBackground();
  if (_page == Page::Dashboard) {
    drawDashboardStatic();
  } else if (_page == Page::Miners) {
    drawMinersPageStatic();
  } else if (_page == Page::Stats) {
    drawStatsPageStatic();
  } else if (_page == Page::History) {
    drawHistoryPageStatic();
  } else {
    drawFullPageStatic(_page);
  }
  drawPageDots();
  _staticDrawn = true;
  _forceAll = true;
}

void UI::drawDashboardStatic() {
  drawTopBarStatic();
  drawHeroCardStatic();
  drawSideCardsStatic();
  drawBottomCardsStatic();
}

void UI::update(const MinerData& data) {
  updateLed();
  if (millis() - _lastPowerCheckMs >= 1000UL) applyPowerState();

  bool live = data.online && data.everConnected;

  if (_celebrating) {
    if (millis() < _celebrateUntil) return;
    _celebrating = false;
    _staticDrawn = false;
    _forceAll = true;
  }

  if (live && _recordInit && data.bestDiff > _lastBestDiff) {
    bool sessionRecord = data.bestSessionDiff > _lastSessionDiff;
    _lastBestDiff = data.bestDiff;
    _lastSessionDiff = data.bestSessionDiff;
    bool led = (data.ledMode == 3) || (data.ledMode == 1) ||
               (data.ledMode == 2 && sessionRecord);
    showBestEverRecord(formatDiff(data.bestDiff, true), led);
    return;
  }

  if (_bestEverActive) {
    if (live && _recordInit && data.bestSessionDiff > _lastSessionDiff) {
      _lastSessionDiff = data.bestSessionDiff;
    }
    if (millis() >= _bestEverNext) {
      _bestEverPhase = !_bestEverPhase;
      _bestEverNext = millis() + BEST_EVER_BLINK_MS;
      drawBestEverOverlay();
    }
    return;
  }

  if (_flashing) {
    if (millis() < _flashNext) return;

    if (_flashPhase == 0) {
      drawNewOverlay(false);
      _flashPhase = 1;
      _flashNext = millis() + 400UL;
      return;
    }
    if (_flashPhase == 1) {
      drawNewOverlay(true);
      _flashPhase = 2;
      _flashNext = millis() + 2000UL;
      return;
    }

    _flashing = false;
    _staticDrawn = false;
    _forceAll = true;
  }

  if (!_staticDrawn) {
    drawCurrentPageStatic();
    if (_alertActive) drawOfflineAlert(_alertText);
  }

  bool online = data.online && data.everConnected;
  bool wifi = data.wifiConnected;

  int rssiLevel = 0;
  if (wifi) {
    if (data.rssi > -55) {
      rssiLevel = 4;
    } else if (data.rssi > -65) {
      rssiLevel = 3;
    } else if (data.rssi > -75) {
      rssiLevel = 2;
    } else if (data.rssi > -85) {
      rssiLevel = 1;
    }
  }

  if (_forceAll) {
    _lastOnline = !online;
    _lastWifi = !wifi;
    _lastRssiLevel = -1;
    _lastOnlineCount = 0xFF;
    _lastMinerCount = 0xFF;
    _lastBest = "";
    _lastSession = "";
    _lastNetwork = "";
    _lastHrValue = "";
    _lastHrUnit = "";
    _lastTemp = "";
    _lastTempUnit = "";
    _lastTempColor = 0;
    _lastBigValue = "";
    _lastFooterLeft = "";
    _lastFooterRight = "";
    _lastFooterMs = 0;
    _forceAll = false;
  }

  if (_page == Page::Dashboard &&
      (online != _lastOnline || wifi != _lastWifi || rssiLevel != _lastRssiLevel ||
       data.onlineCount != _lastOnlineCount || data.minerCount != _lastMinerCount)) {
    drawStatus(online, wifi, rssiLevel, data.onlineCount, data.minerCount);
  }
  _lastOnline = online;
  _lastWifi = wifi;
  _lastRssiLevel = rssiLevel;
  _lastOnlineCount = data.onlineCount;
  _lastMinerCount = data.minerCount;

  bool valid = data.everConnected;

  if (valid && millis() - _lastHistoryMs >= 10000UL) {
    _historyHashrate[_historyIndex] = data.hashRate1m > 0 ? data.hashRate1m : data.hashRate;
    _historyTemp[_historyIndex] = data.temp;
    _historyIndex = (_historyIndex + 1) % HISTORY_LEN;
    if (_historyCount < HISTORY_LEN) ++_historyCount;
    _lastHistoryMs = millis();
    _historyDirty = true;
  }

  if (_page == Page::Dashboard && millis() - _lastFooterMs >= 1000UL) {
    updateFooter(online, wifi, data.deviceIp, data.lastUpdateMs, valid);
    _lastFooterMs = millis();
  }

  bool alert = !online && (data.everConnected || millis() > 15000UL);
  String alertText = wifi ? "BITAXE OFFLINE" : "WIFI OFFLINE";
  if (alert != _alertActive || (alert && alertText != _alertText)) {
    _alertActive = alert;
    _alertText = alertText;
    if (alert) {
      drawOfflineAlert(alertText);
    } else {
      _staticDrawn = false;
      _forceAll = true;
      return;
    }
  }
  if (_alertActive) return;

  if (online && valid) {
    if (!_blockInit) {
      _lastBlockFound = data.blockFound;
      _blockInit = true;
    } else if (data.blockFound > _lastBlockFound) {
      _lastBlockFound = data.blockFound;
      startCelebration(data.blockHeight);
      return;
    }
  }

  if (online && valid) {
    if (!_recordInit) {
      _lastBestDiff = data.bestDiff;
      _lastSessionDiff = data.bestSessionDiff;
      _recordInit = true;
    } else {
      bool sessionRecord = data.bestSessionDiff > _lastSessionDiff;
      _lastBestDiff = data.bestDiff;
      _lastSessionDiff = data.bestSessionDiff;
      if (sessionRecord) {
        bool led = (data.ledMode == 3) || (data.ledMode == 2);
        startRecordFlash(led);
        return;
      }
    }
  }

  if (_page == Page::Dashboard) {
    updateDashboard(data);
  } else if (_page == Page::Hashrate) {
    updateHashratePage(data);
  } else if (_page == Page::Miners) {
    updateMinersPage(data);
  } else if (_page == Page::Stats) {
    updateStatsPage(data);
  } else if (_page == Page::History) {
    updateHistoryPage(data);
  } else {
    updateBestPage(data);
  }
}

void UI::updateDashboard(const MinerData& data) {
  bool valid = data.everConnected;

  String best = formatDiff(data.bestDiff, valid);
  if (best != _lastBest) {
    drawHeroValue(best);
    _lastBest = best;
  }

  String session = formatDiff(data.bestSessionDiff, valid);
  if (session != _lastSession) {
    drawSideValue(_side1X + 8, _side1Y + 19, _sideW - 16, session, C_CYAN);
    _lastSession = session;
  }

  String network = formatDiff(data.networkDiff, valid);
  if (network != _lastNetwork) {
    drawSideValue(_side2X + 8, _side2Y + 19, _sideW - 16, network, C_VIOLET);
    _lastNetwork = network;
  }

  double hr = data.hashRate1m > 0 ? data.hashRate1m : data.hashRate;
  HashRateText hrText = formatHashRate(hr, valid);
  String tempUnit;
  String temp = formatTemp(data.temp, valid, tempUnit);
  uint16_t tempColorValue = tempColor(data.temp);

  if (hrText.value != _lastHrValue || hrText.unit != _lastHrUnit || temp != _lastTemp ||
      tempUnit != _lastTempUnit || tempColorValue != _lastTempColor) {
    drawBottomValues(hrText, temp, tempUnit, tempColorValue);
    _lastHrValue = hrText.value;
    _lastHrUnit = hrText.unit;
    _lastTemp = temp;
    _lastTempUnit = tempUnit;
    _lastTempColor = tempColorValue;
  }
}

void UI::updateBestPage(const MinerData& data) {
  double value = (_page == Page::BestEver) ? data.bestDiff : data.bestSessionDiff;
  String text = formatDiff(value, data.everConnected);
  if (text != _lastBigValue) {
    drawBigValue(text, _page);
    _lastBigValue = text;
  }
}

void UI::updateHashratePage(const MinerData& data) {
  double value = data.hashRate1m > 0 ? data.hashRate1m : data.hashRate;
  HashRateText text = formatHashRate(value, data.everConnected);
  String display = text.value;
  if (text.unit.length() > 0) display += text.unit;
  if (display != _lastBigValue) {
    drawBigValue(display, Page::Hashrate);
    _lastBigValue = display;
  }
}

uint16_t UI::blend(uint16_t a, uint16_t b, uint8_t amount) {
  int ar = (a >> 11) & 0x1F;
  int ag = (a >> 5) & 0x3F;
  int ab = a & 0x1F;
  int br = (b >> 11) & 0x1F;
  int bg = (b >> 5) & 0x3F;
  int bb = b & 0x1F;

  int r = ar + ((br - ar) * amount) / 255;
  int g = ag + ((bg - ag) * amount) / 255;
  int bl = ab + ((bb - ab) * amount) / 255;

  return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

String UI::formatDiff(double value, bool valid) {
  if (!valid) return "--";
  if (value < 0) value = 0;
  if (value < 999.5) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", value);
    return String(buf);
  }

  static const char* const units[] = {"K", "M", "G", "T", "P", "E", "Z", "Y"};
  int unit = -1;
  while (value >= 999.5 && unit < 7) {
    value /= 1000.0;
    ++unit;
  }

  char buf[20];
  if (value >= 100.0) {
    snprintf(buf, sizeof(buf), "%.0f%s", value, units[unit]);
  } else if (value >= 10.0) {
    snprintf(buf, sizeof(buf), "%.1f%s", value, units[unit]);
  } else {
    snprintf(buf, sizeof(buf), "%.2f%s", value, units[unit]);
  }
  return String(buf);
}

UI::HashRateText UI::formatHashRate(double gh, bool valid) {
  HashRateText out;
  if (!valid) {
    out.value = "--";
    out.unit = "";
    return out;
  }

  if (gh >= 999.5) {
    double th = gh / 1000.0;
    out.value = (th >= 10.0) ? String(th, 0) : String(th, 2);
    out.unit = "TH/s";
  } else if (gh >= 1.0) {
    out.value = String(gh, 0);
    out.unit = "GH/s";
  } else {
    out.value = "0";
    out.unit = "GH/s";
  }
  return out;
}

String UI::formatTemp(float value, bool valid, String& unit) {
  if (!valid || value < -50.0f || value > 150.0f) {
    unit = "";
    return "--";
  }
  unit = "°C";
  return String(static_cast<int>(roundf(value)));
}

uint16_t UI::tempColor(float value) {
  if (value < 0) return C_MUTED;
  if (value < 60.0f) return C_GREEN;
  if (value < 70.0f) return C_AMBER;
  return C_RED;
}

void UI::drawBackground() {
  for (int y = 0; y < _screenH; y += 2) {
    uint8_t amount = static_cast<uint8_t>((y * 255) / (_screenH - 1));
    uint16_t color = blend(C_BG_TOP, C_BG_BOTTOM, amount);
    _bg[y] = color;
    _tft.drawFastHLine(0, y, _screenW, color);
    if (y + 1 < _screenH) {
      _bg[y + 1] = color;
      _tft.drawFastHLine(0, y + 1, _screenW, color);
    }
  }
}

void UI::clearBgRect(int x, int y, int w, int h) {
  for (int yy = y; yy < y + h; ++yy) {
    if (yy < 0 || yy >= _screenH) continue;
    _tft.drawFastHLine(x, yy, w, _bg[yy]);
  }
}

void UI::drawTopBarStatic() {
  _tft.fillRoundRect(10, 9, 22, 22, 6, C_ORANGE);
  drawTextCenteredAt("B", 21, 20, FONT_VALUE, C_BG_TOP);

  drawTextAt("BITAXE", 38, 8, 16, FONT_SMALL, C_WHITE);
  drawTextAt("LOTTERY MONITOR", 38, 23, 10, FONT_TINY, C_MUTED);
}

void UI::drawStatus(bool online, bool wifi, int rssiLevel, uint8_t onlineCount,
                    uint8_t minerCount) {
  clearBgRect(_rssiX, 10, 34, 18);

  const int baseY = 27;
  for (int i = 0; i < 4; ++i) {
    int h = 5 + i * 3;
    int x = _rssiX + 4 + i * 5;
    uint16_t color = C_DIM;
    if (wifi && i < rssiLevel) color = C_GREEN;
    _tft.fillRect(x, baseY - h, 3, h, color);
  }

  clearBgRect(_statusTextX, 28, _statusTextW, 10);

  String text;
  if (!wifi) {
    text = "NO WIFI";
  } else if (minerCount > 1) {
    text = String(onlineCount) + "/" + String(minerCount);
  } else {
    text = online ? "ONLINE" : "NO MINER";
  }
  uint16_t color = (wifi && onlineCount > 0) ? C_GREEN : (wifi ? C_AMBER : C_RED);
  drawTextRight(text.c_str(), _statusTextX, 28, _statusTextW, 10, FONT_TINY, color);
}

void UI::drawHeroCardStatic() {
  for (int i = 8; i >= 1; --i) {
    uint16_t color = blend(_bg[_heroY], C_ORANGE, static_cast<uint8_t>(8 + (9 - i) * 3));
    _tft.fillRoundRect(_heroX - i, _heroY - i, _heroW + i * 2, _heroH + i * 2, 14 + i, color);
  }

  _tft.fillRoundRect(_heroX, _heroY, _heroW, _heroH, 14, C_CARD);
  _tft.drawRoundRect(_heroX, _heroY, _heroW, _heroH, 14, C_BORDER);
  _tft.fillRoundRect(_heroX + 1, _heroY + 1, _heroW - 2, 4, 3, C_ORANGE);

  drawTextCentered("BEST DIFF EVER", _heroX, _heroY + 8, _heroW, 10, FONT_TINY, C_MUTED);
  _tft.fillRoundRect(_heroCx - 24, _heroY + 94, 48, 2, 1, blend(C_CARD, C_ORANGE, 80));
  drawTextCentered("SHARE DIFFICULTY", _heroX, _heroY + 102, _heroW, 10, FONT_TINY, C_DIM);
}

void UI::drawSideCardsStatic() {
  struct Side {
    int x;
    int y;
    uint16_t accent;
    const char* label;
    const char* sub;
  };

  const Side sides[2] = {
      {_side1X, _side1Y, C_CYAN, "BEST SESSION", "since restart"},
      {_side2X, _side2Y, C_VIOLET, "NETWORK", "current diff"},
  };

  for (const Side& side : sides) {
    _tft.fillRoundRect(side.x, side.y, _sideW, _sideH, 12, C_CARD);
    _tft.drawRoundRect(side.x, side.y, _sideW, _sideH, 12, C_BORDER);
    _tft.fillRoundRect(side.x + 1, side.y + 1, _sideW - 2, 3, 2, side.accent);
    drawTextAt(side.label, side.x + 8, side.y + 7, 10, FONT_TINY, C_MUTED);
    drawTextAt(side.sub, side.x + 8, side.y + (_landscape ? 43 : 46), 8, FONT_TINY, C_DIM);
  }
}

void UI::drawBottomCardsStatic() {
  _tft.fillRoundRect(_bot1X, _bot1Y, _botW, _botH, 12, C_CARD);
  _tft.drawRoundRect(_bot1X, _bot1Y, _botW, _botH, 12, C_BORDER);
  _tft.fillRoundRect(_bot1X + 1, _bot1Y + 1, _botW - 2, 3, 2, C_GREEN);

  int ix = _bot1X + 8;
  int iy = _bot1Y + 7;
  _tft.drawLine(ix, iy + 9, ix + 4, iy + 4, C_GREEN);
  _tft.drawLine(ix + 4, iy + 4, ix + 8, iy + 7, C_GREEN);
  _tft.drawLine(ix + 8, iy + 7, ix + 12, iy, C_GREEN);
  drawTextAt("HASHRATE", _bot1X + 24, _bot1Y + 7, 10, FONT_TINY, C_MUTED);

  _tft.fillRoundRect(_bot2X, _bot2Y, _botW, _botH, 12, C_CARD);
  _tft.drawRoundRect(_bot2X, _bot2Y, _botW, _botH, 12, C_BORDER);
  _tft.fillRoundRect(_bot2X + 1, _bot2Y + 1, _botW - 2, 3, 2, C_AMBER);

  int tx = _bot2X + 9;
  int ty = _bot2Y + 4;
  _tft.drawRoundRect(tx, ty, 5, 10, 2, C_AMBER);
  _tft.fillCircle(tx + 2, ty + 12, 4, C_AMBER);
  _tft.fillRect(tx + 1, ty + 5, 3, 7, C_AMBER);
  drawTextAt("ASIC TEMP", _bot2X + 23, _bot2Y + 7, 10, FONT_TINY, C_MUTED);

  if (_landscape) drawPageCard();
}

void UI::drawPageCard() {
  int x = _side1X;
  int y = _bot1Y;
  int w = _sideW;
  int h = _botH;

  _tft.fillRoundRect(x, y, w, h, 12, C_CARD);
  _tft.drawRoundRect(x, y, w, h, 12, C_BORDER);
  _tft.fillRoundRect(x + 1, y + 1, w - 2, 3, 2, C_ORANGE);

  drawTextAt("PAGE", x + 8, y + 6, 10, FONT_TINY, C_MUTED);
  String number = String(static_cast<int>(_page) + 1) + " / 7";
  drawTextAt(number.c_str(), x + 8, y + 18, 20, FONT_VALUE, C_ORANGE_LIGHT);
  drawTextAt("TAP", x + 8, y + 40, 8, FONT_TINY, C_DIM);
}

void UI::drawPageDots() {
  int y = (_page == Page::Dashboard) ? (_landscape ? 220 : 298) : (_landscape ? 232 : 306);
  int r = (_page == Page::Dashboard) ? 2 : 3;
  int cx = _screenW / 2;
  const int count = 7;
  int span = (count - 1) * 10;
  uint16_t active = C_ORANGE;
  if (_page == Page::Hashrate || _page == Page::Miners) {
    active = C_GREEN;
  } else if (_page == Page::Stats) {
    active = C_VIOLET;
  } else if (_page == Page::History) {
    active = C_AMBER;
  } else if (_page == Page::BestSession) {
    active = C_CYAN;
  }

  clearBgRect(cx - span / 2 - r - 2, y - r - 1, span + r * 2 + 4, r * 2 + 2);
  for (int i = 0; i < count; ++i) {
    int x = cx - span / 2 + i * 10;
    _tft.fillCircle(x, y, r, i == static_cast<int>(_page) ? active : C_DIM);
  }
}

void UI::drawHeroValue(const String& value) {
  _tft.fillRect(_heroX + 6, _heroY + 20, _heroW - 12, 74, C_CARD);

  if (value == "--") {
    drawTextCenteredAt(value.c_str(), _heroCx, _heroCy, FONT_HERO, C_MUTED);
    return;
  }

  setFont(FONT_HERO, C_WHITE);
  int width = textWidth(value.c_str());
  const uint8_t* font = (width <= _heroW - 20) ? FONT_HERO : FONT_BIG;

  uint16_t glow = blend(C_CARD, C_ORANGE, 90);
  drawTextCenteredAt(value.c_str(), _heroCx + 1, _heroCy, font, glow);
  drawTextCenteredAt(value.c_str(), _heroCx - 1, _heroCy, font, glow);
  drawTextCenteredAt(value.c_str(), _heroCx, _heroCy + 1, font, glow);
  drawTextCenteredAt(value.c_str(), _heroCx, _heroCy - 1, font, glow);
  drawTextCenteredAt(value.c_str(), _heroCx, _heroCy, font, C_ORANGE_LIGHT);
}

void UI::drawSideValue(int x, int y, int w, const String& value, uint16_t color) {
  _tft.fillRect(x, y, w, 23, C_CARD);
  drawTextAt(value.c_str(), x, y, 23, FONT_VALUE, color);
}

void UI::drawBottomValues(const HashRateText& hr, const String& temp, const String& tempUnit,
                          uint16_t tempColorValue) {
  const int leftX = _bot1X + 8;
  const int rightX = _bot2X + 8;
  const int valueY = _bot1Y + (_landscape ? 20 : 22);
  const int valueW = _botW - 16;
  const int valueH = 22;

  _tft.fillRect(leftX, valueY, valueW, valueH, C_CARD);
  _tft.fillRect(rightX, valueY, valueW, valueH, C_CARD);

  setFont(FONT_VALUE, C_WHITE);
  int hrWidth = _u8f.getUTF8Width(hr.value.c_str());
  int hrUnitWidth = 0;
  if (hr.unit.length() > 0) {
    setFont(FONT_SMALL, C_MUTED);
    hrUnitWidth = _u8f.getUTF8Width(hr.unit.c_str());
  }
  bool hrFits = hrWidth + (hr.unit.length() > 0 ? 3 + hrUnitWidth : 0) <= valueW;
  const uint8_t* hrFont = hrFits ? FONT_VALUE : FONT_SMALL;

  setFont(hrFont, C_WHITE);
  int hrAscent = _u8f.getFontAscent();
  int hrBaseline = valueY + (valueH + hrAscent) / 2;
  _u8f.drawUTF8(leftX, hrBaseline, hr.value.c_str());
  if (hr.unit.length() > 0) {
    int drawnWidth = _u8f.getUTF8Width(hr.value.c_str());
    setFont(FONT_SMALL, C_MUTED);
    _u8f.drawUTF8(leftX + drawnWidth + 3, hrBaseline, hr.unit.c_str());
  }

  setFont(FONT_VALUE, tempColorValue);
  int tempWidth = _u8f.getUTF8Width(temp.c_str());
  int tempUnitWidth = 0;
  if (tempUnit.length() > 0) {
    setFont(FONT_SMALL, C_MUTED);
    tempUnitWidth = _u8f.getUTF8Width(tempUnit.c_str());
  }
  bool tempFits = tempWidth + (tempUnit.length() > 0 ? 3 + tempUnitWidth : 0) <= valueW;
  const uint8_t* tempFont = tempFits ? FONT_VALUE : FONT_SMALL;

  setFont(tempFont, tempColorValue);
  int tempAscent = _u8f.getFontAscent();
  int tempBaseline = valueY + (valueH + tempAscent) / 2;
  _u8f.drawUTF8(rightX, tempBaseline, temp.c_str());
  if (tempUnit.length() > 0) {
    int drawnWidth = _u8f.getUTF8Width(temp.c_str());
    setFont(FONT_SMALL, C_MUTED);
    _u8f.drawUTF8(rightX + drawnWidth + 3, tempBaseline, tempUnit.c_str());
  }
}

void UI::updateFooter(bool online, bool wifi, const char* ip, unsigned long lastUpdateMs, bool valid) {
  String left;
  uint16_t color;

  if (!valid) {
    if (millis() > 15000UL) {
      left = wifi ? "MINER OFFLINE" : "WIFI OFFLINE";
      color = C_RED;
    } else {
      left = "CONNECTING...";
      color = C_AMBER;
    }
  } else if (!online) {
    left = wifi ? "MINER OFFLINE" : "WIFI OFFLINE";
    color = C_RED;
  } else {
    unsigned long age = (millis() - lastUpdateMs) / 1000UL;
    if (age < 1) {
      left = "UPDATED <1s";
    } else if (age < 60) {
      left = "UPDATED " + String(age) + "s";
    } else {
      left = "UPDATED " + String(age / 60) + "m";
    }
    color = C_DIM;
  }

  String right = (ip != nullptr && ip[0] != 0) ? String(ip) : String("---.---.---.---");
  if (left == _lastFooterLeft && right == _lastFooterRight) return;

  int footerW = _screenW - 20;
  clearBgRect(10, _footerY, footerW, _footerH);
  drawTextAt(left.c_str(), 10, _footerY, _footerH, FONT_TINY, color);
  drawTextRight(right.c_str(), 10, _footerY, footerW, _footerH, FONT_TINY, C_DIM);

  _lastFooterLeft = left;
  _lastFooterRight = right;
}

void UI::drawMinersPageStatic() {
  int x = 10;
  int headerY = _landscape ? 40 : 44;
  int w = _screenW - 20;
  drawTextAt("MINERS", x, headerY, 12, FONT_SMALL, C_MUTED);
  _tft.drawFastHLine(x, headerY + 18, w, C_BORDER);
}

void UI::updateMinersPage(const MinerData& data) {
  String sig = String(data.minerCount) + ":" + String(data.onlineCount);
  for (uint8_t i = 0; i < data.minerCount; ++i) {
    const MinerEntry& e = data.miners[i];
    sig += "|";
    sig += e.online ? "1" : "0";
    sig += String(e.hashRate1m, 1);
    sig += String(e.temp, 1);
    sig += String(e.power, 1);
  }
  if (sig == _lastMinersSig) return;
  _lastMinersSig = sig;

  int x = 10;
  int w = _screenW - 20;
  int headerY = _landscape ? 40 : 44;
  int start = _landscape ? 62 : 64;
  int rowH = _landscape ? 19 : 24;
  int valueY = _landscape ? 9 : 12;

  String onlineText = String(data.onlineCount) + "/" + String(data.minerCount) + " ONLINE";
  drawTextRight(onlineText.c_str(), x, headerY, w, 12, FONT_SMALL,
                data.onlineCount > 0 ? C_GREEN : C_RED);

  for (uint8_t i = 0; i < MAX_MINERS; ++i) {
    int rowY = start + i * rowH;
    if (rowY + rowH > _screenH - 18) break;

    clearBgRect(x, rowY, w, rowH - 1);
    _tft.drawFastHLine(x, rowY + rowH - 1, w, C_BORDER);

    if (i >= data.minerCount) {
      drawTextAt("--", x + 6, rowY + 1, 10, FONT_TINY, C_DIM);
      continue;
    }

    const MinerEntry& e = data.miners[i];
    uint16_t color = e.online ? C_GREEN : (e.everConnected ? C_RED : C_DIM);
    _tft.fillCircle(x + 6, rowY + rowH / 2, 3, color);
    drawTextAt(e.ip, x + 14, rowY + 1, 10, FONT_TINY, e.online ? C_WHITE : C_DIM);

    String values;
    if (!e.everConnected) {
      values = "NOT CONNECTED";
    } else if (!e.online) {
      values = "OFFLINE";
    } else {
      HashRateText hr = formatHashRate(e.hashRate1m, true);
      values = hr.value + hr.unit + "  ";
      if (e.temp >= -50.0f && e.temp <= 150.0f) {
        values += String(static_cast<int>(roundf(e.temp))) + "C  ";
      }
      values += String(static_cast<int>(roundf(e.power))) + "W";
    }
    drawTextRight(values.c_str(), x + 14, rowY + valueY, w - 18, 10, FONT_TINY,
                  e.online ? C_MUTED : color);
  }
}

void UI::drawStatRow(int y, const char* label, const String& value, uint16_t color,
                     const uint8_t* font) {
  int x = 10;
  int w = _screenW - 20;
  int h = _landscape ? 30 : 38;
  int valueY = _landscape ? 10 : 13;
  int valueH = _landscape ? 18 : 22;

  clearBgRect(x, y, w, h);
  _tft.drawFastHLine(x, y + h - 1, w, C_BORDER);
  drawTextAt(label, x + 4, y + 2, 10, FONT_TINY, C_MUTED);
  drawTextRight(value.c_str(), x + 4, y + valueY, w - 8, valueH, font, color);
}

void UI::drawStatsPageStatic() {
  int x = 10;
  int headerY = _landscape ? 40 : 44;
  drawTextAt("POWER & SHARES", x, headerY, 12, FONT_SMALL, C_MUTED);
  _tft.drawFastHLine(x, headerY + 18, _screenW - 20, C_BORDER);

  int y = _landscape ? 62 : 66;
  int rowH = _landscape ? 30 : 38;
  drawStatRow(y, "TOTAL POWER", "--", C_DIM, FONT_VALUE);
  drawStatRow(y + rowH, "EFFICIENCY", "--", C_DIM, FONT_VALUE);
  drawStatRow(y + rowH * 2, "SHARES ACCEPTED", "--", C_DIM, FONT_VALUE);
  drawStatRow(y + rowH * 3, "SHARES REJECTED", "--", C_DIM, FONT_VALUE);
  drawStatRow(y + rowH * 4, "UPTIME", "--", C_DIM, FONT_VALUE);
}

void UI::updateStatsPage(const MinerData& data) {
  double hr = data.hashRate1m > 0 ? data.hashRate1m : data.hashRate;
  String power = data.everConnected ? String(data.power, 0) + " W" : "--";
  String efficiency = "--";
  if (data.everConnected && hr >= 1000.0 && data.power > 0) {
    efficiency = String(data.power / (hr / 1000.0), 1) + " W/TH";
  }
  String accepted = data.everConnected ? String(data.sharesAccepted) : "--";
  String rejected = data.everConnected ? String(data.sharesRejected) : "--";
  String uptime = data.everConnected ? formatUptime(data.uptime) : "--";
  String sig = power + "|" + efficiency + "|" + accepted + "|" + rejected + "|" + uptime;
  if (sig == _lastStatsSig) return;
  _lastStatsSig = sig;

  int y = _landscape ? 62 : 66;
  int rowH = _landscape ? 30 : 38;
  drawStatRow(y, "TOTAL POWER", power, C_GREEN, FONT_VALUE);
  drawStatRow(y + rowH, "EFFICIENCY", efficiency, C_CYAN, FONT_VALUE);
  drawStatRow(y + rowH * 2, "SHARES ACCEPTED", accepted, C_GREEN, FONT_VALUE);
  drawStatRow(y + rowH * 3, "SHARES REJECTED", rejected, C_AMBER, FONT_VALUE);
  drawStatRow(y + rowH * 4, "UPTIME", uptime, C_VIOLET, FONT_VALUE);
}

String UI::formatUptime(uint32_t seconds) {
  if (seconds == 0) return "--";
  uint32_t days = seconds / 86400UL;
  uint32_t hours = (seconds % 86400UL) / 3600UL;
  uint32_t minutes = (seconds % 3600UL) / 60UL;
  if (days > 0) return String(days) + "d " + String(hours) + "h";
  if (hours > 0) return String(hours) + "h " + String(minutes) + "m";
  if (minutes > 0) return String(minutes) + "m";
  return String(seconds) + "s";
}

void UI::drawHistoryPageStatic() {
  int x = 10;
  int headerY = _landscape ? 40 : 44;
  int w = _screenW - 20;
  int g1y = 62;
  int g2y = _landscape ? 146 : 176;
  int gh = _landscape ? 76 : 100;

  drawTextAt("HISTORY", x, headerY, 12, FONT_SMALL, C_MUTED);
  _tft.drawFastHLine(x, headerY + 18, w, C_BORDER);

  _tft.fillRoundRect(x, g1y, w, gh, 10, C_CARD);
  _tft.drawRoundRect(x, g1y, w, gh, 10, C_BORDER);
  drawTextAt("HASHRATE", x + 6, g1y + 4, 10, FONT_TINY, C_GREEN);

  _tft.fillRoundRect(x, g2y, w, gh, 10, C_CARD);
  _tft.drawRoundRect(x, g2y, w, gh, 10, C_BORDER);
  drawTextAt("TEMPERATURE", x + 6, g2y + 4, 10, FONT_TINY, C_ORANGE);

  drawHistoryGraphs();
  _historyDirty = false;
}

void UI::drawHistoryGraph(int x, int y, int w, int h, bool temp) {
  _tft.fillRect(x, y, w, h, C_CARD);
  if (_historyCount == 0) {
    drawTextCentered("--", x, y, w, h, FONT_SMALL, C_DIM);
    return;
  }

  double minV = 1e30;
  double maxV = -1e30;
  int validCount = 0;
  for (uint8_t i = 0; i < _historyCount; ++i) {
    uint8_t idx = (_historyIndex + HISTORY_LEN - _historyCount + i) % HISTORY_LEN;
    double v = temp ? _historyTemp[idx] : _historyHashrate[idx];
    if (temp && (v < -50.0 || v > 150.0)) continue;
    if (!temp && v < 0.0) continue;
    if (v < minV) minV = v;
    if (v > maxV) maxV = v;
    ++validCount;
  }

  if (validCount == 0) {
    drawTextCentered("--", x, y, w, h, FONT_SMALL, C_DIM);
    return;
  }
  if (maxV - minV < 0.001) {
    maxV += temp ? 1.0 : 0.5;
    minV = max(0.0, minV - (temp ? 1.0 : 0.5));
  }

  for (int g = 1; g < 4; ++g) {
    _tft.drawFastHLine(x, y + (g * h) / 4, w, C_BORDER);
  }

  int prevX = -1;
  int prevY = -1;
  int point = 0;
  uint16_t color = temp ? C_ORANGE : C_GREEN;
  for (uint8_t i = 0; i < _historyCount; ++i) {
    uint8_t idx = (_historyIndex + HISTORY_LEN - _historyCount + i) % HISTORY_LEN;
    double v = temp ? _historyTemp[idx] : _historyHashrate[idx];
    if (temp && (v < -50.0 || v > 150.0)) continue;
    if (!temp && v < 0.0) continue;

    int px = x + (validCount > 1 ? (point * (w - 1)) / (validCount - 1) : w / 2);
    int py = y + h - 1 - static_cast<int>((v - minV) * (h - 1) / (maxV - minV));
    if (prevX >= 0) {
      _tft.drawLine(prevX, prevY, px, py, color);
    } else {
      _tft.fillCircle(px, py, 2, color);
    }
    prevX = px;
    prevY = py;
    ++point;
  }

  String maxText;
  String minText;
  if (temp) {
    maxText = String(static_cast<int>(roundf(maxV))) + "C";
    minText = String(static_cast<int>(roundf(minV))) + "C";
  } else {
    HashRateText maxHr = formatHashRate(maxV, true);
    HashRateText minHr = formatHashRate(minV, true);
    maxText = maxHr.value + maxHr.unit;
    minText = minHr.value + minHr.unit;
  }
  drawTextRight(maxText.c_str(), x, y + 2, w, 10, FONT_TINY, C_DIM);
  drawTextRight(minText.c_str(), x, y + h - 10, w, 10, FONT_TINY, C_DIM);
}

void UI::drawHistoryGraphs() {
  int x = 10;
  int w = _screenW - 20;
  int g1y = 62;
  int g2y = _landscape ? 146 : 176;
  int gh = _landscape ? 76 : 100;
  drawHistoryGraph(x + 2, g1y + 16, w - 4, gh - 20, false);
  drawHistoryGraph(x + 2, g2y + 16, w - 4, gh - 20, true);
}

void UI::updateHistoryPage(const MinerData&) {
  if (!_historyDirty) return;
  _historyDirty = false;
  drawHistoryGraphs();
}

void UI::drawFullPageStatic(Page page) {
  int x = 10;
  int y = _landscape ? 42 : 46;
  int w = _screenW - 20;
  int h = _landscape ? 166 : 234;
  uint16_t accent = (page == Page::Hashrate)
                        ? C_GREEN
                        : ((page == Page::BestSession) ? C_CYAN : C_ORANGE);

  for (int i = 8; i >= 1; --i) {
    uint16_t color = blend(_bg[y], accent, static_cast<uint8_t>(8 + (9 - i) * 3));
    _tft.fillRoundRect(x - i, y - i, w + i * 2, h + i * 2, 16 + i, color);
  }

  _tft.fillRoundRect(x, y, w, h, 16, C_CARD);
  _tft.drawRoundRect(x, y, w, h, 16, C_BORDER);
  _tft.fillRoundRect(x + 1, y + 1, w - 2, 4, 3, accent);

  const char* label = (page == Page::Hashrate)
                          ? "GLOBAL HASHRATE"
                          : ((page == Page::BestEver) ? "BEST DIFF EVER" : "BEST SESSION");
  const char* sub = (page == Page::Hashrate)
                        ? "ALL MINERS - 1 MIN AVG"
                        : ((page == Page::BestEver) ? "SHARE DIFFICULTY" : "SINCE RESTART");
  drawTextCentered(label, x, _landscape ? y + 10 : y + 12, w, 10, FONT_SMALL, C_MUTED);
  drawTextCentered(sub, x, _landscape ? y + 140 : y + 206, w, 10, FONT_SMALL, C_DIM);

  drawTextCentered("TAP TO CHANGE", 0, _landscape ? 214 : 286, _screenW, 10, FONT_TINY,
                   C_DIM);
}

void UI::drawBigValue(const String& value, Page page) {
  int x = 10;
  int y = _landscape ? 42 : 46;
  int w = _screenW - 20;
  int valueY = _landscape ? y + 34 : y + 54;
  int valueH = _landscape ? 96 : 140;
  int cx = _screenW / 2;
  int cy = valueY + valueH / 2;
  int availW = w - 4;

  _tft.fillRect(x + 6, valueY, w - 12, valueH, C_CARD);

  uint16_t accent = (page == Page::Hashrate)
                        ? C_GREEN
                        : ((page == Page::BestSession) ? C_CYAN : C_ORANGE);
  uint16_t mainColor = (value == "--")
                           ? C_MUTED
                           : ((page == Page::Hashrate)
                                  ? C_GREEN
                                  : ((page == Page::BestSession) ? C_CYAN : C_ORANGE_LIGHT));
  uint16_t glow = blend(C_CARD, accent, 90);

  String number = value;
  String suffix;
  for (int i = 0; i < static_cast<int>(value.length()); ++i) {
    char c = value.charAt(i);
    if (!((c >= '0' && c <= '9') || c == '.' || c == '-')) {
      number = value.substring(0, i);
      suffix = value.substring(i);
      break;
    }
  }

  _tft.setTextDatum(ML_DATUM);
  _tft.setTextFont(8);
  _tft.setTextSize(1);

  int numberW = _tft.textWidth(number.c_str());
  int gap = suffix.length() > 0 ? 3 : 0;
  const uint8_t* suffixFont = FONT_HERO;
  int suffixW = 0;

  if (suffix.length() > 0) {
    setFont(suffixFont, mainColor);
    suffixW = textWidth(suffix.c_str());
    if (numberW + gap + suffixW > availW) {
      suffixFont = FONT_BIG;
      setFont(suffixFont, mainColor);
      suffixW = textWidth(suffix.c_str());
    }
    if (numberW + gap + suffixW > availW) {
      suffixFont = FONT_VALUE;
      setFont(suffixFont, mainColor);
      suffixW = textWidth(suffix.c_str());
    }
    if (numberW + gap + suffixW > availW) {
      suffixFont = FONT_SMALL;
      setFont(suffixFont, mainColor);
      suffixW = textWidth(suffix.c_str());
    }
  }

  int totalW = numberW + gap + suffixW;
  if (totalW > availW) {
    drawTextCenteredAt(value.c_str(), cx, cy, FONT_MEGA, mainColor);
    return;
  }

  int startX = cx - totalW / 2;

  if (value != "--") {
    _tft.setTextColor(glow);
    _tft.drawString(number, startX + 2, cy);
    _tft.drawString(number, startX - 2, cy);
    _tft.drawString(number, startX, cy + 2);
    _tft.drawString(number, startX, cy - 2);
  }

  _tft.setTextColor(mainColor);
  _tft.drawString(number, startX, cy);

  if (suffix.length() > 0) {
    int suffixCx = startX + numberW + gap + suffixW / 2;
    if (value != "--") {
      drawTextCenteredAt(suffix.c_str(), suffixCx + 2, cy + 12, suffixFont, glow);
      drawTextCenteredAt(suffix.c_str(), suffixCx - 2, cy + 12, suffixFont, glow);
      drawTextCenteredAt(suffix.c_str(), suffixCx, cy + 14, suffixFont, glow);
      drawTextCenteredAt(suffix.c_str(), suffixCx, cy + 10, suffixFont, glow);
    }
    drawTextCenteredAt(suffix.c_str(), suffixCx, cy + 12, suffixFont, mainColor);
  }
}

void UI::drawOfflineAlert(const String& text) {
  if (_page == Page::Dashboard) {
    _tft.fillRect(_heroX + 6, _heroY + 20, _heroW - 12, 74, C_CARD);
    drawExclamation(_heroCx, _heroY + 20, 74, C_RED);

    _tft.fillRect(_heroX + 6, _heroY + 102, _heroW - 12, 10, C_CARD);
    drawTextCentered(text.c_str(), _heroX, _heroY + 102, _heroW, 10, FONT_TINY, C_RED);
    _tft.drawRoundRect(_heroX, _heroY, _heroW, _heroH, 14, C_RED);
    _tft.fillRoundRect(_heroX + 1, _heroY + 1, _heroW - 2, 4, 3, C_RED);
    return;
  }

  int x = 10;
  int y = _landscape ? 42 : 46;
  int w = _screenW - 20;
  int h = _landscape ? 166 : 234;
  int valueY = _landscape ? y + 34 : y + 54;
  int valueH = _landscape ? 96 : 140;
  int subY = _landscape ? y + 140 : y + 206;

  _tft.fillRect(x + 6, valueY, w - 12, valueH, C_CARD);
  drawExclamation(_screenW / 2, valueY, valueH, C_RED);

  _tft.fillRect(x + 6, subY - 2, w - 12, 14, C_CARD);
  drawTextCentered(text.c_str(), x, subY, w, 10, FONT_TINY, C_RED);
  _tft.drawRoundRect(x, y, w, h, 16, C_RED);
  _tft.fillRoundRect(x + 1, y + 1, w - 2, 4, 3, C_RED);
}

void UI::drawExclamation(int cx, int top, int height, uint16_t color) {
  int barW = max(10, (height * 22) / 100);
  int dotR = max(5, (barW * 55) / 100);
  int gap = max(3, (height * 8) / 100);
  int barH = height - dotR * 2 - gap;
  if (barH < barW) barH = barW;

  int barX = cx - barW / 2;
  int barY = top;
  int dotY = barY + barH + gap + dotR;
  uint16_t glow = blend(C_CARD, color, 90);

  for (int dx = -2; dx <= 2; dx += 2) {
    for (int dy = -2; dy <= 2; dy += 2) {
      if (dx == 0 && dy == 0) continue;
      _tft.fillRoundRect(barX + dx, barY + dy, barW, barH, barW / 2, glow);
      _tft.fillCircle(cx + dx, dotY + dy, dotR, glow);
    }
  }

  _tft.fillRoundRect(barX, barY, barW, barH, barW / 2, color);
  _tft.fillCircle(cx, dotY, dotR, color);
}

void UI::startRecordFlash(bool led) {
  _flashing = true;
  _flashPhase = 0;
  _flashNext = millis() + 2000UL;
  if (led) startLedBlink();
  drawNewOverlay(true);
}

void UI::showBestEverRecord(const String& value, bool led) {
  _bestEverValue = value;
  if (!_bestEverActive) {
    _bestEverActive = true;
    _bestEverPhase = false;
    _bestEverNext = millis() + BEST_EVER_BLINK_MS;
    _suppressTouch = true;
  }
  _flashing = false;
  _lastTouchMs = millis();
  if (_screenOff) {
    _screenOff = false;
    applyPowerState();
  }
  if (led) startLedBlink();
  drawBestEverOverlay();
}

void UI::drawBestEverOverlay() {
  bool dark = !_bestEverPhase;
  uint16_t bg = dark ? C_BEST_EVER_BG : TFT_WHITE;
  uint16_t fg = dark ? C_WHITE : C_BEST_EVER_RED;
  _tft.fillScreen(bg);

  drawTextCentered("BEST DIFF EVER", 0, 18, _screenW, 10, FONT_SMALL, fg);

  const uint8_t* fonts[] = {FONT_MEGA, FONT_HERO, FONT_BIG, FONT_VALUE, FONT_SMALL};
  const uint8_t* valueFont = FONT_SMALL;
  int maxW = _screenW - 16;
  for (const uint8_t* font : fonts) {
    setFont(font, fg);
    if (textWidth(_bestEverValue.c_str()) <= maxW) {
      valueFont = font;
      break;
    }
  }
  drawTextCenteredAt(_bestEverValue.c_str(), _screenW / 2, _screenH / 2, valueFont, fg);
  drawTextCentered("TOUCH TO CLOSE", 0, _screenH - 30, _screenW, 10, FONT_TINY, fg);
}

void UI::dismissBestEver() {
  _bestEverActive = false;
  _bestEverPhase = false;
  _bestEverNext = 0;
  _bestEverValue = "";
  _flashing = false;
  _staticDrawn = false;
  _forceAll = true;
  _lastBigValue = "";
  drawCurrentPageStatic();
  if (_alertActive) drawOfflineAlert(_alertText);
  _staticDrawn = true;
}

void UI::drawNewOverlay(bool showText) {
  _tft.fillScreen(TFT_WHITE);
  if (!showText) return;

  drawTextCenteredAt("NEW!", _screenW / 2, _screenH / 2, FONT_MEGA, rgb565(8, 24, 72));
}

void UI::setLed(bool on) {
#if LED_ACTIVE_LOW
  uint8_t level = on ? LOW : HIGH;
#else
  uint8_t level = on ? HIGH : LOW;
#endif
  digitalWrite(LED_R_PIN, level);
  digitalWrite(LED_G_PIN, level);
  digitalWrite(LED_B_PIN, level);
}

void UI::startLedBlink() {
  _ledBlinking = true;
  _ledUntil = millis() + LED_BLINK_MS;
  _ledNext = millis() + 150UL;
  _ledOn = true;
  setLed(true);
}

void UI::updateLed() {
  if (!_ledBlinking) return;

  if (millis() >= _ledUntil) {
    _ledBlinking = false;
    _ledOn = false;
    setLed(false);
    return;
  }

  if (millis() >= _ledNext) {
    _ledOn = !_ledOn;
    setLed(_ledOn);
    _ledNext = millis() + 150UL;
  }
}

void UI::resetRecords() {
  _blockInit = false;
  _recordInit = false;
  _lastBlockFound = 0;
  _lastBestDiff = 0;
  _lastSessionDiff = 0;
  _bestEverActive = false;
  _bestEverPhase = false;
  _bestEverNext = 0;
  _bestEverValue = "";
  _forceAll = true;
}

void UI::startCelebration(int blockHeight) {
  _celebrating = true;
  _celebrateUntil = millis() + 6000UL;
  drawCelebration(blockHeight);
}

void UI::drawCelebration(int blockHeight) {
  for (int y = 0; y < _screenH; y += 2) {
    uint8_t amount = static_cast<uint8_t>((y * 255) / (_screenH - 1));
    uint16_t color = blend(rgb565(26, 15, 4), C_BG_BOTTOM, amount);
    _tft.drawFastHLine(0, y, _screenW, color);
    if (y + 1 < _screenH) _tft.drawFastHLine(0, y + 1, _screenW, color);
  }

  randomSeed(static_cast<unsigned long>(blockHeight) * 2654435761UL);
  int count = _landscape ? 48 : 36;
  for (int i = 0; i < count; ++i) {
    int x = random(0, _screenW - 5);
    int y = random(0, _screenH - 8);
    int w = random(2, 5);
    int h = random(3, 7);
    uint16_t color = (i % 3 == 0) ? C_ORANGE : (i % 3 == 1) ? C_ORANGE_LIGHT : C_WHITE;
    _tft.fillRect(x, y, w, h, color);
  }

  int cx = _screenW / 2;
  int cy = _landscape ? 78 : 100;
  int outer = _landscape ? 46 : 70;
  _tft.drawCircle(cx, cy, outer, blend(C_BG_TOP, C_ORANGE, 60));
  _tft.drawCircle(cx, cy, outer - 6, blend(C_BG_TOP, C_ORANGE, 100));
  _tft.fillCircle(cx, cy, outer - 14, C_ORANGE);
  drawTextCenteredAt("B", cx, cy, FONT_HERO, C_BG_TOP);

  if (_landscape) {
    drawTextCenteredAt("BLOCK", cx, 140, FONT_BIG, C_WHITE);
    drawTextCenteredAt("FOUND!", cx, 166, FONT_BIG, C_ORANGE_LIGHT);
    drawTextCenteredAt(("HEIGHT " + String(blockHeight)).c_str(), cx, 192, FONT_SMALL, C_MUTED);
    drawTextCenteredAt("SOLO LOTTERY WIN", cx, 208, FONT_TINY, C_DIM);
    drawTextCenteredAt("CONGRATULATIONS", cx, 222, FONT_TINY, C_AMBER);
  } else {
    drawTextCenteredAt("BLOCK", cx, 182, FONT_BIG, C_WHITE);
    drawTextCenteredAt("FOUND!", cx, 210, FONT_BIG, C_ORANGE_LIGHT);
    drawTextCenteredAt(("HEIGHT " + String(blockHeight)).c_str(), cx, 238, FONT_SMALL, C_MUTED);
    drawTextCenteredAt("SOLO LOTTERY WIN", cx, 256, FONT_TINY, C_DIM);
    drawTextCenteredAt("CONGRATULATIONS", cx, 272, FONT_TINY, C_AMBER);
  }
}

void UI::setPage(Page page) {
  if (page == _page) return;
  _page = page;
  _staticDrawn = false;
  _forceAll = true;
  _lastBigValue = "";
  _lastMinersSig = "";
  _lastStatsSig = "";
  if (page == Page::History) _historyDirty = true;
}

void UI::showPage(uint8_t page) {
  if (page > 6) return;
  setPage(static_cast<Page>(page));
}

void UI::nextPage() {
  if (_flashing || _celebrating || _bestEverActive) return;
  switch (_page) {
    case Page::Dashboard:
      setPage(Page::BestEver);
      break;
    case Page::BestEver:
      setPage(Page::BestSession);
      break;
    case Page::BestSession:
      setPage(Page::Hashrate);
      break;
    case Page::Hashrate:
      setPage(Page::Miners);
      break;
    case Page::Miners:
      setPage(Page::Stats);
      break;
    case Page::Stats:
      setPage(Page::History);
      break;
    case Page::History:
      setPage(Page::Dashboard);
      break;
  }
}

void UI::prevPage() {
  if (_flashing || _celebrating || _bestEverActive) return;
  switch (_page) {
    case Page::Dashboard:
      setPage(Page::History);
      break;
    case Page::BestEver:
      setPage(Page::Dashboard);
      break;
    case Page::BestSession:
      setPage(Page::BestEver);
      break;
    case Page::Hashrate:
      setPage(Page::BestSession);
      break;
    case Page::Miners:
      setPage(Page::Hashrate);
      break;
    case Page::Stats:
      setPage(Page::Miners);
      break;
    case Page::History:
      setPage(Page::Stats);
      break;
  }
}

int UI::mapTouchX(int raw) const {
  int v = constrain(raw, 0, 4095);
  return (v * (_screenW - 1)) / 4095;
}

int UI::mapTouchY(int raw) const {
  int v = constrain(raw, 0, 4095);
  return (v * (_screenH - 1)) / 4095;
}

void UI::pollTouch() {
  if (_bestEverActive) {
    bool down = _ts.touched();
    if (!down) {
      _suppressTouch = false;
      return;
    }
    if (_suppressTouch) return;

    _lastTouchMs = millis();
    if (_screenOff) {
      _screenOff = false;
      applyPowerState();
    }
    dismissBestEver();
    _suppressTouch = true;
    _touchWasDown = false;
    return;
  }

  if (_flashing || _celebrating) return;

  bool down = _ts.touched();
  if (down) {
    _lastTouchMs = millis();
    if (_screenOff) {
      _screenOff = false;
      applyPowerState();
      _suppressTouch = true;
      return;
    }
    if (_suppressTouch) return;

    TS_Point p = _ts.getPoint();
    int x = mapTouchX(p.x);
    int y = mapTouchY(p.y);
    if (!_touchWasDown) {
      _touchWasDown = true;
      _touchStartMs = millis();
      _touchStartX = x;
      _touchStartY = y;
      _touchLastX = x;
      _touchLastY = y;
    } else {
      _touchLastX = x;
      _touchLastY = y;
    }
    return;
  }

  if (_suppressTouch) {
    _suppressTouch = false;
    return;
  }
  if (!_touchWasDown) return;
  _touchWasDown = false;

  unsigned long duration = millis() - _touchStartMs;
  int dx = _touchLastX - _touchStartX;
  int dy = _touchLastY - _touchStartY;

  if (duration < 1200UL && abs(dx) > _screenW / 4 && abs(dx) > abs(dy)) {
    if (dx < 0) {
      nextPage();
    } else {
      prevPage();
    }
    return;
  }

  if (duration < 1000UL && abs(dx) < _screenW / 6 && abs(dy) < _screenH / 6) {
    nextPage();
  }
}

void UI::setFont(const uint8_t* font, uint16_t color) {
  _u8f.setFont(font);
  _u8f.setForegroundColor(color);
  _u8f.setFontMode(1);
}

int16_t UI::textWidth(const char* text) {
  return _u8f.getUTF8Width(text);
}

int16_t UI::baselineFor(int yTop, int h) {
  int ascent = _u8f.getFontAscent();
  return yTop + (h + ascent) / 2;
}

void UI::drawTextAt(const char* text, int x, int yTop, int h, const uint8_t* font, uint16_t color) {
  setFont(font, color);
  _u8f.drawUTF8(x, baselineFor(yTop, h), text);
}

void UI::drawTextCentered(const char* text, int x, int yTop, int w, int h, const uint8_t* font,
                          uint16_t color) {
  setFont(font, color);
  int width = textWidth(text);
  _u8f.drawUTF8(x + (w - width) / 2, baselineFor(yTop, h), text);
}

void UI::drawTextRight(const char* text, int x, int yTop, int w, int h, const uint8_t* font,
                       uint16_t color) {
  setFont(font, color);
  int width = textWidth(text);
  _u8f.drawUTF8(x + w - width, baselineFor(yTop, h), text);
}

void UI::drawTextCenteredAt(const char* text, int cx, int cy, const uint8_t* font, uint16_t color) {
  setFont(font, color);
  int width = textWidth(text);
  int ascent = _u8f.getFontAscent();
  _u8f.drawUTF8(cx - width / 2, cy + ascent / 2, text);
}
