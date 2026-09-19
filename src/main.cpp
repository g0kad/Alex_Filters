// ALEX RCVR / TX filter board standalone controller
// ESP32-WROOM-32, Arduino framework.
//
// Drives the two Alex boards' shared SPI-style shift-register bus (separate
// load strobes per board) so each relay/LED bit can be set individually --
// either from a serial terminal, or from a small web page served over WiFi
// (see include/web_page.h). Both interfaces share the same state and the
// same bit names from alex_bits.h, so correcting a name in one place fixes
// both UIs. See README.md for wiring and setup.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <math.h>
#include "alex_bits.h"
#include "web_page.h"

// Name of the setup access point the board makes if it has no saved WiFi
// credentials (or they no longer work). Connect a phone/laptop to it and a
// captive portal lets you pick your real network -- no password ever goes
// in source code. See README.md for the full flow.
static const char *AP_SETUP_NAME = "ALEX-Filter-Setup";

// Hostname advertised via mDNS -- reachable as http://<this>.local/ on
// networks that support it (most home LANs; some Windows PCs may need
// Bonjour installed -- the IP address printed on the serial monitor at boot
// always works as a fallback).
static const char *MDNS_HOSTNAME = "alexctrl";

// ---- Pin assignments (change here if you wire it differently) ----
static const uint8_t PIN_SCK     = 18; // -> Alex pin 4  (SPI SCK)
static const uint8_t PIN_SDO     = 23; // -> Alex pin 3  (SPI SDO / data)
static const uint8_t PIN_RX_LOAD = 5;  // -> Alex pin 5  (RX board load strobe)
static const uint8_t PIN_TX_LOAD = 17; // -> Alex pin 6  (TX board load strobe)
static const uint8_t PIN_FWD_PWR = 34; // <- Alex pin 7  (analog fwd power, 0-3V, ADC1 input-only pin)
static const uint8_t PIN_REV_PWR = 35; // <- Alex pin 9  (analog rev power, 0-3V, ADC1 input-only pin)

// Clock the SPI bus slower than the 1.25 Mbit/s TAPR recommends -- this is a
// bit-banged bus for a test jig, not a speed-critical link.
static const uint16_t BIT_DELAY_US = 5;

static uint16_t rxWord = 0;
static uint16_t txWord = 0;

static WebServer server(80);
static WiFiManager wm;

// ---------------------------------------------------------------------------
// Low-level bus drive
// ---------------------------------------------------------------------------

// Shift a 16-bit word out, bit 0 first, data valid on rising edge of SCK,
// then pulse the given board's load strobe to latch it (rising edge active).
static void shiftAndLatch(uint16_t word, uint8_t loadPin) {
  digitalWrite(PIN_SCK, LOW);
  digitalWrite(loadPin, LOW);

  for (uint8_t i = 0; i < 16; i++) {
    digitalWrite(PIN_SDO, (word >> i) & 0x01);
    delayMicroseconds(BIT_DELAY_US);
    digitalWrite(PIN_SCK, HIGH);   // rising edge loads the bit
    delayMicroseconds(BIT_DELAY_US);
    digitalWrite(PIN_SCK, LOW);
    delayMicroseconds(BIT_DELAY_US);
  }

  digitalWrite(loadPin, HIGH);     // rising edge latches the word
  delayMicroseconds(BIT_DELAY_US);
  digitalWrite(loadPin, LOW);
}

static void sendRx() { shiftAndLatch(rxWord, PIN_RX_LOAD); }
static void sendTx() { shiftAndLatch(txWord, PIN_TX_LOAD); }

// Shared by both the serial shell and the web API so there's exactly one
// place that updates a board's word and re-latches it.
static void setBitOnBoard(bool isRx, uint8_t bit, bool on) {
  uint16_t mask = (uint16_t)1 << bit;
  if (isRx) {
    if (on) rxWord |= mask; else rxWord &= ~mask;
    sendRx();
  } else {
    if (on) txWord |= mask; else txWord &= ~mask;
    sendTx();
  }
}

static void clearBoard(bool isRx) {
  if (isRx) { rxWord = 0; sendRx(); } else { txWord = 0; sendTx(); }
}

// ---------------------------------------------------------------------------
// Power reading
// ---------------------------------------------------------------------------
// Forward/reverse power formula (P = V^2 / 0.09) is from the TAPR ALEX
// manual's description of the on-board directional coupler; treat the watt
// figures as approximate. SWR is derived from those two readings the usual
// way and is only meaningful once forward power is well above the ADC noise
// floor.
//
// The coupler only means anything while the TX board is actually in
// transmit (its tr_transmit relay routes RF through it) -- in receive there
// is no RF present to sample, so the ADC pins just read whatever floating/
// bias voltage they happen to settle on. We gate the reading on that bit
// rather than report a number that looks like power but isn't.

