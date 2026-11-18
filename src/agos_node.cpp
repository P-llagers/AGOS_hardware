#include <Arduino.h>

#if defined(AGOS_MODE) && AGOS_MODE == 4
#include <SPI.h>
#include <LoRa.h>
#include <TinyGPS++.h>
#include "agos_node.h"
#include "hardware.h"
#include "link_stats.h"
#include "position_packet.h"
#include "test_log.h"

#ifndef AGOS_NODE_ID
#define AGOS_NODE_ID 65
#endif

namespace {
constexpr char NODE_ID = static_cast<char>(AGOS_NODE_ID);
constexpr uint32_t TX_TIMEOUT_MS = 3000;
constexpr uint32_t GPS_LOG_INTERVAL_MS = 1000;
constexpr size_t MAX_PEERS = 8;
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
size_t outgoingLength = 0;
const char *outgoingFix = "";

uint16_t session = 0;
agos::Console console("mM");
bool gpsLogging = false;
uint32_t lastGpsLogAt = 0;
agos::FixState lastFix = agos::FixState::NoFix;
bool ttffLogged = false;

// Per-peer reception accounting. Mode 4 packets carry no run size, so the
// expected count is the span of sequence numbers heard ("seq_span").
struct Peer {
  char node;
  agos::LinkRun run;
};
Peer peers[MAX_PEERS];
size_t peerCount = 0;

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

bool hdopFresh() {
  return gps.hdop.isValid() && gps.hdop.age() <= agos::MAX_FIX_AGE_MS;
}

void printHeaders() {
  agos::printCommonHeaders();
  agos::printHeader("TX", "ms,session,node,seq,fix,bytes,tx_ms,toa_calc_ms,result");
  agos::printHeader("RX", "ms,session,src_node,seq,peer_fix,bytes,rssi_dbm,snr_db,ferr_hz,"
                          "result,local_fix,local_lat,local_lon,peer_lat,peer_lon,"
                          "dist_m,payload");
  agos::printSumHeader();
  agos::printHeader("GPS", "ms,session,node,utc_date,utc_time,fix,lat,lon,alt_m,hdop,sat,"
                           "loc_age_ms,gps_chars,checksum_ok,checksum_bad");
  agos::printHeader("FIX", "ms,session,node,from,to,sat,hdop");
  agos::printHeader("TTFF", "ms,session,node,ttff_ms,sat,hdop");
}

void printConfig() {
  printHeaders();
  agos::printCfg(session, AGOS_MODE, "node", NODE_ID, radioReady);
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

// One GPS sample. Coordinates appear only for a VALID fix; optional values are
// empty when the receiver has not reported them within MAX_FIX_AGE_MS.
void printGpsRow() {
  const agos::Position p = position();
  const agos::FixState state = agos::fixState(p);
  Serial.printf("GPS,%lu,%04X,%c,", (unsigned long)millis(), session, NODE_ID);
  if (gps.date.isValid() && gps.date.age() <= agos::MAX_FIX_AGE_MS)
    Serial.printf("%04u-%02u-%02u", (unsigned)gps.date.year(), (unsigned)gps.date.month(),
                  (unsigned)gps.date.day());
  Serial.printf(",");
  if (gps.time.isValid() && gps.time.age() <= agos::MAX_FIX_AGE_MS)
    Serial.printf("%02u:%02u:%02u.%02u", (unsigned)gps.time.hour(), (unsigned)gps.time.minute(),
                  (unsigned)gps.time.second(), (unsigned)gps.time.centisecond());
  Serial.printf(",%s,", agos::fixName(state));
  if (state == agos::FixState::Valid)
    Serial.printf("%.7f,%.7f", p.latitude, p.longitude);
  else
    Serial.printf(",");
  agos::printOptional(gps.altitude.isValid() && gps.altitude.age() <= agos::MAX_FIX_AGE_MS,
                      "%.1f", gps.altitude.meters());
  agos::printOptional(hdopFresh(), "%.2f", gps.hdop.hdop());
  agos::printOptional(p.satellitesValid, "%.0f", p.satellites);
  agos::printOptional(p.locationValid, "%.0f", p.locationAge);
  Serial.printf(",%lu,%lu,%lu\n", (unsigned long)gps.charsProcessed(),
                (unsigned long)gps.passedChecksum(), (unsigned long)gps.failedChecksum());
}

void printSatHdop(const agos::Position &p) {
  agos::printOptional(p.satellitesValid, "%.0f", p.satellites);
  agos::printOptional(hdopFresh(), "%.2f", gps.hdop.hdop());
  Serial.println();
}

// Fix-state changes and the first VALID fix since boot (time to first fix).
// TTFF counts from ESP32 start; whether that was a cold, warm or hot start
// depends on how the GPS module was powered, so record that with a NOTE.
void trackFix() {
  const agos::Position p = position();
  const agos::FixState state = agos::fixState(p);
  if (state == lastFix) return;
  Serial.printf("FIX,%lu,%04X,%c,%s,%s", (unsigned long)millis(), session, NODE_ID,
                agos::fixName(lastFix), agos::fixName(state));
  printSatHdop(p);
  if (state == agos::FixState::Valid && !ttffLogged) {
    ttffLogged = true;
    Serial.printf("TTFF,%lu,%04X,%c,%lu", (unsigned long)millis(), session, NODE_ID,
                  (unsigned long)millis());
    printSatHdop(p);
  }
  lastFix = state;
}

Peer *findPeer(char node) {
  for (size_t i = 0; i < peerCount; ++i)
    if (peers[i].node == node) return &peers[i];
  if (peerCount == MAX_PEERS) return nullptr;
  peers[peerCount].node = node;
  peers[peerCount].run = agos::LinkRun();
  return &peers[peerCount++];
}

void logTx(const char *result) {
  Serial.printf("TX,%lu,%04X,%c,%lu,%s,%u,", (unsigned long)millis(), session, NODE_ID,
                (unsigned long)sequence, outgoingFix, (unsigned)outgoingLength);
  // Measured from endPacket() to the loop noticing TX_DONE; empty on timeout.
  if (strcmp(result, "ok") == 0) Serial.printf("%lu", (unsigned long)(millis() - txStartedAt));
  Serial.printf(",%.1f,%s\n", agos::timeOnAirCalcMs(outgoingLength), result);
}

void logReceived(const char *payload, size_t count, int size, int rssi, float snr, long ferr) {
  const uint32_t now = millis();
  agos::PosPacketInfo info;
  const bool parsed = strlen(payload) == count && agos::parsePosPacket(payload, info);
  const char *result = "unknown";
  if (parsed) {
    Peer *peer = findPeer(info.node);
    if (!peer) {
      result = "untracked";
    } else {
      agos::LinkRun &run = peer->run;
      if (run.active && info.seq < run.seq.base) {
        agos::printSum(session, run, "restart");  // sender rebooted
        run.active = false;
      }
      if (!run.active) run.start(info.node, -1, -1, info.seq, 0, now);
      result = agos::seqResultName(run.record(info.seq, rssi, snr, ferr, now));
    }
  }

  const agos::Position local = position();
  const bool localValid = agos::fixState(local) == agos::FixState::Valid;
  Serial.printf("RX,%lu,%04X,", (unsigned long)now, session);
  if (parsed) Serial.printf("%c,%lu,%s", info.node, info.seq, info.fix);
  else Serial.printf(",,");
  Serial.printf(",%d,%d,%.2f,%ld,%s,%s,", size, rssi, snr, ferr, result,
                agos::fixName(agos::fixState(local)));
  if (localValid) Serial.printf("%.7f,%.7f", local.latitude, local.longitude);
  else Serial.printf(",");
  Serial.printf(",");
  if (parsed && info.hasCoords) Serial.printf("%.6f,%.6f", info.lat, info.lon);
  else Serial.printf(",");
  // Calculated from this node's fix and the peer's claimed (unverified) fix.
  agos::printOptional(parsed && info.hasCoords && localValid, "%.1f",
                      agos::gpsDistanceM(local.latitude, local.longitude, info.lat, info.lon));
  Serial.printf(",");
  agos::printQuoted(payload, count);
  Serial.println();
}

void pollRadio() {
  if (!radioReady) return;
  if (transmitting) {
    if (txDone) {
      transmitting = false;
      resumeListening();
      Serial.printf("TX complete: %s\nLISTENING (delivery requires peer RX output)\n", outgoing);
      logTx("ok");
    } else if (millis() - txStartedAt > TX_TIMEOUT_MS) {
      LoRa.idle();
      transmitting = false;
      resumeListening();
      Serial.println("TX timeout: returned to listening; packet delivery unknown.");
      logTx("timeout");
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
  const long ferr = LoRa.packetFrequencyError();
  LoRa.receive();
  ++received;
  // Print received data as data, never as a format string.
  Serial.printf("RX #%lu bytes=%d RSSI=%d dBm SNR=%.1f dB | ",
                (unsigned long)received, size, rssi, snr);
  Serial.write(reinterpret_cast<const uint8_t *>(payload), count);
  Serial.println();
  logReceived(payload, count, size, rssi, snr, ferr);
}

void sendPosition() {
  if (!radioReady) { Serial.println("Cannot send: radio initialization failed."); return; }
  if (transmitting) { Serial.println("TX busy; wait and type t again."); return; }
  const agos::Position p = position();
  const size_t length = agos::formatPosition(outgoing, sizeof(outgoing), NODE_ID,
                                            sequence + 1, p);
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
  outgoingLength = length;
  outgoingFix = agos::fixName(agos::fixState(p));
  Serial.printf("TX started: node=%c seq=%lu\n", NODE_ID, (unsigned long)sequence);
}

void printSums(const char *status) {
  if (!peerCount) { Serial.println("No peer packets yet."); return; }
  for (size_t i = 0; i < peerCount; ++i) agos::printSum(session, peers[i].run, status);
}

void handleKey(char key) {
  switch (key) {
    case 't': case 'T': sendPosition(); break;
    case 'g': case 'G': printStatus(); break;
    case 'h': case 'H':
      Serial.println("t: send once | g: GPS status | h: help. Both nodes listen while idle.");
      Serial.println("l: GPS log on/off (1 Hz) | s: peer summary | r: reset peer counters");
      Serial.println("f: noise floor (peer idle) | c: print config | m <text>+Enter: test note");
      break;
    case 'l': case 'L':
      gpsLogging = !gpsLogging;
      Serial.printf("GPS logging %s\n", gpsLogging ? "ON (GPS rows every 1 s)" : "OFF");
      break;
    case 's': case 'S': printSums("partial"); break;
    case 'r': case 'R':
      printSums("reset");
      peerCount = 0;
      Serial.println("--- peer counters reset ---");
      break;
    case 'c': case 'C': printConfig(); break;
    case 'f': case 'F':
      if (!radioReady) Serial.println("Noise sample skipped: radio not initialized.");
      else if (transmitting) Serial.println("Noise sample skipped: TX in progress.");
      else agos::sampleNoise(session, NODE_ID, pollGps);
      break;
    default: break;
  }
}
} // namespace

void setupNode() {
  Serial.begin(115200);
  delay(1000);
  Serial.printf("\nAGOS node %c | bidirectional LoRa + GPS\n", NODE_ID);
  session = agos::newSessionId();
  gpsSerial.setRxBufferSize(2048);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  radioReady = LoRa.begin(LORA_FREQUENCY_HZ);
  if (radioReady) {
    agos::applyRadioConfig();
    LoRa.receive();
    Serial.println("PASS: Ra-02 initialized. LISTENING.");
    agos::printRadioSummary();
  } else {
    Serial.println("FAIL: Ra-02 initialization. GPS diagnostics remain available.");
  }
  printConfig();
  Serial.println("t: send GPS/status packet | g: local GPS status | h: help");
  Serial.println("Connect antennas and verify RF test conditions before sending. No automatic TX.");
}

void loopNode() {
  pollGps();
  pollRadio();
  // Limit console work so pasted input cannot starve GPS and radio servicing.
  for (int i = 0; i < 32 && Serial.available(); ++i) {
    char key = 0;
    const int event = console.feed(Serial.read(), key);
    if (event == 1) handleKey(key);
    else if (event == 2) agos::printNote(session, NODE_ID, console.argument());
  }
  trackFix();
  if (gpsLogging && millis() - lastGpsLogAt >= GPS_LOG_INTERVAL_MS) {
    lastGpsLogAt = millis();
    printGpsRow();
  }
  if (millis() - lastStatusAt >= 5000) {
    lastStatusAt = millis();
    printStatus();
  }
}
#endif
