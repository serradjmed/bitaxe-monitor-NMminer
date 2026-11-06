#include <Arduino.h>

#include "config.h"
#include "miner_client.h"
#include "ui.h"

namespace {

UI ui;
MinerClient miner;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.printf("%s v%s\n", FW_NAME, FW_VERSION);

  miner.loadSettings();
  ui.begin(miner.orientation(), miner.flip(), miner.brightness());
  ui.showBoot(FW_NAME, "Connecting to Wi-Fi...", String("Portal: ") + WIFI_PORTAL_NAME, -1);

  miner.begin();

  ui.applyDisplaySettings(miner.orientation(), miner.flip(), miner.brightness());
  ui.setNightSettings(miner.nightEnabled(), miner.nightStart(), miner.nightEnd(),
                      miner.nightBrightness(), miner.screenTimeoutSec());
  ui.showBoot(FW_NAME, "Connecting to Bitaxe...", "Looking for the AxeOS API", -1);

  MinerData data;
  unsigned long started = millis();
  while (millis() - started < BOOT_WAIT_MS) {
    if (miner.getData(data) && data.everConnected) break;
    delay(100);
  }

  ui.drawStatic();
  if (miner.getData(data)) {
    ui.update(data);
  }
}

void loop() {
  static unsigned long lastUi = 0;

  ui.pollTouch();

  uint8_t requestedPage = 0;
  if (miner.consumePageRequest(requestedPage)) {
    ui.showPage(requestedPage);
  }

  if (miner.consumeDisplayDirty()) {
    ui.resetRecords();
    ui.applyDisplaySettings(miner.orientation(), miner.flip(), miner.brightness());
    ui.setNightSettings(miner.nightEnabled(), miner.nightStart(), miner.nightEnd(),
                        miner.nightBrightness(), miner.screenTimeoutSec());
    MinerData data;
    if (miner.getData(data)) ui.update(data);
  }

  if (millis() - lastUi >= 100UL) {
    MinerData data;
    if (miner.getData(data)) {
      ui.update(data);
    }
    lastUi = millis();
  }

  delay(10);
}