struct PowerReading {
  bool txActive;
  uint32_t fwdMv;
  uint32_t revMv;
  float fwdW;
  float revW;
  float swr;
  bool swrValid;
};

static bool isTransmitting() {
  return (txWord >> TX_TR_TRANSMIT) & 0x01;
}

static PowerReading readPower() {
  PowerReading p = {};
  p.txActive = isTransmitting();
  if (!p.txActive) {
    return p; // leave everything zeroed/invalid -- see buildPowerJson()/printPower()
  }

  p.fwdMv = analogReadMilliVolts(PIN_FWD_PWR);
  p.revMv = analogReadMilliVolts(PIN_REV_PWR);
  float vf = p.fwdMv / 1000.0f;
  float vr = p.revMv / 1000.0f;
  p.fwdW = (vf * vf) / 0.09f;
  p.revW = (vr * vr) / 0.09f;
  if (p.fwdW > 0.01f && p.revW < p.fwdW) {
    float ratio = sqrtf(p.revW / p.fwdW);
    if (ratio > 0.98f) ratio = 0.98f; // avoid blowing up near-total-reflection edge case
    p.swr = (1.0f + ratio) / (1.0f - ratio);
    p.swrValid = true;
  } else {
    p.swr = 0;
    p.swrValid = false;
  }
  return p;
}

// ---------------------------------------------------------------------------
// JSON helpers (hand-rolled -- the payloads are small and fixed-shape, so
// this avoids pulling in a JSON library dependency for a beginner project)
// ---------------------------------------------------------------------------

static String bitsJsonArray(const BitName *names, size_t count, uint16_t word) {
  String s = "[";
  for (size_t i = 0; i < count; i++) {
    if (i) s += ",";
    s += "{\"bit\":" + String(names[i].bit) + ",\"name\":\"" + names[i].name +
         "\",\"on\":" + (((word >> names[i].bit) & 0x01) ? "true" : "false") + "}";
  }
  s += "]";
  return s;
}

static String buildStateJson() {
  String s = "{";
  s += "\"rx\":{\"word\":" + String(rxWord) + ",\"bits\":" +
       bitsJsonArray(RX_BIT_NAMES, sizeof(RX_BIT_NAMES) / sizeof(RX_BIT_NAMES[0]), rxWord) + "},";
  s += "\"tx\":{\"word\":" + String(txWord) + ",\"bits\":" +
       bitsJsonArray(TX_BIT_NAMES, sizeof(TX_BIT_NAMES) / sizeof(TX_BIT_NAMES[0]), txWord) + "}";
  s += "}";
  return s;
}

static String buildPowerJson() {
  PowerReading p = readPower();
  String s = "{";
  s += "\"tx_active\":";
  s += (p.txActive ? "true" : "false");
  s += ",";
  if (p.txActive) {
    s += "\"fwd_mv\":" + String(p.fwdMv) + ",";
    s += "\"rev_mv\":" + String(p.revMv) + ",";
    s += "\"fwd_w\":" + String(p.fwdW, 2) + ",";
    s += "\"rev_w\":" + String(p.revW, 3) + ",";
    s += "\"swr\":" + (p.swrValid ? String(p.swr, 2) : String("null"));
  } else {
    s += "\"fwd_mv\":null,\"rev_mv\":null,\"fwd_w\":null,\"rev_w\":null,\"swr\":null";
  }
  s += "}";
  return s;
}

// ---------------------------------------------------------------------------
// Web server handlers
// ---------------------------------------------------------------------------

static void handleRoot() {
  server.send(200, "text/html", PAGE_HTML);
}

static void handleApiState() {
  server.send(200, "application/json", buildStateJson());
}

static void handleApiPower() {
  server.send(200, "application/json", buildPowerJson());
}

static void handleApiSet() {
  if (!server.hasArg("board") || !server.hasArg("bit") || !server.hasArg("val")) {
    server.send(400, "text/plain", "missing args: board, bit, val");
    return;
  }
  String board = server.arg("board");
  int bit = server.arg("bit").toInt();
  int val = server.arg("val").toInt();
  if ((board != "rx" && board != "tx") || bit < 0 || bit > 15) {
    server.send(400, "text/plain", "bad args");
    return;
  }
  setBitOnBoard(board == "rx", (uint8_t)bit, val != 0);
  server.send(200, "application/json", buildStateJson());
}

