# ALEX filter board controller (ESP32)

Standalone controller for a pair of HPSDR "Alex" RF filter boards (RCVR P-1
Ver 4.0 + TX Ver P1.1), so they can be driven and characterised on a VNA
without the rest of an HPSDR stack (Mercury/Penelope/Metis).

## What this does

Both Alex boards are controlled over a shared 16-bit shift-register bus
(clock + data), with a separate load/latch strobe per board. This project
bit-bangs that bus from an ESP32 and gives you two interfaces onto the same
state: a serial-terminal command shell, and a small web page served over
WiFi with a toggle for every relay/LED bit plus a live forward/reverse power
readout. Use whichever's convenient -- they both drive the same boards.

It does **not** yet do anything automatic (no band-follows-radio logic, no
PTT following) -- it's still a manually-driven test jig. See "Next steps"
below for where this goes next (CAT/TCI-over-IP band following).

images\WebTX.jpg

## Hardware needed

- ESP32-WROOM-32 dev board
- A separate 12V supply for the Alex boards' relay coils (do **not** try to
  power them from the ESP32's 5V or 3.3V rail -- several relays energising
  at once will draw more than the ESP32 board can supply)
- Common ground between the ESP32, the 12V supply, and both Alex boards

## Wiring

The Alex 10-pin header (3M 3793-5002RB, 2x5, 0.1" pitch) pinout, and the
ESP32 GPIOs used in `src/main.cpp`:

| Alex pin | Signal              | ESP32 pin | Notes |
|----------|---------------------|-----------|-------|
| 1, 8, 10 | Ground              | GND       | common ground |
| 2        | +12V                | --        | from external 12V supply, not the ESP32 |
| 3        | SPI SDO (data)      | GPIO23    | |
| 4        | SPI SCK (clock)     | GPIO18    | |
| 5        | RX board load strobe| GPIO5     | |
| 6        | TX board load strobe| GPIO17    | |
| 7        | Fwd power (0-3V)    | GPIO34    | ADC1, input-only pin |
| 9        | Rev power (0-3V)    | GPIO35    | ADC1, input-only pin |

The Alex control inputs accept 3V CMOS logic directly, so the ESP32's 3.3V
GPIOs can drive them with no level shifting. Pins 34/35 are ADC1, input-only
pins, chosen deliberately since ADC2 conflicts with WiFi on the ESP32 (not
used here, but worth keeping in mind if you extend this later).

If you wire it differently, just change the pin numbers at the top of
`src/main.cpp`.

## Bit map: verified

`include/alex_bits.h` has a named bit for every relay/LED on both boards,
transcribed from the TAPR documentation's description of the control word.
Every bit has now been checked against the real hardware -- first by
energising one at a time and listening for the relay click, then confirmed
properly by sweeping each filter section on the VNA and matching the
passband it actually produces. The names in that header are correct.

The TX board's four "paired" band bits are named for *both* bands sharing
that LPF, but Mike's VNA test files are named for a single representative
band in each pair (whichever he happened to sweep at). For the record, the
mapping that the measured cutoff frequencies confirm is:

| Test file      | Bit name            | Bands sharing this LPF |
|-----------------|---------------------|-------------------------|
| `160m LPF`      | `TX_BAND_160M`      | 160m only |
| `80m LPF`       | `TX_BAND_80M`       | 80m only |
| `40m LPF`       | `TX_BAND_60_40M`    | 60m + 40m |
| `20m LPF`       | `TX_BAND_30_20M`    | 30m + 20m |
| `15m LPF`       | `TX_BAND_17_15M`    | 17m + 15m |
| `10m LPF`       | `TX_BAND_12_10M`    | 12m + 10m |

Each measured cutoff sits just above the *upper* edge of the higher band in
its pair (e.g. the "40m" LPF's 8.257 MHz cutoff clears both 60m's 5.4 MHz
and 40m's 7.3 MHz), which is exactly what you'd expect from a shared LPF
covering two adjacent bands, so no bit renaming was needed.

## WiFi setup

No credentials in source code -- this uses
[WiFiManager](https://github.com/tzapu/WiFiManager) to provision WiFi the
same way a smart plug or Chromecast does:

1. First boot after flashing, the ESP32 can't find a saved network, so it
   puts up its own WiFi access point called **ALEX-Filter-Setup**.
2. On your phone or laptop, connect to that network. You should get a
   "Sign in to network" / captive portal popup; if not, open a browser and
   go to `192.168.4.1`.
3. Pick your real WiFi network from the list, enter its password, and save.
   The ESP32 reboots, connects to that network, and remembers it in flash
   from then on -- no portal on future boots, it just connects straight
   away.
4. If it's ever connecting to the wrong network (new router, moved house,
   typo'd password), send `wifi reset` over the serial monitor -- it forgets
   the saved network and reboots straight back into setup mode.

It joins your existing network (station mode) rather than staying in its
own isolated hotspot permanently -- that's deliberate, so a PC or Raspberry
Pi elsewhere on your LAN can eventually talk to it too (see "Next steps").

If nothing configures it within 3 minutes of boot, the firmware gives up on
the portal and carries on anyway -- the serial commands still work, you
just won't get the web UI until it's configured and rebooted.

## Software setup (VS Code + PlatformIO)

1. Install [VS Code](https://code.visualstudio.com/).
2. Open the Extensions panel (the squares icon in the left sidebar) and
   install **PlatformIO IDE**.
3. Restart VS Code if it asks you to (PlatformIO needs this after first
   install).
4. File > Open Folder... and select this `alex-esp32-controller` folder.
5. Wait for PlatformIO to finish initialising -- the first time you open a
   project it downloads the ESP32 toolchain, which can take a few minutes.
   You'll see a checkmark or "Home" icon appear in the blue status bar at the
   bottom once it's ready. Since this update adds the WiFiManager library
   (see `platformio.ini`), the first build after pulling these changes will
   also fetch that library automatically -- needs internet access, same as
   the toolchain download.
6. Plug the ESP32 into USB.
7. Click the checkmark icon in the blue bottom status bar to **build**, then
   the right-arrow icon next to it to **build and upload**. If it can't find
   the board, you may need to select the right serial port from the plug
   icon, or install a USB-serial driver (CP2102 or CH340, depending on the
   board).
8. Click the plug icon in the status bar to open the **serial monitor**. Set
   it to 115200 baud if it isn't already (this project's `platformio.ini`
   sets that as the default).
9. Type `help` and press enter. It'll also print the web UI's address --
   either `http://alexctrl.local/` or a plain IP address if mDNS doesn't
   resolve on your PC.

## Serial commands

```
rx <bit> on|off     set one RX board bit (0-15)
tx <bit> on|off     set one TX board bit (0-15)
rx word <hex>       set the whole RX word directly, e.g. rx word 0A03
tx word <hex>       set the whole TX word directly, e.g. tx word 0000
rx clear / tx clear all bits off on that board
status              show both words and named bit states
pwr                 read forward/reverse power ADC pins (raw millivolts)
wifi reset          forget saved WiFi and reboot into setup mode
help                show this message
```

## Web UI

Open the address printed on the serial monitor in any browser on the same
network (phone, laptop, doesn't matter). It shows both boards' bits as named
toggle switches -- built straight from the same `alex_bits.h` names as the
serial shell, so fixing a name there fixes both UIs at once -- plus a live
forward/reverse power readout that refreshes every second.

The forward/reverse watt figures use the coupler formula from the TAPR ALEX
manual (P = V² / 0.09), and the displayed SWR is derived from those two
readings the normal way; treat both as approximate rather than
calibrated-instrument accurate. The coupler only means anything while the
TX board's `tr_transmit` bit is actually on -- in receive there's no RF
through it, so the ADC pins just float/bias to whatever they settle on. Both
the web UI and the `pwr` serial command check that bit first and show "RX
(not transmitting)" rather than a number that looks like a real reading but
isn't. There's no login/authentication on the page -- fine for a bench tool
on your home LAN, just don't port-forward it to the internet.

## Suggested characterisation workflow

1. Power up the ESP32 and both Alex boards (12V + common ground), connect
   the SV4401A's two ports to the relevant input/output BNCs for the section
   you're testing.
2. `rx clear` (or `tx clear`) to start from a known state.
3. Turn on one filter bit at a time (e.g. `rx 13 on` for what's hypothesised
   to be the 20 MHz HPF) and run an S21/S11 sweep. Confirm the passband edge
   matches what you'd expect, and correct the name in `alex_bits.h` if it
   turns out to be a different filter than labelled.
4. Repeat for each HPF/LPF/attenuator/LNA section on both boards, and keep
   notes -- this becomes your verified reference for future use.

## Filter characterisation results

Measured on the SV4401A, all sections in-circuit on the real boards (not
simulated). "Cutoff" is the instrument's -3 dB point from its LowPass/
HighPass sweep-analysis tool.

RX board (five HPF sections, cutoff is the *lower* passband edge, plus the
6m preamp): all five HPFs measured with cutoffs consistent with their
nominal labels (1.5/6.5/9.5/13/20 MHz), sensible elliptic-response notches,
low in-band insertion loss and good return loss, and the 6m preamp showed
the expected gain and passband. Full marker-by-marker figures are in
Mike's own notes (`Filter Responses/Claude RX Analysis.docx`).

TX board (LPFs -- cutoff is the *upper* passband edge, see bit-map table
above for which bands share each section):

| Test file  | Cutoff (-3dB) | -6dB point | -60dB point | Roll-off (dB/oct) |
|------------|---------------|------------|-------------|--------------------|
| 160m LPF   | 2.6645 MHz    | 2.777 MHz  | 3.9245 MHz  | 71.2 |
| 80m LPF    | 5.356 MHz     | 5.581 MHz  | 7.723 MHz   | 71.8 |
| 40m LPF    | 8.257 MHz     | 8.598 MHz  | 13.064 MHz  | 65.1 |
| 20m LPF    | 17.575 MHz    | 18.325 MHz | 23.845 MHz  | 226.4 (see note) |
| 15m LPF    | 27.125 MHz    | 28.7 MHz   | 38.95 MHz   | 85.8 |
| 10m LPF    | 35.55 MHz     | 37 MHz     | 52.6 MHz    | 79.5 |

All six passbands sit comfortably above their top ham band edge with
plenty of headroom before the 2nd-harmonic frequency, in-band insertion
loss is negligible and return loss is good (better than -15 dB away from
the band edges) on every section -- textbook 7th-order elliptic/Cauer LPF
behaviour. The 20m section's roll-off figure is an outlier and is most
likely the sweep-analysis tool's slope calculation being thrown off by a
transmission-zero notch sitting close to its -60dB search point, rather
than a real difference in that filter -- the raw trace shape looks the
same family as the rest.