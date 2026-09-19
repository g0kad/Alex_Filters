#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// ALEX RCVR / TX control word bit assignments.
//
// Source: TAPR "ALEX Version P1-4.0 Schematics, Board Layout, Notes, Bill of
// Materials" documentation (web.tapr.org/product_docs/Alex/ALEX-docs.pdf).
//
// Each board is loaded with a 16-bit word. Bit 0 is shifted out FIRST, bit 15
// LAST. Control functions are positive logic (1 = ON / energised) unless
// noted otherwise. The word takes effect on the rising edge of that board's
// own LOAD strobe.
//
// IMPORTANT: the exact bit-to-function mapping WITHIN each group below (e.g.
// which of bits 11-8 is which band on the TX board) is transcribed from the
// document's descriptive text, not verified against your physical boards.
// Treat the names here as a starting hypothesis. Use the `rx <n> on/off` and
// `tx <n> on/off` raw bit commands in the firmware to energise one bit at a
// time, listen for the relay click / watch the VNA trace, and correct any
// names below that don't match what you observe on your two boards.
// ---------------------------------------------------------------------------

// ---- RX (RCVR) board, bit positions 0-15 ----
#define RX_LED_RED          0   // Bit 0  - red LED
#define RX_ATTEN_10DB       1   // Bit 1  - step attenuator, 10 dB section
#define RX_ATTEN_20DB       2   // Bit 2  - step attenuator, 20 dB section
#define RX_BYPASS           3   // Bit 3  - full bypass path
#define RX_OUT_SWITCH       4   // Bit 4  - RX1 output switch (0 = default receive path)
#define RX_PATH_RX1_IN      5   // Bit 5  - receive path select: RX 1 input
#define RX_PATH_RX2_IN      6   // Bit 6  - receive path select: RX 2 input
#define RX_PATH_XVTR        7   // Bit 7  - receive path select: transverter input
// Bit 8 - not connected
#define RX_HPF_1_5MHZ       9   // Bit 9  - 1.5 MHz high-pass filter
#define RX_HPF_6_5MHZ       10  // Bit 10 - 6.5 MHz high-pass filter
#define RX_HPF_9_5MHZ       11  // Bit 11 - 9.5 MHz high-pass filter
#define RX_6M_PREAMP        12  // Bit 12 - 6 m preamp (LNA) enable
#define RX_HPF_20MHZ        13  // Bit 13 - 20 MHz high-pass filter
#define RX_HPF_13MHZ        14  // Bit 14 - 13 MHz high-pass filter
#define RX_LED_YELLOW       15  // Bit 15 - yellow LED

// ---- TX board, bit positions 0-15 ----
#define TX_BAND_17_15M      0   // Bit 0  - 17/15 m band filter
#define TX_BAND_12_10M      1   // Bit 1  - 12/10 m band filter
#define TX_6M_BYPASS        2   // Bit 2  - 6 m bypass
#define TX_LED_RED          3   // Bit 3  - red LED
#define TX_TR_TRANSMIT      4   // Bit 4  - T/R relay, 1 = transmit, 0 = receive
#define TX_ANT3             5   // Bit 5  - antenna #3 select
#define TX_ANT2             6   // Bit 6  - antenna #2 select
#define TX_ANT1             7   // Bit 7  - antenna #1 select
#define TX_BAND_160M        8   // Bit 8  - 160 m band filter
#define TX_BAND_80M         9   // Bit 9  - 80 m band filter
#define TX_BAND_60_40M      10  // Bit 10 - 60/40 m band filter
#define TX_BAND_30_20M      11  // Bit 11 - 30/20 m band filter
#define TX_LED_YELLOW       12  // Bit 12 - yellow LED
// Bits 13-15 - not connected

struct BitName {
  uint8_t bit;
  const char *name;
};

static const BitName RX_BIT_NAMES[] = {
  {RX_LED_RED,     "led_red"},
  {RX_ATTEN_10DB,  "atten_10db"},
  {RX_ATTEN_20DB,  "atten_20db"},
  {RX_BYPASS,      "bypass"},
  {RX_OUT_SWITCH,  "out_switch"},
  {RX_PATH_RX1_IN, "path_rx1_in"},
  {RX_PATH_RX2_IN, "path_rx2_in"},
  {RX_PATH_XVTR,   "path_xvtr"},
  {RX_HPF_1_5MHZ,  "hpf_1_5mhz"},
  {RX_HPF_6_5MHZ,  "hpf_6_5mhz"},
  {RX_HPF_9_5MHZ,  "hpf_9_5mhz"},
  {RX_6M_PREAMP,   "6m_preamp"},
  {RX_HPF_20MHZ,   "hpf_20mhz"},
  {RX_HPF_13MHZ,   "hpf_13mhz"},
  {RX_LED_YELLOW,  "led_yellow"},
};

static const BitName TX_BIT_NAMES[] = {
  {TX_BAND_17_15M, "band_17_15m"},
  {TX_BAND_12_10M, "band_12_10m"},
  {TX_6M_BYPASS,   "6m_bypass"},
  {TX_LED_RED,     "led_red"},
  {TX_TR_TRANSMIT, "tr_transmit"},
  {TX_ANT3,        "ant3"},
  {TX_ANT2,        "ant2"},
  {TX_ANT1,        "ant1"},
  {TX_BAND_160M,   "band_160m"},
  {TX_BAND_80M,    "band_80m"},
  {TX_BAND_60_40M, "band_60_40m"},
  {TX_BAND_30_20M, "band_30_20m"},
  {TX_LED_YELLOW,  "led_yellow"},
};