static void handleApiClear() {
  if (!server.hasArg("board")) {
    server.send(400, "text/plain", "missing arg: board");
    return;
  }
  String board = server.arg("board");
  if (board != "rx" && board != "tx") {
    server.send(400, "text/plain", "bad board");
    return;
  }
  clearBoard(board == "rx");
  server.send(200, "application/json", buildStateJson());
}

static void handleNotFound() {
  server.send(404, "text/plain", "not found");
}

// ---------------------------------------------------------------------------
// Serial command shell
// ---------------------------------------------------------------------------

static void printBinary16(uint16_t w) {
  for (int i = 15; i >= 0; i--) {
    Serial.print((w >> i) & 0x01);
    if (i % 4 == 0 && i != 0) Serial.print(' ');
  }
}

static void printStatus() {
  Serial.println();
  Serial.print("RX word: 0x");
  Serial.print(rxWord, HEX);
  Serial.print("  ");
  printBinary16(rxWord);
  Serial.println();
  for (auto &bn : RX_BIT_NAMES) {
    Serial.print("  rx ");
    Serial.print(bn.bit);
    Serial.print(" (");
    Serial.print(bn.name);
    Serial.print("): ");
    Serial.println((rxWord >> bn.bit) & 0x01 ? "ON" : "off");
  }
  Serial.println();
  Serial.print("TX word: 0x");
  Serial.print(txWord, HEX);
  Serial.print("  ");
  printBinary16(txWord);
  Serial.println();
  for (auto &bn : TX_BIT_NAMES) {
    Serial.print("  tx ");
    Serial.print(bn.bit);
    Serial.print(" (");
    Serial.print(bn.name);
    Serial.print("): ");
    Serial.println((txWord >> bn.bit) & 0x01 ? "ON" : "off");
  }
  Serial.println();
}

static void printPower() {
  PowerReading p = readPower();
  if (!p.txActive) {
    Serial.println("Not transmitting (TX tr_transmit bit is off) -- forward/reverse power");
    Serial.println("reading is meaningless in receive, so it's skipped rather than shown.");
    return;
  }
  Serial.print("Fwd: ");
  Serial.print(p.fwdMv);
  Serial.print(" mV (");
  Serial.print(p.fwdW, 2);
  Serial.print(" W)   Rev: ");
  Serial.print(p.revMv);
  Serial.print(" mV (");
  Serial.print(p.revW, 3);
  Serial.print(" W)   SWR: ");
  if (p.swrValid) Serial.println(p.swr, 2);
  else Serial.println("n/a");
}

static void printHelp() {
  Serial.println();
  Serial.println("ALEX board controller -- commands:");
  Serial.println("  rx <bit> on|off     set one RX board bit (0-15)");
  Serial.println("  tx <bit> on|off     set one TX board bit (0-15)");
  Serial.println("  rx word <hex>       set the whole RX word, e.g. rx word 0A03");
  Serial.println("  tx word <hex>       set the whole TX word, e.g. tx word 0000");
  Serial.println("  rx clear / tx clear all bits off on that board");
  Serial.println("  status              show both words and named bit states");
  Serial.println("  pwr                 read forward/reverse power ADC pins");
  Serial.println("  wifi reset          forget saved WiFi and reboot into setup mode");
  Serial.println("  help                show this message");
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Web UI: http://");
    Serial.print(WiFi.localIP());
    Serial.print("/  (or http://");
    Serial.print(MDNS_HOSTNAME);
    Serial.println(".local/)");
  } else {
    Serial.println("WiFi not connected -- web UI unavailable. Connect to the");
    Serial.print("\"");
    Serial.print(AP_SETUP_NAME);
    Serial.println("\" setup AP and reboot, or send 'wifi reset' to try a different network.");
  }
  Serial.println();
  Serial.println("Bit numbers/names are a best guess from the TAPR docs -- verify");
  Serial.println("against your boards (relay click / VNA trace) before trusting them.");
  Serial.println();
}

static String readLine() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (buf.length() == 0) continue;
      String line = buf;
      buf = "";
      return line;
    }
    buf += c;
  }
  return String();
}

