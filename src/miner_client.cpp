#include "miner_client.h"

#include <Update.h>

#include "wifi_qr.h"

namespace {

const char* const PREFS_NS = "bitaxe-mon";

const char PORTAL_HEAD[] = R"rawliteral(
<script>
(function(){
  function addQr(){
    if(location.pathname!=='/')return;
    var h=document.querySelector('.wrap h3')||document.querySelector('h3');
    if(!h||document.getElementById('wifi-qr'))return;
    var d=document.createElement('div');
    d.id='wifi-qr';
    d.style.cssText='text-align:center;margin:18px 0 10px';
    d.innerHTML='<img src="/qr.png" alt="Wi-Fi QR code" style="width:220px;height:220px;background:#fff;padding:8px;border-radius:12px"><div style="font-size:13px;color:#9aa0a6;margin-top:8px">Scan to join the BitaxeMonitor Wi-Fi</div>';
    h.parentNode.insertBefore(d,h.nextSibling);
  }
  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',addQr);else addQr();
})();
</script>
)rawliteral";

double variantToDouble(JsonVariant value) {
  if (value.isNull()) return 0.0;
  return value.as<double>();
}

String htmlEscape(const String& input) {
  String out;
  out.reserve(input.length());
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input.charAt(i);
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c; break;
    }
  }
  return out;
}

}  // namespace

void MinerClient::begin() {
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  _mutex = xSemaphoreCreateMutex();
  if (_mutex == nullptr) {
    Serial.println("[miner] mutex allocation failed");
    ESP.restart();
  }

  loadSettings();
  setupWiFi();

  _data.minerCount = _ipCount;
  _data.ledMode = _ledMode;
  _data.wifiConnected = (WiFi.status() == WL_CONNECTED);
  _data.rssi = _data.wifiConnected ? WiFi.RSSI() : 0;
  if (_data.wifiConnected) {
    String ip = WiFi.localIP().toString();
    strncpy(_data.deviceIp, ip.c_str(), sizeof(_data.deviceIp) - 1);
    _data.deviceIp[sizeof(_data.deviceIp) - 1] = 0;
    Serial.printf("[wifi] connected, monitor IP: %s\n", _data.deviceIp);
  }

  setupWebServer();

  if (xTaskCreatePinnedToCore(taskEntry, "miner", 12288, this, 1, &_task, 0) != pdPASS) {
    Serial.println("[miner] task creation failed");
    ESP.restart();
  }
}

bool MinerClient::getData(MinerData& out) {
  if (_mutex == nullptr) return false;
  if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  out = _data;
  xSemaphoreGive(_mutex);
  return true;
}

bool MinerClient::updateData(const MinerData& data) {
  if (_mutex == nullptr) return false;
  if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
  _data = data;
  xSemaphoreGive(_mutex);
  return true;
}

bool MinerClient::isWifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String MinerClient::bitaxeIp() {
  return _ipCount > 0 ? _ips[0] : String(DEFAULT_BITAXE_IP);
}

uint16_t MinerClient::pollSec() {
  return _pollSec;
}

void MinerClient::loadSettings() {
  _prefs.begin(PREFS_NS, true);
  String raw = _prefs.getString("ips", "");
  if (raw.length() == 0) raw = _prefs.getString("ip", DEFAULT_BITAXE_IP);
  _pollSec = _prefs.getUShort("poll", DEFAULT_POLL_SEC);
  _orientation = _prefs.getUChar("orient", DEFAULT_ORIENTATION);
  _flip = _prefs.getBool("flip", DEFAULT_FLIP != 0);
  _brightness = _prefs.getUChar("bright", DEFAULT_BRIGHTNESS);
  _ledMode = _prefs.getUChar("led", DEFAULT_LED_MODE);
  _nightEnabled = _prefs.getBool("night", false);
  _nightStart = _prefs.getUChar("nstart", 22);
  _nightEnd = _prefs.getUChar("nend", 7);
  _nightBrightness = _prefs.getUChar("nbright", 15);
  _screenTimeoutSec = _prefs.getUShort("timeout", 0);
  _prefs.end();

  if (!applyIpList(raw)) {
    _ipCount = 1;
    _ips[0] = DEFAULT_BITAXE_IP;
  }

  if (_pollSec < 1 || _pollSec > 60) _pollSec = DEFAULT_POLL_SEC;
  if (_orientation > 1) _orientation = DEFAULT_ORIENTATION;
  if (_brightness > 100) _brightness = DEFAULT_BRIGHTNESS;
  if (_ledMode > 3) _ledMode = DEFAULT_LED_MODE;
  if (_nightStart > 23) _nightStart = 22;
  if (_nightEnd > 23) _nightEnd = 7;
  if (_nightBrightness > 100) _nightBrightness = 15;
  if (_screenTimeoutSec > 3600) _screenTimeoutSec = 0;
  resetMinerStates();
}

