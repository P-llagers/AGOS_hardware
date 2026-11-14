#pragma once

#include <stdint.h>
#include <stdio.h>
#include <math.h>

namespace agos {
constexpr uint32_t MAX_FIX_AGE_MS = 5000;

enum class FixState { NoFix, Stale, Valid };

struct Position {
  bool locationValid = false;
  uint32_t locationAge = UINT32_MAX;
  bool statusKnown = false;
  bool statusActive = false;
  uint32_t statusAge = UINT32_MAX;
  double latitude = 0;
  double longitude = 0;
  bool satellitesValid = false;
  uint32_t satellites = 0;
};

inline FixState fixState(const Position &p) {
  // An explicit invalid RMC status takes precedence over an old cached location.
  if (!p.locationValid || !p.statusKnown || !p.statusActive)
    return FixState::NoFix;
  if (p.locationAge > MAX_FIX_AGE_MS || p.statusAge > MAX_FIX_AGE_MS)
    return FixState::Stale;
  if (!isfinite(p.latitude) || !isfinite(p.longitude) ||
      p.latitude < -90 || p.latitude > 90 ||
      p.longitude < -180 || p.longitude > 180)
    return FixState::NoFix;
  return FixState::Valid;
}

inline const char *fixName(FixState state) {
  switch (state) {
    case FixState::Valid: return "VALID";
    case FixState::Stale: return "STALE";
    default: return "NO_FIX";
  }
}

// A zero return means the output did not fit. Never transmit a truncated packet.
inline size_t formatPosition(char *out, size_t capacity, char node,
                             uint32_t sequence, const Position &p) {
  const FixState state = fixState(p);
  char satellites[16] = "NA";
  if (p.satellitesValid)
    snprintf(satellites, sizeof(satellites), "%lu", (unsigned long)p.satellites);
  int length;
  if (state == FixState::Valid) {
    length = snprintf(out, capacity,
        "AGOS,POS,v=1,node=%c,seq=%lu,fix=VALID,age_ms=%lu,lat=%.6f,lon=%.6f,sat=%s",
        node, (unsigned long)sequence, (unsigned long)p.locationAge,
        p.latitude, p.longitude, satellites);
  } else {
    length = snprintf(out, capacity,
        "AGOS,POS,v=1,node=%c,seq=%lu,fix=%s,sat=%s",
        node, (unsigned long)sequence, fixName(state), satellites);
  }
  return length > 0 && (size_t)length < capacity && length <= 255 ? (size_t)length : 0;
}
} // namespace agos
