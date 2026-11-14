# AGOS — ESP32 + Ra-02 LoRa + Capacitors + NEO-M8N GPS Wiring Guide

**Purpose:** Hardware connection reference for an AI coding agent implementing the first AGOS survivor-node prototype. This document defines the intended GPIO mapping; **confirm the actual ESP32 board and GPS breakout specifications before applying power.**

## 1. Hardware and assumptions

- 1 × ESP32 DevKit (classic ESP32 with GPIO 16/17/18/19/23/5/14/26 accessible; **not** an ESP32-C3/S3 pinout)
- 1 × Ai-Thinker Ra-02, **SX1278**, 433 MHz LoRa **SPI** transceiver on a breakout board
- 1 × suitable **433 MHz antenna** connected to the Ra-02 U.FL/IPEX connector
- 1 × **100 nF ceramic capacitor** (non-polarized)
- 1 × **470 µF electrolytic capacitor**, rated **10–16 V or higher** (polarized)
- 1 × NEO-M8N GPS **breakout** with pins marked **VCC, RX, TX, GND**
- Jumper wires, breadboard, USB power cable; multimeter strongly recommended

**Scope:** One survivor node. For a basic two-node LoRa test, repeat the ESP32 + Ra-02 + capacitor wiring on the second ESP32; the GPS is optional for the second node. An ESP32 and a Ra-02 are required for each independently operating LoRa node or gateway.

## 2. Definitive pin mapping

### 2.1 Ra-02 (SX1278) LoRa → ESP32

| Ra-02 pin | ESP32 connection | Notes |
|---|---|---|
| `3.3V` / `VCC` | `3V3` **regulated** | **Never connect directly to 5V/VIN** |
| `GND` | `GND` | Common ground |
| `SCK` | `GPIO18` | VSPI clock |
| `MISO` | `GPIO19` | SPI data from LoRa to ESP32 |
| `MOSI` | `GPIO23` | SPI data from ESP32 to LoRa |
| `NSS` / `CS` | `GPIO5` | SPI chip select; ESP32 boot strapping pin, avoid forcing its level during reset |
| `RESET` / `RST` | `GPIO14` | Radio reset |
| `DIO0` | `GPIO26` | LoRa RX/TX-complete interrupt |
| `DIO1`, `DIO2`, `DIO3`, `DIO4`, `DIO5` | Not connected | Not needed by the initial Sandeep Mistry LoRa test |
| Any additional `GND` pins | Optional ground | At least one reliable ground connection is required |

**Note:** This pin map assumes the Ra-02 breakout pin labels correspond to the usual SX1278 module signals. Connect by **printed labels**, not by physical position in a photo.

### 2.2 NEO-M8N GPS (UART) → ESP32

| GPS breakout pin | ESP32 connection | Notes |
|---|---|---|
| `TX` | `GPIO16` (`RX2`) | GPS transmits serial data to ESP32 |
| `RX` | `GPIO17` (`TX2`) | ESP32 sends configuration data to GPS; optional if only reading position |
| `GND` | `GND` | Same common ground as ESP32 and LoRa |
| `VCC` | **VERIFY GPS BREAKOUT RATING FIRST** | Use `3V3` **only if** the breakout VCC input supports regulated 3.3 V. Some boards accept 5 V through an onboard regulator; others require a different input. Do not assume all NEO-M8N boards have identical VCC ratings. |

**GPS signal levels:** ESP32 GPIO is **3.3 V logic** and is **not 5 V tolerant**. Verify that GPS **TX output** does not exceed ESP32 input limits. If the breakout produces 5 V UART logic, use appropriate level shifting. The GPS module's internal chip voltage specification is not necessarily the same as the breakout board's `VCC` pin specification.

**UART crossing:** `GPS TX → ESP32 RX2` and `GPS RX ← ESP32 TX2`. Do **not** connect TX-to-TX.

## 3. Two capacitors across Ra-02 supply

Both capacitors are connected **in parallel between the local LoRa 3.3 V node and ground**, physically close to the radio module:

```text
                     +--------------------> Ra-02 3.3V / VCC
ESP32 regulated 3V3 --+-------+----------------+
                      |       |                |
                  [100 nF] [470 µF]         (radio)
                  ceramic   electrolytic      |
                      |       |                |
ESP32 GND ------------+-------+----------------+----> Ra-02 GND
                              |
                       electrolytic (-)

470 µF positive (+) -> 3.3 V
470 µF negative (-) -> GND
100 nF ceramic -> either orientation
```

- **100 nF ceramic:** short leads/short wires; high-frequency decoupling.
- **470 µF electrolytic:** local bulk energy storage for supply transients. Positive leg goes to **3.3 V**, negative marked stripe goes to **GND**.
- **Do not** connect either capacitor in series with the radio's VCC line.
- Capacitors **do not replace** a capable 3.3 V regulator. Check the ESP32 board regulator's available current, accounting for ESP32 + LoRa + GPS simultaneously. If voltage dips occur, use a suitable dedicated regulated 3.3 V supply with common ground.
- Capacitor voltage rating (10–16 V) is a **maximum operating rating**, not a reason to feed 10–16 V into the LoRa.