void MinerClient::saveSettings() {
  _prefs.begin(PREFS_NS, false);
  _prefs.putString("ips", ipListText("\n"));
  _prefs.putString("ip", _ipCount > 0 ? _ips[0] : String(DEFAULT_BITAXE_IP));
  _prefs.putUShort("poll", _pollSec);
  _prefs.putUChar("orient", _orientation);
  _prefs.putBool("flip", _flip);
  _prefs.putUChar("bright", _brightness);
  _prefs.putUChar("led", _ledMode);
  _prefs.putBool("night", _nightEnabled);
  _prefs.putUChar("nstart", _nightStart);
  _prefs.putUChar("nend", _nightEnd);
  _prefs.putUChar("nbright", _nightBrightness);
  _prefs.putUShort("timeout", _screenTimeoutSec);
  _prefs.end();
}

bool MinerClient::applyIpList(const String& raw) {
  String items[MAX_MINERS];
  uint8_t count = 0;

  unsigned int start = 0;
  while (start < raw.length() && count < MAX_MINERS) {
    unsigned int end = start;
    while (end < raw.length()) {
      char c = raw.charAt(end);
      if (c == ',' || c == ';' || c == '\n' || c == '\r' || c == ' ' || c == '\t') break;
      ++end;
    }

    String token = raw.substring(start, end);
    token.trim();
    if (token.length() > 0) {
      IPAddress parsed;
      if (parsed.fromString(token)) {
        bool duplicate = false;
        for (uint8_t i = 0; i < count; ++i) {
          if (items[i] == token) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) items[count++] = token;
      }
    }

    start = end;
    while (start < raw.length()) {
      char c = raw.charAt(start);
      if (c != ',' && c != ';' && c != '\n' && c != '\r' && c != ' ' && c != '\t') break;
      ++start;
    }
  }

  if (count == 0) return false;

  _ipCount = count;
  for (uint8_t i = 0; i < MAX_MINERS; ++i) {
    _ips[i] = (i < count) ? items[i] : String();
  }
  return true;
}

void MinerClient::resetMinerStates() {
  for (uint8_t i = 0; i < MAX_MINERS; ++i) {
    _miners[i] = MinerData();
    _lastBlockFound[i] = 0;
    _blockInit[i] = false;
  }
  _blockCounter = 0;
  _lastBlockHeight = 0;
  _wasOnline = false;
}

String MinerClient::ipListText(const char* separator) const {
  String out;
  for (uint8_t i = 0; i < _ipCount; ++i) {
    if (i > 0) out += separator;
    out += _ips[i];
  }
  return out;
}

bool MinerClient::consumeDisplayDirty() {
  if (!_displayDirty) return false;
  _displayDirty = false;
  return true;
}

bool MinerClient::consumePageRequest(uint8_t& page) {
  if (_requestedPage > 6) return false;
  page = _requestedPage;
  _requestedPage = 0xFF;
  return true;
}

void MinerClient::setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("bitaxe-monitor");
  WiFi.setAutoReconnect(true);

  WiFiManager wm;
  wm.setDebugOutput(false);
  wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT);
  wm.setTitle("Bitaxe Monitor");
  wm.setDarkMode(true);
  wm.setCustomHeadElement(PORTAL_HEAD);
  wm.setWebServerCallback([&wm]() {
    auto* server = wm.server.get();
    server->on("/qr.png", HTTP_GET, [server]() {
      server->send_P(200, "image/png", reinterpret_cast<PGM_P>(WIFI_QR_PNG),
                     sizeof(WIFI_QR_PNG));
    });
  });

  char ipsBuf[MAX_MINERS * 16 + 8];
  String ipsList = ipListText(",");
  strncpy(ipsBuf, ipsList.c_str(), sizeof(ipsBuf) - 1);
  ipsBuf[sizeof(ipsBuf) - 1] = 0;

  char pollBuf[4];
  snprintf(pollBuf, sizeof(pollBuf), "%u", static_cast<unsigned>(_pollSec));

  char orientBuf[2];
  snprintf(orientBuf, sizeof(orientBuf), "%u", static_cast<unsigned>(_orientation));
  char flipBuf[2];
  snprintf(flipBuf, sizeof(flipBuf), "%u", _flip ? 1U : 0U);
  char brightBuf[4];
  snprintf(brightBuf, sizeof(brightBuf), "%u", static_cast<unsigned>(_brightness));
  char ledBuf[2];
  snprintf(ledBuf, sizeof(ledBuf), "%u", static_cast<unsigned>(_ledMode));

  WiFiManagerParameter paramIps("ips", "Bitaxe IP addresses (comma-separated)", ipsBuf,
                                sizeof(ipsBuf) - 1);
  WiFiManagerParameter paramPoll("poll", "Refresh interval (seconds)", pollBuf, 3);
  WiFiManagerParameter paramOrient("orient", "Orientation (0=portrait, 1=landscape)", orientBuf, 1);
  WiFiManagerParameter paramFlip("flip", "Rotation 180 (0=no, 1=yes)", flipBuf, 1);
  WiFiManagerParameter paramBright("bright", "Brightness (0-100)", brightBuf, 3);
  WiFiManagerParameter paramLed("led", "LED on best (0=off, 1=ever, 2=session, 3=both)", ledBuf, 1);
  wm.addParameter(&paramIps);
  wm.addParameter(&paramPoll);
  wm.addParameter(&paramOrient);
  wm.addParameter(&paramFlip);
  wm.addParameter(&paramBright);
  wm.addParameter(&paramLed);

  wm.setSaveParamsCallback([this, &paramIps, &paramPoll, &paramOrient, &paramFlip, &paramBright,
                            &paramLed]() {
    bool ipsChanged = applyIpList(String(paramIps.getValue()));
    int p = atoi(paramPoll.getValue());
    if (p >= 1 && p <= 60) _pollSec = static_cast<uint16_t>(p);

    int o = atoi(paramOrient.getValue());
    if (o >= 0 && o <= 1) _orientation = static_cast<uint8_t>(o);
    int f = atoi(paramFlip.getValue());
    _flip = (f != 0);
    int b = atoi(paramBright.getValue());
    if (b >= 0 && b <= 100) _brightness = static_cast<uint8_t>(b);
    int l = atoi(paramLed.getValue());
    if (l >= 0 && l <= 3) _ledMode = static_cast<uint8_t>(l);

    if (ipsChanged) resetMinerStates();
    saveSettings();
    _displayDirty = true;
    _forcePoll = true;
  });

  Serial.printf("[wifi] connecting (portal \"%s\" if needed)\n", WIFI_PORTAL_NAME);
  bool connected = wm.autoConnect(WIFI_PORTAL_NAME);
  if (!connected) {
    Serial.println("[wifi] configuration portal timed out, continuing offline");
  }

  WiFi.setSleep(false);
  Serial.printf("[wifi] ready, IP: %s\n", WiFi.localIP().toString().c_str());
}

