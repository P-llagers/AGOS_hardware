#include <Arduino.h>

#if defined(AGOS_MODE) && AGOS_MODE == 4
#include <SPI.h>
#include <LoRa.h>
#include <TinyGPS++.h>
#include "agos_node.h"
#include "hardware.h"
#include "position_packet.h"

#ifndef AGOS_NODE_ID
#define AGOS_NODE_ID 65
#endif

namespace {
constexpr char NODE_ID = static_cast<char>(AGOS_NODE_ID);
constexpr uint32_t TX_TIMEOUT_MS = 3000;
HardwareSerial gpsSerial(2);
TinyGPSPlus gps;
// TinyGPSPlus retains its last location after fix loss. Track current RMC status too.
TinyGPSCustom gpStatus(gps, "GPRMC", 2);
TinyGPSCustom gnStatus(gps, "GNRMC", 2);
bool statusKnown = false;
bool statusActive = false;
uint32_t statusAt = 0;
bool radioReady = false;
bool transmitting = false;
volatile bool txDone = false;
uint32_t txStartedAt = 0;
uint32_t sequence = 0;
uint32_t received = 0;
uint32_t lastStatusAt = 0;
char outgoing[256];

void IRAM_ATTR onTxDone() {
  txDone = true;
}

void resumeListening() {
  // RX is polled. Detach the TX-only interrupt before DIO0 becomes RX_DONE,
  // otherwise the library's ISR would consume RX flags before parsePacket().
  LoRa.onTxDone(nullptr);
  LoRa.receive();
}

void readStatus(TinyGPSCustom &status) {
  if (!status.isUpdated()) return;
  statusActive = status.value()[0] == 'A';
  statusKnown = true;
  statusAt = millis();
}

void pollGps() {
  for (int i = 0; i < 512 && gpsSerial.available(); ++i) {
    gps.encode(static_cast<char>(gpsSerial.read()));
    readStatus(gpStatus);
    readStatus(gnStatus);
  }
}

agos::Position position() {
  agos::Position p;
  p.locationValid = gps.location.isValid();
  p.locationAge = gps.location.age();
  p.statusKnown = statusKnown;
  p.statusActive = statusActive;
  p.statusAge = statusKnown ? millis() - statusAt : UINT32_MAX;
  p.latitude = gps.location.lat();
  p.longitude = gps.location.lng();
  p.satellitesValid = gps.satellites.isValid() &&
                      gps.satellites.age() <= agos::MAX_FIX_AGE_MS;
  p.satellites = gps.satellites.value();
  return p;
}

void printStatus() {
  const agos::Position p = position();
  char packet[256];
  agos::formatPosition(packet, sizeof(packet), NODE_ID, sequence, p);
  Serial.printf("LOCAL %s | gps_chars=%lu checksum_ok=%lu checksum_bad=%lu | radio=%s rx=%lu\n",
      packet, (unsigned long)gps.charsProcessed(),
      (unsigned long)gps.passedChecksum(), (unsigned long)gps.failedChecksum(),
      !radioReady ? "FAILED" : transmitting ? "TX" : "LISTENING",
      (unsigned long)received);
  if (gps.charsProcessed() == 0)
    Serial.println("GPS: no UART data; check power, GPS TX -> GPIO16 and baud.");
  else if (gps.passedChecksum() == 0)
    Serial.println("GPS: bytes received but no valid NMEA; check baud and signal levels.");
  else if (!statusKnown)
    Serial.println("GPS: waiting for GPRMC/GNRMC status; RMC output must be enabled.");
}

void pollRadio() {
  if (!radioReady) return;
  if (transmitting) {
    if (txDone) {
      transmitting = false;
      resumeListening();
      Serial.printf("TX complete: %s\nLISTENING (delivery requires peer RX output)\n", outgoing);
    } else if (millis() - txStartedAt > TX_TIMEOUT_MS) {
      LoRa.idle();
      transmitting = false;
      resumeListening();
      Serial.println("TX timeout: returned to listening; packet delivery unknown.");
    }
    return;
  }
  const int size = LoRa.parsePacket();
  if (!size) return;
  char payload[256];
  size_t count = 0;
  while (LoRa.available() && count < sizeof(payload) - 1)
    payload[count++] = static_cast<char>(LoRa.read());
  payload[count] = '\0';
  const int rssi = LoRa.packetRssi();
  const float snr = LoRa.packetSnr();
  LoRa.receive();
  ++received;
  // Print received data as data, never as a format string.
  Serial.printf("RX #%lu bytes=%d RSSI=%d dBm SNR=%.1f dB | ",
                (unsigned long)received, size, rssi, snr);
  Serial.write(reinterpret_cast<const uint8_t *>(payload), count);
  Serial.println();
}

void sendPosition() {
  if (!radioReady) { Serial.println("Cannot send: radio initialization failed."); return; }
  if (transmitting) { Serial.println("TX busy; wait and type t again."); return; }
  const size_t length = agos::formatPosition(outgoing, sizeof(outgoing), NODE_ID,
                                            sequence + 1, position());
  if (!length) { Serial.println("Cannot send: packet exceeds radio limit."); return; }
  if (!LoRa.beginPacket()) { Serial.println("Radio busy; try again."); return; }
  if (LoRa.write(reinterpret_cast<const uint8_t *>(outgoing), length) != length) {
    LoRa.receive();
    Serial.println("Cannot send: incomplete packet write.");
    return;
  }
  // Async TX keeps the main loop consuming GPS bytes during airtime.
  txDone = false;
  LoRa.onTxDone(onTxDone);
  if (!LoRa.endPacket(true)) {
    resumeListening();
    Serial.println("Cannot start transmission.");
    return;
  }
  ++sequence;
  transmitting = true;
  txStartedAt = millis();
  Serial.printf("TX started: node=%c seq=%lu\n", NODE_ID, (unsigned long)sequence);
}
} // namespace

void setupNode() {
  Serial.begin(115200);
  delay(1000);
  Serial.printf("\nAGOS node %c | bidirectional LoRa + GPS\n", NODE_ID);
  gpsSerial.setRxBufferSize(2048);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  radioReady = LoRa.begin(LORA_FREQUENCY_HZ);
  if (radioReady) {
    LoRa.setSignalBandwidth(125000);
    LoRa.setSpreadingFactor(7);
    LoRa.setCodingRate4(5);
    LoRa.setSyncWord(0x12);
    LoRa.enableCrc();
    LoRa.setTxPower(2);
    LoRa.receive();
    Serial.println("PASS: Ra-02 initialized at 433 MHz; BW125/SF7/CR4:5/CRC. LISTENING.");
  } else {
    Serial.println("FAIL: Ra-02 initialization. GPS diagnostics remain available.");
  }
  Serial.println("t: send GPS/status packet | g: local GPS status | h: help");
  Serial.println("Connect antennas and verify RF test conditions before sending. No automatic TX.");
}

void loopNode() {
  pollGps();
  pollRadio();
  // Limit console work so pasted input cannot starve GPS and radio servicing.
  for (int i = 0; i < 32 && Serial.available(); ++i) {
    switch (Serial.read()) {
      case 't': case 'T': sendPosition(); break;
      case 'g': case 'G': printStatus(); break;
      case 'h': case 'H':
        Serial.println("t: send once | g: GPS status | h: help. Both nodes listen while idle.");
        break;
      default: break;
    }
  }
  if (millis() - lastStatusAt >= 5000) {
    lastStatusAt = millis();
    printStatus();
  }
}
#endif