static void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;

  // tokenize on spaces (max 3 tokens needed)
  String tok[3];
  int n = 0;
  int start = 0;
  for (int i = 0; i <= (int)line.length() && n < 3; i++) {
    if (i == (int)line.length() || line[i] == ' ') {
      if (i > start) tok[n++] = line.substring(start, i);
      start = i + 1;
    }
  }

  if (tok[0] == "help") {
    printHelp();
  } else if (tok[0] == "status") {
    printStatus();
  } else if (tok[0] == "pwr") {
    printPower();
  } else if (tok[0] == "wifi" && tok[1] == "reset") {
    Serial.println("Clearing saved WiFi credentials and rebooting into setup mode...");
    wm.resetSettings();
    delay(500);
    ESP.restart();
  } else if ((tok[0] == "rx" || tok[0] == "tx") && tok[1] == "clear") {
    clearBoard(tok[0] == "rx");
    Serial.println("cleared.");
  } else if ((tok[0] == "rx" || tok[0] == "tx") && tok[1] == "word") {
    uint16_t w = (uint16_t)strtol(tok[2].c_str(), nullptr, 16);
    if (tok[0] == "rx") { rxWord = w; sendRx(); } else { txWord = w; sendTx(); }
    Serial.print(tok[0]);
    Serial.print(" word set to 0x");
    Serial.println(w, HEX);
  } else if (tok[0] == "rx" || tok[0] == "tx") {
    int bit = tok[1].toInt();
    bool on = (tok[2] == "on" || tok[2] == "1");
    bool off = (tok[2] == "off" || tok[2] == "0");
    if (bit < 0 || bit > 15 || (!on && !off)) {
      Serial.println("usage: rx|tx <bit 0-15> on|off");
      return;
    }
    setBitOnBoard(tok[0] == "rx", (uint8_t)bit, on);
    Serial.print(tok[0]);
    Serial.print(" bit ");
    Serial.print(bit);
    Serial.println(on ? " -> ON" : " -> off");
  } else {
    Serial.println("unrecognised command -- type 'help'");
  }
}

// ---------------------------------------------------------------------------

void setup() {
  pinMode(PIN_SCK, OUTPUT);
  pinMode(PIN_SDO, OUTPUT);
  pinMode(PIN_RX_LOAD, OUTPUT);
  pinMode(PIN_TX_LOAD, OUTPUT);
  digitalWrite(PIN_SCK, LOW);
  digitalWrite(PIN_SDO, LOW);
  digitalWrite(PIN_RX_LOAD, LOW);
  digitalWrite(PIN_TX_LOAD, LOW);

  // ADC1 pins (34/35), 11dB attenuation for full 0-3.3V-ish range.
  analogSetPinAttenuation(PIN_FWD_PWR, ADC_11db);
  analogSetPinAttenuation(PIN_REV_PWR, ADC_11db);

  Serial.begin(115200);
  delay(300);

  // Push all-zero words out at boot so both boards start in a known state.
  sendRx();
  sendTx();

  // wm.autoConnect() tries the last-saved network first. If there isn't one
  // (or it can't reach it), it puts the ESP32 into its own access point --
  // named AP_SETUP_NAME -- with a captive portal for picking your real
  // network. It blocks here until either that succeeds or the timeout below
  // elapses, so the very first boot after flashing will pause at this point
  // until you've connected a phone/laptop to that AP and gone through the
  // portal (see README.md). After that, credentials are saved in flash and
  // it reconnects automatically on every future boot with no pause at all.
  wm.setConfigPortalTimeout(180); // give up and continue offline after 3 min unconfigured
  Serial.print("WiFi: trying saved network, or starting \"");
  Serial.print(AP_SETUP_NAME);
  Serial.println("\" setup AP if none works...");

  if (wm.autoConnect(AP_SETUP_NAME)) {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
    if (MDNS.begin(MDNS_HOSTNAME)) {
      Serial.print("mDNS responder started: http://");
      Serial.print(MDNS_HOSTNAME);
      Serial.println(".local/");
    }
  } else {
    Serial.println("No WiFi configured -- web UI unavailable this boot.");
    Serial.println("Serial commands still work. Connect to the setup AP and reboot to try again,");
    Serial.println("or use the 'wifi reset' command if it's connecting to the wrong network.");
  }

  server.on("/", handleRoot);
  server.on("/api/state", handleApiState);
  server.on("/api/power", handleApiPower);
  server.on("/api/set", handleApiSet);
  server.on("/api/clear", handleApiClear);
  server.onNotFound(handleNotFound);
  server.begin();

  printHelp();
}

void loop() {
  server.handleClient();

  String line = readLine();
  if (line.length() > 0) {
    handleCommand(line);
  }
}