void MinerClient::setupWebServer() {
  _server.on("/", HTTP_GET, [this]() { handleRoot(); });
  _server.on("/save", HTTP_GET, [this]() { handleSave(); });
  _server.on("/save", HTTP_POST, [this]() { handleSave(); });
  _server.on("/page", HTTP_GET, [this]() { handlePage(); });
  _server.on("/status", HTTP_GET, [this]() { handleStatus(); });
  _server.on("/restart", HTTP_GET, [this]() { handleRestart(); });
  _server.on("/update", HTTP_GET, [this]() { handleUpdatePage(); });
  _server.on("/update", HTTP_POST, [this]() { handleUpdateFinish(); },
             [this]() { handleUpdateUpload(); });
  _server.on("/favicon.ico", HTTP_GET, [this]() { _server.send(204, "text/plain", ""); });
  _server.onNotFound([this]() { _server.send(404, "text/plain", "Not found"); });
  _server.begin();
}

void MinerClient::taskEntry(void* arg) {
  static_cast<MinerClient*>(arg)->taskLoop();
  vTaskDelete(nullptr);
}

void MinerClient::taskLoop() {
  unsigned long lastWifiUpdate = 0;
  unsigned long lastReconnect = 0;
  bool timeConfigured = false;

  while (true) {
    _server.handleClient();
    checkResetButton();

    bool wifi = (WiFi.status() == WL_CONNECTED);
    if (wifi && !timeConfigured) {
      configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
      timeConfigured = true;
    }
    if (!wifi) {
      for (uint8_t i = 0; i < _ipCount; ++i) _miners[i].online = false;
    }

    if (wifi) {
      if (_forcePoll || (millis() - _lastAttemptMs) >= (static_cast<unsigned long>(_pollSec) * 1000UL)) {
        _forcePoll = false;
        poll();
        _lastAttemptMs = millis();
      }
    } else if (millis() - lastReconnect > 10000UL) {
      WiFi.reconnect();
      lastReconnect = millis();
    }

    if (millis() - lastWifiUpdate >= 1000UL) {
      MinerData snapshot;
      if (getData(snapshot)) {
        snapshot.wifiConnected = wifi;
        snapshot.rssi = wifi ? WiFi.RSSI() : 0;
        snapshot.minerCount = _ipCount;
        snapshot.ledMode = _ledMode;
        if (wifi) {
          String ip = WiFi.localIP().toString();
          strncpy(snapshot.deviceIp, ip.c_str(), sizeof(snapshot.deviceIp) - 1);
          snapshot.deviceIp[sizeof(snapshot.deviceIp) - 1] = 0;
        } else {
          snapshot.deviceIp[0] = 0;
          snapshot.online = false;
          snapshot.onlineCount = 0;
        }
        updateData(snapshot);
      }
      lastWifiUpdate = millis();
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void MinerClient::poll() {
  bool wifi = (WiFi.status() == WL_CONNECTED);
  if (!wifi) {
    for (uint8_t i = 0; i < _ipCount; ++i) _miners[i].online = false;
    MinerData agg = buildAggregate(false);
    _wasOnline = false;
    updateData(agg);
    return;
  }

  for (uint8_t i = 0; i < _ipCount; ++i) {
    bool wasOnline = _miners[i].online;
    pollOne(_ips[i], _miners[i]);

    if (_miners[i].online) {
      if (!_blockInit[i]) {
        _lastBlockFound[i] = _miners[i].blockFound;
        _blockInit[i] = true;
      } else if (_miners[i].blockFound > _lastBlockFound[i]) {
        _blockCounter += _miners[i].blockFound - _lastBlockFound[i];
        _lastBlockHeight = _miners[i].blockHeight;
        _lastBlockFound[i] = _miners[i].blockFound;
      } else if (_miners[i].blockFound < _lastBlockFound[i]) {
        _lastBlockFound[i] = _miners[i].blockFound;
      }
    }

    if (_miners[i].online != wasOnline) {
      Serial.printf("[axeos] %s %s\n", _ips[i].c_str(), _miners[i].online ? "online" : "offline");
    }
  }

  MinerData agg = buildAggregate(true);
  if (agg.online != _wasOnline) {
    Serial.printf("[axeos] %u/%u miner(s) online, bestDiff=%.0f hashRate=%.0f temp=%.1f\n",
                  static_cast<unsigned>(agg.onlineCount), static_cast<unsigned>(agg.minerCount),
                  agg.bestDiff, agg.hashRate, agg.temp);
    _wasOnline = agg.online;
  }
  updateData(agg);
}

void MinerClient::pollOne(const String& ip, MinerData& out) {
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(1000);
  http.setTimeout(1800);

  String url = String("http://") + ip + "/api/system/info";
  if (!http.begin(client, url)) {
    out.online = false;
    return;
  }

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      out.bestDiff = parseDiff(doc["bestDiff"]);
      out.bestSessionDiff = parseDiff(doc["bestSessionDiff"]);
      out.networkDiff = parseDiff(doc["networkDifficulty"]);
      out.hashRate = variantToDouble(doc["hashRate"]);
      out.hashRate1m = variantToDouble(doc["hashRate_1m"]);
      out.temp = doc["temp"] | -1.0f;
      out.power = doc["power"] | 0.0f;
      out.sharesAccepted = doc["sharesAccepted"] | 0;
      out.sharesRejected = doc["sharesRejected"] | 0;
      out.blockHeight = doc["blockHeight"] | 0;
      out.blockFound = doc["blockFound"] | 0;
      out.uptime = doc["uptimeSeconds"] | 0;

      out.online = true;
      out.everConnected = true;
      out.lastUpdateMs = millis();
    } else {
      out.online = false;
    }
  } else {
    out.online = false;
  }

  http.end();
}

MinerData MinerClient::buildAggregate(bool wifi) const {
  MinerData agg;
  agg.wifiConnected = wifi;
  agg.rssi = wifi ? WiFi.RSSI() : 0;
  agg.minerCount = _ipCount;
  agg.ledMode = _ledMode;

  if (wifi) {
    String ip = WiFi.localIP().toString();
    strncpy(agg.deviceIp, ip.c_str(), sizeof(agg.deviceIp) - 1);
    agg.deviceIp[sizeof(agg.deviceIp) - 1] = 0;
  }

  double tempSum = 0;
  int tempCount = 0;

  for (uint8_t i = 0; i < _ipCount; ++i) {
    const MinerData& m = _miners[i];

    MinerEntry& entry = agg.miners[i];
    entry.online = m.online;
    entry.everConnected = m.everConnected;
    entry.hashRate1m = m.hashRate1m > 0 ? m.hashRate1m : m.hashRate;
    entry.temp = m.temp;
    entry.power = m.power;
    strncpy(entry.ip, _ips[i].c_str(), sizeof(entry.ip) - 1);
    entry.ip[sizeof(entry.ip) - 1] = 0;

    if (m.everConnected) {
      agg.everConnected = true;
      if (m.bestDiff > agg.bestDiff) agg.bestDiff = m.bestDiff;
      if (m.bestSessionDiff > agg.bestSessionDiff) agg.bestSessionDiff = m.bestSessionDiff;
      if (m.networkDiff > agg.networkDiff) agg.networkDiff = m.networkDiff;
      if (m.lastUpdateMs > agg.lastUpdateMs) agg.lastUpdateMs = m.lastUpdateMs;
    }

    if (m.online) {
      agg.online = true;
      ++agg.onlineCount;
      agg.hashRate += m.hashRate;
      agg.hashRate1m += (m.hashRate1m > 0 ? m.hashRate1m : m.hashRate);
      agg.power += m.power;
      agg.sharesAccepted += m.sharesAccepted;
      agg.sharesRejected += m.sharesRejected;
      if (m.uptime > agg.uptime) agg.uptime = m.uptime;
      if (m.temp >= -50.0f && m.temp <= 150.0f) {
        tempSum += m.temp;
        ++tempCount;
      }
    }
  }

  agg.temp = tempCount > 0 ? static_cast<float>(tempSum / tempCount) : -1.0f;
  agg.blockFound = _blockCounter;
  agg.blockHeight = _lastBlockHeight;
  return agg;
}

double MinerClient::parseDiff(JsonVariant value) {
  if (value.isNull()) return 0.0;

  if (value.is<const char*>()) {
    String text = value.as<String>();
    text.trim();
    if (text.length() == 0) return 0.0;

    char suffix = toupper(text.charAt(text.length() - 1));
    double multiplier = 1.0;

    switch (suffix) {
      case 'K': multiplier = 1e3; break;
      case 'M': multiplier = 1e6; break;
      case 'G': multiplier = 1e9; break;
      case 'T': multiplier = 1e12; break;
      case 'P': multiplier = 1e15; break;
      case 'E': multiplier = 1e18; break;
      case 'Z': multiplier = 1e21; break;
      case 'Y': multiplier = 1e24; break;
      default: break;
    }

    if (multiplier != 1.0) text.remove(text.length() - 1);
    return text.toDouble() * multiplier;
  }

  return value.as<double>();
}

void MinerClient::checkResetButton() {
  static unsigned long pressedAt = 0;

  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    if (pressedAt == 0) {
      pressedAt = millis();
    } else if (millis() - pressedAt > 5000UL) {
      Serial.println("[reset] BOOT held: clearing Wi-Fi and settings");
      _prefs.begin(PREFS_NS, false);
      _prefs.clear();
      _prefs.end();
      WiFi.disconnect(true, true);
      delay(300);
      ESP.restart();
    }
  } else {
    pressedAt = 0;
  }
}