## 4. Wiring overview

```text
                          ESP32 DevKit
                        +--------------+
 USB power ------------>| USB          |
                        |              |
  Ra-02 3.3V -----------| 3V3          |----+---- 100 nF -----+
  Ra-02 GND ------------| GND          |----+---- 470 uF -----+ (parallel, to 3V3/GND)
  Ra-02 NSS ------------| GPIO5        |
  Ra-02 RESET ----------| GPIO14       |
  Ra-02 SCK ------------| GPIO18       |
  Ra-02 MISO -----------| GPIO19       |
  Ra-02 MOSI -----------| GPIO23       |
  Ra-02 DIO0 -----------| GPIO26       |
                        |              |
  GPS TX -------------->| GPIO16 / RX2 |
  GPS RX <--------------| GPIO17 / TX2 |
  GPS GND --------------| GND          |
  GPS VCC --------------| 3V3 (ONLY if verified compatible)
                        +--------------+

  Ra-02 antenna: connect suitable 433 MHz antenna at U.FL/IPEX socket.
```

The text diagram is schematic, not a physical breadboard placement instruction. On a breadboard, verify rails, split rail segments, pin isolation, and continuity with a meter.

## 5. Arduino / ESP32 firmware constants

```cpp
#include <SPI.h>
#include <LoRa.h> // LoRa by Sandeep Mistry

// LoRa SX1278 (SPI)
constexpr int LORA_SS   = 5;
constexpr int LORA_RST  = 14;
constexpr int LORA_DIO0 = 26;
constexpr int LORA_SCK  = 18;
constexpr int LORA_MISO = 19;
constexpr int LORA_MOSI = 23;
constexpr long LORA_FREQ = 433E6;

// GPS NEO-M8N (UART2)
constexpr int GPS_RX_PIN = 16; // ESP32 RX <- GPS TX
constexpr int GPS_TX_PIN = 17; // ESP32 TX -> GPS RX
constexpr uint32_t GPS_BAUD = 9600; // common default; verify breakout configuration
HardwareSerial GPSSerial(2);

void setup() {
  Serial.begin(115200);
  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("ERROR: Ra-02 init failed; check wiring/power");
    while (true) delay(1000);
  }

  // Set matching modem parameters on all communicating radios.
  LoRa.setSignalBandwidth(125E3);
  LoRa.setSpreadingFactor(7);
  LoRa.setCodingRate4(5);
  Serial.println("LoRa initialized");
}

void loop() {
  // Next step: parse NMEA from GPSSerial (e.g. using TinyGPSPlus)
  // and transmit a minimal location packet over LoRa.
}
```

**Note on 433 MHz operation:** Do not assume every SX1278 transmit-power setting is legally permitted in the Philippines. Check NTC frequency, radiated-power and other requirements before transmitting, especially outdoors. For initial bring-up, choose compliant test conditions and RF settings.

## 6. Power-on safety checklist

- [ ] **Disconnect USB/power** before wiring or moving components.
- [ ] Confirm this is a **3.3 V Ra-02 SPI** module and verify each printed pin label.
- [ ] Confirm the **GPS breakout's VCC and UART logic-level ratings** before powering/connecting it.
- [ ] Check that `3V3` and `GND` are **not shorted**.
- [ ] Verify **470 µF positive to 3V3** and **striped negative to GND**.
- [ ] Put the **100 nF ceramic close to LoRa VCC/GND**.
- [ ] Connect the **433 MHz antenna before LoRa transmission**; never intentionally transmit without an appropriate antenna/load.
- [ ] Verify the ESP32's supply voltage **remains stable under TX bursts**; use a more capable 3.3 V regulator if needed.
- [ ] Keep the GPS and LoRa grounds shared.
- [ ] Test LoRa initialization **before** adding GPS data parsing.
- [ ] Test GPS NMEA reception on UART2 separately; acquire GPS fix outdoors with clear sky.

## 7. Suggested validation for AI agent

1. **LoRa bring-up:** print whether `LoRa.begin(433E6)` succeeds. If it fails, inspect 3.3 V, GND, SPI pins, NSS, RESET, antenna and module identity.
2. **Point-to-point test:** run two ESP32 + Ra-02 assemblies, send sequence-numbered packets, print packet payload, RSSI and SNR at the receiver.
3. **GPS UART test:** read `GPSSerial` at the configured baud and confirm valid NMEA sentences; then parse latitude/longitude and fix validity. GPS may output data before achieving a position fix.
4. **Integration:** only transmit *valid* GPS coordinates (or explicitly label missing fix). Do not present placeholder coordinates as a real rescue position.
5. **Later mesh:** add node ID, sequence number, TTL, duplicate suppression, random backoff, ACK handling, and gateway logging after the initial RF link works.

## 8. Open hardware questions to resolve

- Exact model and photograph of **NEO-M8N breakout** (especially its input-voltage/regulator and logic-level markings).
- Exact **ESP32 DevKit board variant** and onboard 3.3 V regulator capacity.
- Type of **433 MHz antennas** and suitability for the Ra-02 connector.
- Whether the breadboard's **power rails are split** and how they are bridged.

**Do not silently guess these items in code or wiring.**
