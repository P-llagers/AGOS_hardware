# AGOS: two-node ESP32 bring-up

PlatformIO + Arduino starter for two classic ESP32 boards and two Ra-02
SX1278 433 MHz radios, following the supplied wiring guide. One ESP32 and one
radio are needed per node. GPS is optional during the radio test.

## 1. Confirm the boards

The provisional PlatformIO board is `esp32dev` (classic ESP32, typical 4 MB flash).
30-pin, CP2102 and Type-C describe the board layout and USB interface, not the
exact ESP32 module. Check the metal shield marking; report it before using a
different ESP32 family or a PSRAM module with GPIO16/17 occupied. This pin map
does not apply to ESP32-C3/S3 boards. Do not guess GPS supply voltage from its name.

VS Code, the PlatformIO extension, PlatformIO Core 6.2.0 and the Espressif32
platform were found on this PC. No reinstall is needed.

## 2. Start with USB only

1. Open `C:\thesis1` using VS Code **File > Open Folder**.
2. Disconnect peripherals and plug in only Node A using a USB data cable.
3. In PlatformIO's sidebar, open **Project Tasks > usb_check > General > Build**.
4. Select **Upload**, then **Monitor**. Use 115200 baud.
5. Confirm `AGOS alive` appears every two seconds. Repeat with Node B.

Use one connected board at a time initially. In PlatformIO's terminal, `pio device
list` lists serial ports. CP2102 commonly appears as a Silicon Labs USB UART.
If no port appears, check the cable and Windows Device Manager before changing
firmware. Close Serial Monitor before uploading if the port is busy. If upload
stalls at Connecting, hold BOOT, start upload, and release BOOT when writing starts.

Terminal alternative (PlatformIO terminal):

```powershell
pio run -e usb_check
pio device list
# Replace COMx with the port shown for the connected board:
pio run -e usb_check -t upload --upload-port COMx
pio device monitor -p COMx -b 115200
```

## 3. Wire one Ra-02 to each ESP32

Disconnect USB and all power before wiring. Connect by printed pin labels.

| Ra-02 signal | ESP32 |
|---|---|
| VCC / 3.3V | Regulated 3V3, never 5V/VIN |
| GND | GND |
| SCK | GPIO18 |
| MISO | GPIO19 |
| MOSI | GPIO23 |
| NSS / CS | GPIO5 |
| RESET | GPIO14 |
| DIO0 | GPIO26 |

Leave DIO1-DIO5 disconnected. Avoid forcing GPIO5 during boot. At each radio,
connect 100 nF ceramic and 470 uF electrolytic capacitors in parallel across
3V3/GND. The electrolytic positive goes to 3V3 and striped negative to GND.
Use short connections and check for shorts or split breadboard rails.
Confirm the ESP32 regulator can supply the connected devices; capacitors do not
replace adequate regulation. Attach a suitable 433 MHz antenna before transmitting.

## 4. Test the two-node radio link

1. Connect Node B and upload the **lora_receiver** environment. Check for
   `PASS: Ra-02 initialized`. Disconnect it after upload if you have one USB port.
2. Connect Node A and upload **lora_sender**. Check for the same initialization line.
3. Power both nodes with antennas attached. Open the sender monitor at 115200.
4. After verifying applicable RF operating requirements and test conditions,
   type `t` and press Enter to send one packet. The 2 dBm setting is a bench
   starting value, not a statement of permitted Philippine frequency/power use.
5. The receiver prints `AGOS,TEST,node=A,seq=1` plus RSSI and SNR. A sender's
   `TX complete` alone does not establish that the receiver received anything.
6. Send several packets, record their sequence numbers, and swap sender/receiver
   firmware to check both radio assemblies in both directions.

With two serial ports, use two monitors, explicitly selecting each COM port.
In separate PlatformIO terminals, for example:

```powershell
pio run -e lora_receiver -t upload --upload-port COMx
pio run -e lora_sender -t upload --upload-port COMy
pio device monitor -p COMx -b 115200
# In a second terminal:
pio device monitor -p COMy -b 115200
```

Replace COMx/COMy with actual ports. Both builds use 433 MHz, 125 kHz bandwidth,
SF7, coding rate 4/5, sync word 0x12 and CRC. These are raw point-to-point LoRa
packets; this starter does not implement LoRaWAN, mesh routing or acknowledgements.

If initialization fails, power off and recheck supply, ground, SPI, chip select,
reset and module identity. If initialization passes but packets fail, check both
builds, antennas, power stability and receiver output.

## 5. Check GPS separately after identifying its breakout

Verify the exact GPS breakout's VCC input rating and UART output logic level
first. ESP32 GPIO accepts 3.3 V logic. A breakout's supply input and UART signal
levels are separate specifications.

Connect GPS TX to GPIO16, GND to common ground, and VCC only to a verified
compatible supply. GPS RX to GPIO17 is optional for reading data; this firmware
sends no configuration commands. Upload **gps_check** and monitor at 115200.
The GPS UART starts at 9600 baud, which must match the actual breakout setting.
Look for NMEA lines such as `$G...`. Zero bytes suggests power/wiring/baud issues;
garbled text can indicate a baud mismatch. NMEA output can occur without a fix.
Acquire a fix with clear sky and follow up with parsed fix validation before
using coordinates in rescue packets. This diagnostic does not parse positions.

## Next integration milestone

After both radios pass and the GPS breakout is confirmed, add GPS parsing,
explicit fix validity and age, node IDs, sequence numbers and gateway logging.
Record missing packets and test conditions for thesis experiments. Mesh/ACK/TTL
work comes after the basic link is measured. Do not use sample coordinates as
real survivor locations.

References: [PlatformIO ESP32 board](https://docs.platformio.org/en/stable/boards/espressif32/esp32dev.html),
[LoRa library](https://github.com/sandeepmistry/arduino-LoRa).