void MinerClient::handleRoot() {
  MinerData data;
  getData(data);

  String status;
  if (!data.wifiConnected) {
    status = "Wi-Fi disconnected";
  } else if (data.minerCount <= 1) {
    status = data.online ? "Bitaxe online" : "Bitaxe offline";
  } else {
    status = String(data.onlineCount) + "/" + String(data.minerCount) + " Bitaxe online";
  }

  String html = F(
      "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>Bitaxe Monitor</title><style>"
      ":root{color-scheme:dark}"
      "*{box-sizing:border-box}"
      "body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;"
      "background:#0b0e14;color:#f2f6ff;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif}"
      ".card{width:min(92vw,430px);background:#111826;border:1px solid #1e2a3d;border-radius:18px;"
      "padding:26px;box-shadow:0 24px 70px #0008}"
      "h1{margin:0 0 4px;font-size:23px}"
      "h1 span{color:#f7931a}"
      ".sub{margin:0 0 20px;color:#7a8aa0;font-size:13px}"
      "label{display:block;margin:15px 0 6px;color:#7a8aa0;font-size:11px;"
      "letter-spacing:.09em;text-transform:uppercase}"
      "input,select,textarea{width:100%;padding:12px;border-radius:10px;border:1px solid #243247;"
      "background:#0b0e14;color:#f2f6ff;font-size:16px}"
      "textarea{min-height:104px;resize:vertical;font-family:ui-monospace,SFMono-Regular,monospace;"
      "line-height:1.5}"
      "input:focus,select:focus,textarea:focus{outline:none;border-color:#f7931a}"
      "input[type=range]{height:34px;padding:0;border:0;background:transparent;accent-color:#f7931a}"
      ".val{float:right;color:#f7931a;font-weight:700}"
      "button{width:100%;margin-top:20px;padding:13px;border:0;border-radius:10px;"
      "background:#f7931a;color:#111;font-size:16px;font-weight:700;cursor:pointer}"
      ".status{display:flex;align-items:center;gap:8px;margin-top:18px;color:#7a8aa0;font-size:13px}"
      ".dot{width:8px;height:8px;border-radius:50%;background:%DOT%}"
      ".pages{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin:0 0 20px}"
      ".pages a{padding:10px 6px;border:1px solid #243247;border-radius:10px;text-align:center;"
      "font-size:13px;font-weight:600;color:#f2f6ff;background:#0b0e14}"
      ".pages a:hover{border-color:#f7931a;color:#f7931a}"
      "a{color:#f7931a;text-decoration:none}"
      "</style></head><body><div class=\"card\">"
      "<h1><span>₿</span> Bitaxe Monitor</h1>"
      "<p class=\"sub\">AxeOS dashboard · firmware v%VER%</p>"
      "<label>Screen page</label><div class=\"pages\">"
      "<a href=\"/page?p=0\">Dashboard</a>"
      "<a href=\"/page?p=1\">Best Ever</a>"
      "<a href=\"/page?p=2\">Best Session</a>"
      "<a href=\"/page?p=3\">Hashrate</a>"
      "<a href=\"/page?p=4\">Miners</a>"
      "<a href=\"/page?p=5\">Stats</a>"
      "<a href=\"/page?p=6\">History</a>"
      "</div>"
      "<form action=\"/save\" method=\"post\">"
      "<label>Bitaxe IP addresses</label>"
      "<textarea name=\"ips\" placeholder=\"192.168.1.45&#10;192.168.1.46\" required>%IPS%</textarea>"
      "<p class=\"sub\" style=\"margin:6px 0 0\">One address per line (8 maximum).</p>"
      "<label>Refresh interval (seconds)</label>"
      "<input name=\"poll\" type=\"number\" min=\"1\" max=\"60\" value=\"%POLL%\" required>"
      "<label>Screen orientation</label>"
      "<select name=\"orient\">"
      "<option value=\"portrait\"%ORIENT_P%>Portrait</option>"
      "<option value=\"landscape\"%ORIENT_L%>Landscape</option>"
      "</select>"
      "<label>Screen rotation</label>"
      "<select name=\"flip\">"
      "<option value=\"0\"%FLIP_0%>Normal</option>"
      "<option value=\"1\"%FLIP_1%>180°</option>"
      "</select>"
      "<label>LED on new best</label>"
      "<select name=\"led\">"
      "<option value=\"0\"%LED_0%>Disabled</option>"
      "<option value=\"1\"%LED_1%>Best ever</option>"
      "<option value=\"2\"%LED_2%>Best session</option>"
      "<option value=\"3\"%LED_3%>Both</option>"
      "</select>"
      "<label>Brightness <span class=\"val\" id=\"bv\">%BRIGHT%</span></label>"
      "<input name=\"bright\" type=\"range\" min=\"0\" max=\"100\" value=\"%BRIGHT%\" "
      "oninput=\"document.getElementById('bv').textContent=this.value\">"
      "<label>Night mode</label><select name=\"night\">"
      "<option value=\"0\"%NIGHT_0%>Disabled</option>"
      "<option value=\"1\"%NIGHT_1%>Enabled</option>"
      "</select>"
      "<label>Night start / end (UTC)</label>"
      "<div style=\"display:flex;gap:8px\">"
      "<input name=\"nstart\" type=\"number\" min=\"0\" max=\"23\" value=\"%NSTART%\">"
      "<input name=\"nend\" type=\"number\" min=\"0\" max=\"23\" value=\"%NEND%\">"
      "</div>"
      "<label>Night brightness <span class=\"val\" id=\"nbv\">%NBRIGHT%</span></label>"
      "<input name=\"nbright\" type=\"range\" min=\"0\" max=\"100\" value=\"%NBRIGHT%\" "
      "oninput=\"document.getElementById('nbv').textContent=this.value\">"
      "<label>Screen timeout (seconds, 0 = off)</label>"
      "<input name=\"timeout\" type=\"number\" min=\"0\" max=\"3600\" value=\"%TIMEOUT%\">"
      "<button>Save</button></form>"
      "<div class=\"status\"><span class=\"dot\"></span>%STATUS%</div>"
      "<p class=\"sub\" style=\"margin:18px 0 0\">Touch: tap the screen to cycle through "
      "Dashboard, Best Ever, Best Session, Hashrate, Miners, Stats and History.</p>"
      "<p class=\"sub\" style=\"margin:8px 0 0\"><a href=\"/update\">Firmware update</a>"
      " · <a href=\"/restart\">Restart monitor</a> · <a href=\"/status\">API</a></p>"
      "</div></body></html>");

  html.replace("%VER%", FW_VERSION);
  html.replace("%IPS%", htmlEscape(ipListText("\n")));
  html.replace("%POLL%", String(_pollSec));
  html.replace("%ORIENT_P%", _orientation == 0 ? " selected" : "");
  html.replace("%ORIENT_L%", _orientation == 1 ? " selected" : "");
  html.replace("%FLIP_0%", _flip ? "" : " selected");
  html.replace("%FLIP_1%", _flip ? " selected" : "");
  html.replace("%LED_0%", _ledMode == 0 ? " selected" : "");
  html.replace("%LED_1%", _ledMode == 1 ? " selected" : "");
  html.replace("%LED_2%", _ledMode == 2 ? " selected" : "");
  html.replace("%LED_3%", _ledMode == 3 ? " selected" : "");
  html.replace("%BRIGHT%", String(_brightness));
  html.replace("%NIGHT_0%", _nightEnabled ? "" : " selected");
  html.replace("%NIGHT_1%", _nightEnabled ? " selected" : "");
  html.replace("%NSTART%", String(_nightStart));
  html.replace("%NEND%", String(_nightEnd));
  html.replace("%NBRIGHT%", String(_nightBrightness));
  html.replace("%TIMEOUT%", String(_screenTimeoutSec));
  html.replace("%STATUS%", status);
  html.replace("%DOT%", data.online ? "#34d399" : (data.wifiConnected ? "#fbbf24" : "#f87171"));

  _server.send(200, "text/html; charset=utf-8", html);
}

