#pragma once
// Link-test bookkeeping shared by the firmware and the host tests.
// No Arduino dependencies: everything here can be checked with a desktop compiler.

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace agos {

// Running min/mean/max/sample standard deviation (Welford's method).
struct RunningStats {
  uint32_t n = 0;
  double mean = 0, m2 = 0, min = 0, max = 0;
  void add(double x) {
    ++n;
    if (n == 1) { min = max = x; }
    else { if (x < min) min = x; if (x > max) max = x; }
    const double d = x - mean;
    mean += d / n;
    m2 += d * (x - mean);
  }
  bool hasStdev() const { return n > 1; }
  double stdev() const { return n > 1 ? sqrt(m2 / (n - 1)) : 0; }
};

// Sequence numbers are tracked with a bitmap so duplicates are never counted
// as extra deliveries. Burst runs are limited to this many packets.
constexpr uint32_t MAX_TRACKED_SEQ = 1000;

enum class SeqResult { New, Duplicate, OutOfOrder, OutOfRange };

inline const char *seqResultName(SeqResult r) {
  switch (r) {
    case SeqResult::New: return "new";
    case SeqResult::Duplicate: return "dup";
    case SeqResult::OutOfOrder: return "ooo";
    default: return "range";
  }
}

struct SeqTracker {
  uint32_t base = 0;   // lowest sequence number that belongs to this run
  uint32_t total = 0;  // packets the sender says it will send; 0 = unknown
  uint32_t unique = 0, duplicates = 0, outOfOrder = 0, outOfRange = 0;
  bool any = false;
  uint32_t highest = 0;
  uint8_t seen[(MAX_TRACKED_SEQ + 7) / 8] = {};

  void reset(uint32_t firstSeq, uint32_t declaredTotal) {
    base = firstSeq; total = declaredTotal;
    unique = duplicates = outOfOrder = outOfRange = 0;
    any = false; highest = 0;
    memset(seen, 0, sizeof(seen));
  }

  SeqResult add(uint32_t seq) {
    if (seq < base || seq - base >= MAX_TRACKED_SEQ ||
        (total && seq - base >= total)) {
      ++outOfRange;
      return SeqResult::OutOfRange;
    }
    const uint32_t i = seq - base;
    const uint8_t bit = static_cast<uint8_t>(1u << (i % 8));
    if (seen[i / 8] & bit) { ++duplicates; return SeqResult::Duplicate; }
    seen[i / 8] |= bit;
    ++unique;
    SeqResult result = SeqResult::New;
    if (any && seq < highest) { ++outOfOrder; result = SeqResult::OutOfOrder; }
    if (!any || seq > highest) highest = seq;
    any = true;
    return result;
  }

  // With a declared total the denominator is exact. Without one it is the
  // span of sequence numbers heard, which cannot see losses before the first
  // or after the last received packet (reported as basis "seq_span").
  bool totalKnown() const { return total != 0; }
  uint32_t expected() const { return total ? total : (any ? highest - base + 1 : 0); }
  uint32_t lost() const { const uint32_t e = expected(); return e > unique ? e - unique : 0; }
  bool hasPdr() const { return expected() > 0; }
  double pdrPercent() const { return hasPdr() ? 100.0 * unique / expected() : 0; }
};

// One measured link run: sequence accounting plus signal statistics of the
// unique packets (duplicates are excluded so each packet is counted once).
struct LinkRun {
  bool active = false;
  char node = '?';
  long srcSession = -1;  // -1 = not carried by this packet type
  long run = -1;         // -1 = not carried by this packet type
  SeqTracker seq;
  RunningStats rssi, snr, ferr;
  uint32_t firstMs = 0, lastMs = 0;

  void start(char srcNode, long session, long runNumber, uint32_t firstSeq,
             uint32_t declaredTotal, uint32_t nowMs) {
    active = true; node = srcNode; srcSession = session; run = runNumber;
    seq.reset(firstSeq, declaredTotal);
    rssi = RunningStats(); snr = RunningStats(); ferr = RunningStats();
    firstMs = lastMs = nowMs;
  }
  bool sameRun(char srcNode, long session, long runNumber) const {
    return active && node == srcNode && srcSession == session && run == runNumber;
  }
  SeqResult record(uint32_t sequence, double rssiDbm, double snrDb, double ferrHz,
                   uint32_t nowMs) {
    const SeqResult r = seq.add(sequence);
    if (r == SeqResult::New || r == SeqResult::OutOfOrder) {
      rssi.add(rssiDbm); snr.add(snrDb); ferr.add(ferrHz);
      lastMs = nowMs;
    }
    return r;
  }
};

// Semtech SX127x time-on-air formula (AN1200.13), explicit header, LDRO on
// when the symbol time exceeds 16 ms (as the LoRa library sets it).
// This is a calculated value, not a measurement.
inline double loraTimeOnAirMs(int sf, long bwHz, int crDenom, long preamble,
                              size_t payloadBytes, bool crc) {
  const double symbolMs = static_cast<double>(1UL << sf) / bwHz * 1000.0;
  const int lowDataRate = symbolMs > 16.0 ? 1 : 0;
  const double numerator = 8.0 * payloadBytes - 4.0 * sf + 28 + 16 * (crc ? 1 : 0);
  const double denominator = 4.0 * (sf - 2 * lowDataRate);
  const double payloadSymbols = 8 + fmax(ceil(numerator / denominator) * crDenom, 0.0);
  return (preamble + 4.25) * symbolMs + payloadSymbols * symbolMs;
}

// Great-circle distance on a sphere of mean Earth radius. A calculated value;
// its accuracy is limited by both GPS fixes, not by this formula.
inline double gpsDistanceM(double lat1, double lon1, double lat2, double lon2) {
  const double radiusM = 6371008.8;
  const double toRad = 3.14159265358979323846 / 180.0;
  const double dLat = (lat2 - lat1) * toRad, dLon = (lon2 - lon1) * toRad;
  const double a = sin(dLat / 2) * sin(dLat / 2) +
                   cos(lat1 * toRad) * cos(lat2 * toRad) * sin(dLon / 2) * sin(dLon / 2);
  return 2 * radiusM * asin(fmin(1.0, sqrt(a)));
}

// Burst test packet: AGOS,TEST,node=A,ses=1A2B,run=001,seq=0001,of=0100
// Zero-padded fields keep every packet in a run the same length (same airtime).
struct BurstPacket {
  char node = '?';
  unsigned session = 0;
  unsigned long run = 0, seq = 0, of = 0;
};

inline int formatBurstPacket(char *out, size_t capacity, char node, unsigned session,
                             unsigned long run, unsigned long seq, unsigned long of) {
  return snprintf(out, capacity, "AGOS,TEST,node=%c,ses=%04X,run=%03lu,seq=%04lu,of=%04lu",
                  node, session & 0xFFFFu, run, seq, of);
}

inline bool parseBurstPacket(const char *s, BurstPacket &p) {
  char node; unsigned session; unsigned long run, seq, of; int end = -1;
  if (sscanf(s, "AGOS,TEST,node=%c,ses=%4x,run=%lu,seq=%lu,of=%lu%n",
             &node, &session, &run, &seq, &of, &end) != 5 || end < 0 || s[end] != '\0')
    return false;
  p.node = node; p.session = session; p.run = run; p.seq = seq; p.of = of;
  return true;
}

// Single manual packet from the lora_sender `t` command: AGOS,TEST,node=A,seq=1
inline bool parseLegacyTestPacket(const char *s, char &node, unsigned long &seq) {
  int end = -1;
  return sscanf(s, "AGOS,TEST,node=%c,seq=%lu%n", &node, &seq, &end) == 2 &&
         end >= 0 && s[end] == '\0';
}

// Fields of a mode 4 AGOS,POS,v=1 packet. Values are as claimed by the sender.
struct PosPacketInfo {
  char node = '?';
  unsigned long seq = 0;
  char fix[8] = "";
  bool hasCoords = false;
  double lat = 0, lon = 0;
};

inline bool parsePosPacket(const char *s, PosPacketInfo &p) {
  char node; unsigned long seq; int end = -1;
  if (sscanf(s, "AGOS,POS,v=1,node=%c,seq=%lu,%n", &node, &seq, &end) != 2 || end < 0)
    return false;
  p = PosPacketInfo();
  p.node = node; p.seq = seq;
  const char *rest = s + end;
  if (strncmp(rest, "fix=", 4) != 0) return false;
  size_t n = 0;
  for (const char *c = rest + 4; *c && *c != ',' && n < sizeof(p.fix) - 1; ++c) p.fix[n++] = *c;
  p.fix[n] = '\0';
  if (strcmp(p.fix, "VALID") == 0) {
    const char *la = strstr(rest, ",lat="), *lo = strstr(rest, ",lon=");
    if (la && lo) {
      char *endLat = nullptr, *endLon = nullptr;
      const double lat = strtod(la + 5, &endLat), lon = strtod(lo + 5, &endLon);
      if (endLat != la + 5 && endLon != lo + 5 && isfinite(lat) && isfinite(lon) &&
          lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180) {
        p.hasCoords = true; p.lat = lat; p.lon = lon;
      }
    }
  }
  return true;
}

// Writes `in` as one CSV field in double quotes. Quotes are doubled and
// non-printable bytes are written as \xHH, so the original bytes remain
// recoverable and a payload can never split or break a CSV line.
inline size_t csvQuote(const char *in, size_t length, char *out, size_t capacity) {
  if (capacity < 3) { if (capacity) out[0] = '\0'; return 0; }
  size_t o = 0;
  out[o++] = '"';
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    char piece[5];
    size_t pieceLength;
    if (c == '"') { piece[0] = piece[1] = '"'; pieceLength = 2; }
    else if (c == '\\') { piece[0] = piece[1] = '\\'; pieceLength = 2; }
    else if (c >= 0x20 && c < 0x7F) { piece[0] = static_cast<char>(c); pieceLength = 1; }
    else { snprintf(piece, sizeof(piece), "\\x%02X", c); pieceLength = 4; }
    if (o + pieceLength + 2 > capacity) break;  // leave room for quote and NUL
    memcpy(out + o, piece, pieceLength);
    o += pieceLength;
  }
  out[o++] = '"';
  out[o] = '\0';
  return o;
}

// Serial console: single-key commands act immediately (as before); keys listed
// in lineKeys start a text line that is completed with Enter (e.g. "m note").
struct Console {
  const char *lineKeys;
  char line[121] = "";
  size_t length = 0;
  bool collecting = false;

  explicit Console(const char *keys) : lineKeys(keys) {}

  // Returns 1 with `key` set for an immediate command, 2 when `line` is ready.
  int feed(int value, char &key) {
    if (value < 0) return 0;
    const char c = static_cast<char>(value);
    if (collecting) {
      if (c == '\r' || c == '\n') { collecting = false; line[length] = '\0'; return 2; }
      if (c == '\b' || c == 0x7F) { if (length > 1) --length; return 0; }
      if (length < sizeof(line) - 1) line[length++] = c;
      return 0;
    }
    if (c == '\r' || c == '\n' || c == ' ') return 0;
    if (strchr(lineKeys, c)) { collecting = true; length = 0; line[length++] = c; return 0; }
    key = c;
    return 1;
  }

  // Text after the command letter, with leading spaces removed.
  const char *argument() const {
    const char *a = line + 1;
    while (*a == ' ') ++a;
    return a;
  }
};
} // namespace agos
