#pragma once
// Machine-readable test log shared by lora_receiver, lora_sender and node_a/node_b.
//
// Every data line is CSV whose first field is its row type. Each firmware
// prints "HDR,<type>,<column names>" lines at boot and on the `c` command, so
// a saved log describes its own columns. Missing values are empty fields.
// Human-readable lines are still printed; they never start with an upper-case
// row type followed by a comma, so analysis can keep only CSV rows.
//
// Row values are either radio/GPS readings, values calculated from them (column
// names ending in _calc, or dist_m), configured settings (CFG), or text typed by
// the operator (NOTE). Nothing in this file creates measurement values.

#include <Arduino.h>
#include <LoRa.h>
#include "hardware.h"
#include "link_stats.h"

namespace agos {
constexpr int LOG_SCHEMA = 1;  // bump when a column changes

inline uint16_t newSessionId() {
  // Distinguishes boots in the same log; restarts are otherwise invisible
  // because sequence numbers also restart.
  return static_cast<uint16_t>(esp_random() & 0xFFFFu);
}

inline void applyRadioConfig() {
  LoRa.setSignalBandwidth(LORA_BANDWIDTH_HZ);
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setCodingRate4(LORA_CODING_RATE_DENOM);
  LoRa.setPreambleLength(LORA_PREAMBLE_SYMBOLS);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  if (LORA_CRC_ENABLED) LoRa.enableCrc(); else LoRa.disableCrc();
  LoRa.setTxPower(LORA_TX_POWER_DBM);
}

inline void printRadioSummary() {
  Serial.printf("Radio: %.3f MHz BW%ld kHz SF%d CR4/%d preamble=%ld sync=0x%02X CRC=%s TX=%d dBm (configured)\n",
                LORA_FREQUENCY_HZ / 1e6, LORA_BANDWIDTH_HZ / 1000, LORA_SPREADING_FACTOR,
                LORA_CODING_RATE_DENOM, LORA_PREAMBLE_SYMBOLS, LORA_SYNC_WORD,
                LORA_CRC_ENABLED ? "on" : "off", LORA_TX_POWER_DBM);
}

inline void printHeader(const char *type, const char *columns) {
  Serial.printf("HDR,%s,%s\n", type, columns);
}

inline void printCommonHeaders() {
  printHeader("CFG", "ms,session,schema,mode,role,node,radio_ok,freq_hz,bw_hz,sf,cr,"
                     "preamble,sync_word,crc,tx_dbm_cfg,build");
  printHeader("NOTE", "ms,session,node,text");
  printHeader("NOISE", "ms,session,node,samples,rssi_min_dbm,rssi_mean_dbm,rssi_max_dbm");
}

inline void printCfg(uint16_t session, int mode, const char *role, char node, bool radioOk) {
  Serial.printf("CFG,%lu,%04X,%d,%d,%s,%c,%d,%ld,%ld,%d,4/%d,%ld,0x%02X,%d,%d,%s %s\n",
                (unsigned long)millis(), session, LOG_SCHEMA, mode, role, node,
                radioOk ? 1 : 0, LORA_FREQUENCY_HZ, LORA_BANDWIDTH_HZ,
                LORA_SPREADING_FACTOR, LORA_CODING_RATE_DENOM, LORA_PREAMBLE_SYMBOLS,
                LORA_SYNC_WORD, LORA_CRC_ENABLED ? 1 : 0, LORA_TX_POWER_DBM,
                __DATE__, __TIME__);
}

// Prints `text` as a quoted CSV field (see csvQuote) without a format string.
inline void printQuoted(const char *text, size_t length) {
  char quoted[1100];
  const size_t n = csvQuote(text, length, quoted, sizeof(quoted));
  Serial.write(reinterpret_cast<const uint8_t *>(quoted), n);
}

// Operator-entered test conditions, e.g. "m dist=200m h=1.5m LOS sea=calm".
inline void printNote(uint16_t session, char node, const char *text) {
  Serial.printf("NOTE,%lu,%04X,%c,", (unsigned long)millis(), session, node);
  printQuoted(text, strlen(text));
  Serial.println();
}

inline void printOptional(bool present, const char *format, double value) {
  Serial.printf(",");
  if (present) Serial.printf(format, value);
}

// Samples the radio's current RSSI register while listening. With no packet on
// the air this is the channel noise/interference level. Run it while the other
// board is NOT transmitting; samples during a packet would read that packet.
// `service` keeps time-critical work (GPS parsing) running during sampling.
inline void sampleNoise(uint16_t session, char node, void (*service)() = nullptr) {
  constexpr int SAMPLES = 50;
  LoRa.receive();
  delay(5);
  RunningStats s;
  for (int i = 0; i < SAMPLES; ++i) {
    s.add(LoRa.rssi());
    if (service) service();
    delay(10);
  }
  Serial.printf("NOISE,%lu,%04X,%c,%lu,%.0f,%.1f,%.0f\n", (unsigned long)millis(), session,
                node, (unsigned long)s.n, s.min, s.mean, s.max);
}

inline void printSumHeader() {
  printHeader("SUM", "ms,session,src_node,src_session,run,status,expected,expected_basis,"
                     "received,lost,duplicates,out_of_order,out_of_range,pdr_pct,"
                     "rssi_min_dbm,rssi_mean_dbm,rssi_max_dbm,rssi_sd_db,"
                     "snr_min_db,snr_mean_db,snr_max_db,snr_sd_db,ferr_mean_hz,"
                     "first_rx_ms,last_rx_ms");
}

// status: partial (s command), closed (a new run started), reset (r command),
// restart (sender sequence went backwards, i.e. it rebooted).
inline void printSum(uint16_t session, const LinkRun &r, const char *status) {
  if (!r.active) { Serial.println("No run data yet."); return; }
  const SeqTracker &q = r.seq;
  Serial.printf("SUM,%lu,%04X,%c,", (unsigned long)millis(), session, r.node);
  if (r.srcSession >= 0) Serial.printf("%04lX", (unsigned long)r.srcSession);
  Serial.printf(",");
  if (r.run >= 0) Serial.printf("%ld", r.run);
  Serial.printf(",%s,%lu,%s,%lu,%lu,%lu,%lu,%lu", status, (unsigned long)q.expected(),
                q.totalKnown() ? "declared" : "seq_span", (unsigned long)q.unique,
                (unsigned long)q.lost(), (unsigned long)q.duplicates,
                (unsigned long)q.outOfOrder, (unsigned long)q.outOfRange);
  printOptional(q.hasPdr(), "%.2f", q.pdrPercent());
  const bool any = r.rssi.n > 0;
  printOptional(any, "%.0f", r.rssi.min);
  printOptional(any, "%.2f", r.rssi.mean);
  printOptional(any, "%.0f", r.rssi.max);
  printOptional(r.rssi.hasStdev(), "%.2f", r.rssi.stdev());
  printOptional(any, "%.2f", r.snr.min);
  printOptional(any, "%.2f", r.snr.mean);
  printOptional(any, "%.2f", r.snr.max);
  printOptional(r.snr.hasStdev(), "%.2f", r.snr.stdev());
  printOptional(any, "%.0f", r.ferr.mean);
  Serial.printf(",%lu,%lu\n", (unsigned long)r.firstMs, (unsigned long)r.lastMs);
}

inline double timeOnAirCalcMs(size_t payloadBytes) {
  return loraTimeOnAirMs(LORA_SPREADING_FACTOR, LORA_BANDWIDTH_HZ, LORA_CODING_RATE_DENOM,
                         LORA_PREAMBLE_SYMBOLS, payloadBytes, LORA_CRC_ENABLED);
}
} // namespace agos