void MinerClient::handleSave() {
  String oldIps = ipListText("\n");
  String ips = _server.arg("ips");
  if (!applyIpList(ips)) {
    _server.send(400, "text/html; charset=utf-8",
                 "<meta charset=\"utf-8\"><p>Invalid IP address.</p><p><a href=\"/\">Back</a></p>");
    return;
  }
  bool ipsChanged = (ipListText("\n") != oldIps);

  int p = _server.arg("poll").toInt();
  if (p < 1 || p > 60) p = DEFAULT_POLL_SEC;

  if (_server.hasArg("orient")) {
    String o = _server.arg("orient");
    if (o == "landscape" || o == "1") {
      _orientation = 1;
    } else if (o == "portrait" || o == "0") {
      _orientation = 0;
    }
  }
  if (_server.hasArg("flip")) {
    String f = _server.arg("flip");
    _flip = (f == "1" || f == "on" || f == "true");
  }
  if (_server.hasArg("led")) {
    int l = _server.arg("led").toInt();
    if (l >= 0 && l <= 3) _ledMode = static_cast<uint8_t>(l);
  }
  if (_server.hasArg("bright")) {
    int b = _server.arg("bright").toInt();
    if (b >= 0 && b <= 100) _brightness = static_cast<uint8_t>(b);
  }
  if (_server.hasArg("night")) _nightEnabled = _server.arg("night") == "1";
  if (_server.hasArg("nstart")) {
    int v = _server.arg("nstart").toInt();
    if (v >= 0 && v <= 23) _nightStart = static_cast<uint8_t>(v);
  }
  if (_server.hasArg("nend")) {
    int v = _server.arg("nend").toInt();
    if (v >= 0 && v <= 23) _nightEnd = static_cast<uint8_t>(v);
  }
  if (_server.hasArg("nbright")) {
    int v = _server.arg("nbright").toInt();
    if (v >= 0 && v <= 100) _nightBrightness = static_cast<uint8_t>(v);
  }
  if (_server.hasArg("timeout")) {
    int v = _server.arg("timeout").toInt();
    if (v >= 0 && v <= 3600) _screenTimeoutSec = static_cast<uint16_t>(v);
  }
  _pollSec = static_cast<uint16_t>(p);
  if (ipsChanged) resetMinerStates();
  saveSettings();
  _displayDirty = true;
  _forcePoll = true;

  Serial.printf("[settings] bitaxes=%s poll=%us orient=%u flip=%u bright=%u led=%u\n",
                ipListText(",").c_str(), _pollSec, _orientation, _flip ? 1U : 0U, _brightness,
                _ledMode);
  Serial.printf("[settings] night=%u %u-%u bright=%u timeout=%us\n",
                _nightEnabled ? 1U : 0U, _nightStart, _nightEnd, _nightBrightness,
                _screenTimeoutSec);

  _server.send(200, "text/html; charset=utf-8",
               "<meta charset=\"utf-8\"><meta http-equiv=\"refresh\" content=\"2;url=/\">"
               "<body style=\"background:#0b0e14;color:#f2f6ff;font-family:system-ui;padding:40px\">"
               "<h2>Settings saved</h2><p>Returning to the dashboard...</p></body>");
}

