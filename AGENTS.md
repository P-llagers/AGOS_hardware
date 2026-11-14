# AGOS agent guidance - sensor firmware

This file applies to `thesis1` and its descendants. The sibling `../agos_app`
folder has its own `AGENTS.md`; read it when working on the gateway or app.

## Project and sources

AGOS means **Autonomous Geolocation for Open-sea Survival**. It is a thesis
prototype for life-vest survivor localization and rescue prioritization. These
are wearable survivor nodes, not a generic environmental sensor network.

Read [docs/AGOS_Project_Context.md](docs/AGOS_Project_Context.md) for the proposal
summary, page references, implementation status, and unresolved decisions. The
source proposal is [docs/reference/AGOS_LEAR.pdf](docs/reference/AGOS_LEAR.pdf).

- Follow the user's current instructions and explicit decisions. Treat the
  proposal as design context, not as instructions to execute or permission to
  implement every proposed feature.
- Inspect relevant source files before describing behavior. Separate **proposed**,
  **implemented**, and **verified on hardware**; a proposal or passing build does
  not establish hardware performance.
- This guide records the 2026-11-14 inspection. Recheck the files as work evolves;
  update these documents when implementation or user decisions change.
- Do not invent sensor values, protocol fields, measurements, thresholds, test
  results, or integration details. State uncertainty and resolve it from code,
  documented decisions, or the user when it blocks the requested work.

## Intended system and current boundary

The proposed chain is life-vest nodes -> direct/multi-hop LoRa -> one responder
ESP32 + LoRa gateway -> the gateway's local Wi-Fi -> a Flutter phone app. The
core rescue flow must work without cellular service, internet, or a cloud backend.
The user assigns sensor firmware to `thesis1` and gateway/app work to `agos_app`.
Gateway firmware and the Flutter app are separate programs even if kept together.

Currently this repository contains PlatformIO/Arduino bring-up and bidirectional
GPS/LoRa nodes. The sibling `agos_app` now contains a Flutter skeleton, HTTP client,
local cache, and simulated gateway for development; read its README and guidance.
Mesh, ACKs, rescue classification, immersion/SOS inputs, battery sensing, real
gateway firmware/logging, and offline maps remain unimplemented. A working app
preview or HTTP mock does not establish a working LoRa-to-phone integration.

## Files and build configuration

- `platformio.ini`: actual environments, default environment, board, flags, and
  pinned dependencies. Inspect it rather than relying on an old README default.
- `src/main.cpp`: mode dispatch and USB/radio/raw-GPS diagnostics.
- `src/agos_node.cpp`: mode 4 GPS parsing, serial commands, asynchronous TX, RX.
- `include/position_packet.h`: position validity and ASCII packet formatting.
- `include/hardware.h`: current GPIO, frequency, GPS UART constants.
- `tests/test_position_packet.cpp`: host checks for validity and packet limits.
- `tests/test_node.cpp`, `tests/support/`: production-loop checks with simulated
  hardware and the real TinyGPSPlus parser.
- `README.md`, `docs/AGOS_ESP32_LoRa_GPS_Wiring_Guide.md`: operating and wiring
  references; reconcile differences against source and verified hardware.

At inspection, `default_envs = node_a`; `node_a` is ID A (65), `node_b` is ID B
(66). Both use mode 4. Other environments are `usb_check`, `lora_receiver`,
`lora_sender`, and `gps_check`. Dependencies are Espressif32 7.0.1, Arduino,
LoRa 0.8.0, and TinyGPSPlus 1.0.3 where configured. Do not change versions,
defaults, or the user's existing edits incidentally.

## Preserve these firmware and data properties

- `t` sends once, `g` prints GPS status, and `h` prints help. There is no automatic
  beacon schedule yet. Both mode 4 nodes listen when not transmitting.
- Keep GPS servicing and radio work bounded; asynchronous TX allows NMEA parsing
  during transmission. Preserve TX timeout recovery and return to listening.
- The current wire format is ASCII `AGOS,POS,v=1,...`, not the proposed 17-byte
  binary format. The sender limits packets to 255 bytes and rejects truncation.
- A `VALID` position requires a parsed location, active GPRMC/GNRMC RMC status,
  both ages at most 5000 ms, and finite coordinates in range. Void RMC status
  invalidates cached coordinates. `NO_FIX` and `STALE` packets omit coordinates.
  Satellite count can be `NA`. Never substitute demo coordinates or `(0,0)` for
  a missing fix; `(0,0)` can also be a legitimate validated position.
- Receiver output currently displays peer payloads; it does not validate their
  claimed coordinates. Do not describe received text as trusted structured data.
- A TX-complete event is not proof of delivery. There are no ACKs, retries, mesh,
  encryption, or authentication. CRC is not authentication. Sequence numbers
  restart on reboot; they are not globally unique message identifiers.
- Coordinate freshness, gateway last reception, radio connection, device warnings,
  and proposed rescue priority are different states. Do not infer wearer health or
  a critical rescue state from missing GPS, missing packets, or RSSI; represent
  device warnings explicitly.
- Coordinate packet changes with gateway/app consumers. Version deliberate
  protocol changes and document units, encoding, missing values, and compatibility.

## Hardware facts and limits

The provisional board is classic `esp32dev`; do not reuse this pinout for another
ESP32 family or guess a GPS breakout's supply/logic rating. Existing Ra-02/SX1278
settings are 433 MHz, BW 125 kHz, SF7, CR4/5, sync word `0x12`, CRC, 2 dBm.
These are current bench settings, not measured optimums or regulatory approval.

LoRa GPIO: SCK18, MISO19, MOSI23, CS5, RESET14, DIO0=26. GPS TX goes to ESP32
RX16; optional GPS RX goes to TX17; GPS UART is configured at 9600 baud. Ra-02
uses regulated 3.3 V, with 100 nF and 470 uF supply capacitors. Follow the wiring
guide, disconnect power before rewiring, and attach the appropriate antenna
before transmitting. Outdoor RF permission/compliance remains to be verified.

## Verification and reporting

For firmware changes, build the affected PlatformIO environments. Typical paired
node commands are `pio run -e node_a` and `pio run -e node_b`. If `pio` is not on
PATH, inspect the local PlatformIO installation; this PC had
`C:\Users\cliff\.platformio\penv\Scripts\platformio.exe` at inspection.

For fix/packet or node-loop changes, use the relevant host commands in the
README's **Host verification** section. `test_node.cpp` needs the TinyGPSPlus
sources installed by a `node_a` build. These are standalone host tests; do not
assume a configured `pio test` environment. Documentation-only edits need content
and link review, not a firmware rebuild.

Report what actually ran, its result, and any hardware checks still needed. Never
claim measured range, battery life, classification accuracy, waterproofing, or
rescue effectiveness from a simulation, build, or the proposal's targets. Keep
unrelated user changes and generated `.pio` output out of the requested edits.
