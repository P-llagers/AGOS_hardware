#include "position_packet.h"
#include <assert.h>
#include <string.h>
#include <limits>

static void expectNoCoordinates(const agos::Position &p, const char *state) {
  char packet[256];
  assert(agos::formatPosition(packet, sizeof(packet), 'B', 7, p) > 0);
  assert(strstr(packet, state));
  assert(!strstr(packet, "lat="));
  assert(!strstr(packet, "lon="));
}

int main() {
  agos::Position p;
  expectNoCoordinates(p, "fix=NO_FIX");

  p.locationValid = true;
  p.locationAge = 100;
  p.latitude = 14.5995;
  p.longitude = 120.9842;
  // Cached coordinates without confirmed RMC status must not leak into a packet.
  expectNoCoordinates(p, "fix=NO_FIX");
  p.statusKnown = true;
  p.statusActive = true;
  p.statusAge = 100;
  p.satellitesValid = true;
  p.satellites = 8;
  char packet[256];
  size_t length = agos::formatPosition(packet, sizeof(packet), 'A', UINT32_MAX, p);
  assert(length == strlen(packet) && length <= 255);
  assert(strstr(packet, "node=A,seq=4294967295,fix=VALID"));
  assert(strstr(packet, "lat=14.599500,lon=120.984200,sat=8"));

  p.statusActive = false;
  expectNoCoordinates(p, "fix=NO_FIX");
  p.statusActive = true;
  p.locationAge = 5001;
  expectNoCoordinates(p, "fix=STALE");
  p.locationAge = 100;
  p.statusAge = 5001;
  expectNoCoordinates(p, "fix=STALE");
  p.statusAge = p.locationAge = 5000;
  assert(agos::fixState(p) == agos::FixState::Valid);

  p.latitude = 91;
  expectNoCoordinates(p, "fix=NO_FIX");
  p.latitude = 0;
  p.longitude = -181;
  expectNoCoordinates(p, "fix=NO_FIX");
  p.longitude = std::numeric_limits<double>::quiet_NaN();
  expectNoCoordinates(p, "fix=NO_FIX");
  // 0,0 is a legitimate location when the receiver reports a current fix.
  p.longitude = 0;
  assert(agos::fixState(p) == agos::FixState::Valid);
  p.satellitesValid = false;
  assert(agos::formatPosition(packet, sizeof(packet), 'B', 1, p));
  assert(strstr(packet, "sat=NA"));
  char small[12];
  assert(agos::formatPosition(small, sizeof(small), 'B', 1, p) == 0);
  assert(agos::formatPosition(nullptr, 0, 'B', 1, p) == 0);
  puts("PASS: GPS freshness, fix loss, coordinate bounds and packet formatting");
}