void MinerClient::handlePage() {
  if (!_server.hasArg("p")) {
    _server.send(400, "text/plain", "Missing page");
    return;
  }

  String page = _server.arg("p");
  if (page != "0" && page != "1" && page != "2" && page != "3" && page != "4" &&
      page != "5" && page != "6") {
    _server.send(400, "text/plain", "Invalid page");
    return;
  }

  _requestedPage = static_cast<uint8_t>(page.toInt());
  _server.sendHeader("Location", "/");
  _server.send(303, "text/plain", "");
}

void MinerClient::handleStatus() {
  MinerData data;
  getData(data);

  String json = "{";
  json += "\"online\":";
  json += data.online ? "true" : "false";
  json += ",\"wifiConnected\":";
  json += data.wifiConnected ? "true" : "false";
  json += ",\"minerCount\":";
  json += data.minerCount;
  json += ",\"onlineCount\":";
  json += data.onlineCount;
  json += ",\"ledMode\":";
  json += _ledMode;
  json += ",\"ips\":[";
  for (uint8_t i = 0; i < _ipCount; ++i) {
    if (i > 0) json += ",";
    json += "\"";
    json += _ips[i];
    json += "\"";
  }
  json += "],\"ip\":\"";
  json += _ipCount > 0 ? _ips[0] : String(DEFAULT_BITAXE_IP);
  json += "\",\"deviceIp\":\"";
  json += data.deviceIp;
  json += "\",\"pollSec\":";
  json += _pollSec;
  json += ",\"orientation\":";
  json += _orientation;
  json += ",\"flip\":";
  json += _flip ? "true" : "false";
  json += ",\"brightness\":";
  json += _brightness;
  json += ",\"nightEnabled\":";
  json += _nightEnabled ? "true" : "false";
  json += ",\"nightStart\":";
  json += _nightStart;
  json += ",\"nightEnd\":";
  json += _nightEnd;
  json += ",\"nightBrightness\":";
  json += _nightBrightness;
  json += ",\"screenTimeoutSec\":";
  json += _screenTimeoutSec;
  json += ",\"bestDiff\":";
  json += String(data.bestDiff, 0);
  json += ",\"bestSessionDiff\":";
  json += String(data.bestSessionDiff, 0);
  json += ",\"networkDifficulty\":";
  json += String(data.networkDiff, 0);
  json += ",\"hashRate\":";
  json += String(data.hashRate, 2);
  json += ",\"hashRate1m\":";
  json += String(data.hashRate1m, 2);
  json += ",\"temp\":";
  json += String(data.temp, 1);
  json += ",\"rssi\":";
  json += data.rssi;
  json += ",\"miners\":[";
  for (uint8_t i = 0; i < _ipCount; ++i) {
    const MinerData& m = _miners[i];
    if (i > 0) json += ",";
    json += "{\"ip\":\"";
    json += _ips[i];
    json += "\",\"online\":";
    json += m.online ? "true" : "false";
    json += ",\"bestDiff\":";
    json += String(m.bestDiff, 0);
    json += ",\"bestSessionDiff\":";
    json += String(m.bestSessionDiff, 0);
    json += ",\"hashRate\":";
    json += String(m.hashRate, 2);
    json += ",\"hashRate1m\":";
    json += String(m.hashRate1m, 2);
    json += ",\"temp\":";
    json += String(m.temp, 1);
    json += ",\"power\":";
    json += String(m.power, 1);
    json += "}";
  }
  json += "]}";

  _server.send(200, "application/json", json);
}

void MinerClient::handleRestart() {
  _server.send(200, "text/plain; charset=utf-8", "Restarting...");
  delay(500);
  ESP.restart();
}

void MinerClient::handleUpdatePage() {
  _otaError = "";
  _otaStarted = false;

  String html = F(
      "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>Firmware update</title><style>"
      ":root{color-scheme:dark}"
      "*{box-sizing:border-box}"
      "body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;"
      "background:#0b0e14;color:#f2f6ff;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif}"
      ".card{width:min(92vw,430px);background:#111826;border:1px solid #1e2a3d;border-radius:18px;"
      "padding:26px;box-shadow:0 24px 70px #0008}"
      "h1{margin:0 0 4px;font-size:23px}"
      "h1 span{color:#f7931a}"
      ".sub{margin:0 0 18px;color:#7a8aa0;font-size:13px;line-height:1.5}"
      "label{display:block;margin:15px 0 6px;color:#7a8aa0;font-size:11px;"
      "letter-spacing:.09em;text-transform:uppercase}"
      "input{width:100%;padding:12px;border-radius:10px;border:1px solid #243247;"
      "background:#0b0e14;color:#f2f6ff;font-size:14px}"
      "button{width:100%;margin-top:20px;padding:13px;border:0;border-radius:10px;"
      "background:#f7931a;color:#111;font-size:16px;font-weight:700;cursor:pointer}"
      "a{color:#f7931a;text-decoration:none}"
      "code{color:#f7931a}"
      ".warn{margin:16px 0 0;padding:12px;border:1px solid #7c2d12;border-radius:10px;"
      "background:#2a1208;color:#fdba74;font-size:13px;line-height:1.5}"
      "</style></head><body><div class=\"card\">"
      "<h1><span>₿</span> Firmware update</h1>"
      "<p class=\"sub\">Current version: v%VER%</p>"
      "<form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\">"
      "<label>Application image</label>"
      "<input type=\"file\" name=\"firmware\" accept=\".bin,application/octet-stream\" required>"
      "<p class=\"sub\" style=\"margin:8px 0 0\">Select the <code>*-app.bin</code> file, "
      "not the full merged image.</p>"
      "<button>Upload and reboot</button></form>"
      "<p class=\"warn\">Do not power off the monitor during the update. "
      "The device reboots automatically when it finishes.</p>"
      "<p class=\"sub\" style=\"margin:18px 0 0\"><a href=\"/\">Back</a></p>"
      "</div></body></html>");
  html.replace("%VER%", FW_VERSION);
  _server.send(200, "text/html; charset=utf-8", html);
}

void MinerClient::handleUpdateUpload() {
  HTTPUpload& upload = _server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    _otaError = "";
    _otaStarted = true;
    Serial.printf("[ota] upload: %s\n", upload.filename.c_str());

    if (!upload.filename.endsWith("-app.bin")) {
      _otaError = "Please upload the application image (*-app.bin), not the full merged image.";
      Serial.println("[ota] rejected: filename must end with -app.bin");
      return;
    }

    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      _otaError = Update.errorString();
      Update.printError(Serial);
    }
    return;
  }

  if (_otaError.length() > 0) {
    if (upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED) {
      Update.abort();
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      _otaError = Update.errorString();
      Update.printError(Serial);
      Update.abort();
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      _otaError = Update.errorString();
      Update.printError(Serial);
    } else {
      Serial.printf("[ota] success: %u bytes\n", static_cast<unsigned>(upload.totalSize));
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    _otaError = "Upload aborted.";
  }
}

void MinerClient::handleUpdateFinish() {
  _server.sendHeader("Connection", "close");

  if (!_otaStarted) {
    _server.send(400, "text/html; charset=utf-8",
                 "<meta charset=\"utf-8\"><h1>No firmware uploaded</h1>"
                 "<p><a href=\"/update\">Back</a></p>");
    return;
  }
  _otaStarted = false;

  if (_otaError.length() > 0 || Update.hasError()) {
    String message = _otaError;
    if (message.length() == 0) {
      const char* err = Update.errorString();
      message = err != nullptr ? err : "Unknown error.";
    }

    String html = F(
        "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Update failed</title></head>"
        "<body style=\"margin:0;min-height:100vh;display:flex;align-items:center;"
        "justify-content:center;background:#0b0e14;color:#f2f6ff;"
        "font-family:system-ui;padding:24px\">"
        "<div style=\"max-width:430px\"><h1>Update failed</h1><p>%MSG%</p>"
        "<p><a style=\"color:#f7931a\" href=\"/update\">Retry</a> · "
        "<a style=\"color:#f7931a\" href=\"/\">Back</a></p></div></body></html>");
    html.replace("%MSG%", message);
    _server.send(500, "text/html; charset=utf-8", html);
    return;
  }

  _server.send(200, "text/html; charset=utf-8",
               "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
               "<meta http-equiv=\"refresh\" content=\"8;url=/\">"
               "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
               "<title>Update complete</title></head>"
               "<body style=\"margin:0;min-height:100vh;display:flex;align-items:center;"
               "justify-content:center;background:#0b0e14;color:#f2f6ff;"
               "font-family:system-ui;padding:24px\">"
               "<div style=\"max-width:430px\"><h1>Update complete</h1>"
               "<p>The monitor is rebooting. This page refreshes automatically.</p>"
               "</div></body></html>");
  delay(1000);
  ESP.restart();
}
