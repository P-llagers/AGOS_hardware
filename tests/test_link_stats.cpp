// Host checks for link-test accounting, calculated values, parsers and CSV output.
#include "link_stats.h"
#include <assert.h>
#include <string.h>

static bool near(double a, double b, double tolerance) { return fabs(a - b) <= tolerance; }

int main() {
  using agos::SeqResult;

  // Declared total: losses at the start and the end of a run are counted.
  agos::SeqTracker q;
  q.reset(1, 100);
  for (uint32_t s = 11; s <= 60; ++s) assert(q.add(s) == SeqResult::New);
  assert(q.unique == 50 && q.expected() == 100 && q.lost() == 50);
  assert(near(q.pdrPercent(), 50.0, 1e-9));
  assert(q.add(20) == SeqResult::Duplicate && q.unique == 50 && q.duplicates == 1);
  assert(q.add(5) == SeqResult::OutOfOrder && q.unique == 51 && q.outOfOrder == 1);
  assert(q.add(0) == SeqResult::OutOfRange && q.add(101) == SeqResult::OutOfRange);
  assert(q.outOfRange == 2 && q.unique == 51 && q.lost() == 49);
  assert(q.add(100) == SeqResult::New && q.add(1) == SeqResult::OutOfOrder);
  for (uint32_t s = 1; s <= 100; ++s) q.add(s);
  assert(q.unique == 100 && q.lost() == 0 && near(q.pdrPercent(), 100.0, 1e-9));

  // Unknown total: the denominator is the span heard, starting at the first seq.
  q.reset(7, 0);
  assert(!q.hasPdr());
  q.add(7); q.add(9); q.add(10);
  assert(!q.totalKnown() && q.expected() == 4 && q.lost() == 1);
  assert(q.add(7 + agos::MAX_TRACKED_SEQ) == SeqResult::OutOfRange);

  // Duplicates do not enter the signal statistics.
  agos::LinkRun run;
  run.start('A', 0x1A2B, 3, 1, 10, 0);
  assert(run.sameRun('A', 0x1A2B, 3) && !run.sameRun('A', 0x1A2C, 3) && !run.sameRun('A', 0x1A2B, 4));
  run.record(1, -80, 5, 100, 10);
  run.record(2, -90, 3, 300, 20);
  run.record(2, -40, 9, 900, 30);
  assert(run.rssi.n == 2 && near(run.rssi.mean, -85, 1e-9) && run.rssi.min == -90);
  assert(near(run.rssi.stdev(), 7.0710678, 1e-6) && near(run.ferr.mean, 200, 1e-9));
  assert(run.lastMs == 20);

  // Time on air, compared with Semtech's calculator (CR4/5, preamble 8, CRC on).
  assert(near(agos::loraTimeOnAirMs(7, 125000, 5, 8, 10, true), 41.216, 0.001));
  assert(near(agos::loraTimeOnAirMs(12, 125000, 5, 8, 10, true), 991.232, 0.001));

  // Distance: 0.01 degree of longitude at 48.1173 N is about 742.5 m.
  assert(near(agos::gpsDistanceM(48.1173, 11.5166667, 48.1173, 11.5266667), 742.5, 1.0));
  assert(agos::gpsDistanceM(10, 120, 10, 120) == 0);

  // Burst packets have a fixed length and round-trip through the parser.
  char packet[64];
  int length = agos::formatBurstPacket(packet, sizeof(packet), 'A', 0xBEEF, 1, 1, 100);
  assert(length == 50 && strcmp(packet, "AGOS,TEST,node=A,ses=BEEF,run=001,seq=0001,of=0100") == 0);
  assert(agos::formatBurstPacket(packet, sizeof(packet), 'A', 0xBEEF, 999, 1000, 1000) == 50);
  agos::BurstPacket b;
  assert(agos::parseBurstPacket(packet, b) && b.node == 'A' && b.session == 0xBEEF);
  assert(b.run == 999 && b.seq == 1000 && b.of == 1000);
  assert(!agos::parseBurstPacket("AGOS,TEST,node=A,ses=BEEF,run=001,seq=0001,of=0100x", b));
  assert(!agos::parseBurstPacket("AGOS,TEST,node=A,seq=1", b));
  char node; unsigned long seq;
  assert(agos::parseLegacyTestPacket("AGOS,TEST,node=A,seq=12", node, seq) && node == 'A' && seq == 12);
  assert(!agos::parseLegacyTestPacket(packet, node, seq));

  // Position packets: coordinates only for VALID packets in range.
  agos::PosPacketInfo info;
  assert(agos::parsePosPacket("AGOS,POS,v=1,node=B,seq=4,fix=VALID,age_ms=200,lat=14.599500,lon=120.984200,sat=8", info));
  assert(info.node == 'B' && info.seq == 4 && strcmp(info.fix, "VALID") == 0);
  assert(info.hasCoords && near(info.lat, 14.5995, 1e-9) && near(info.lon, 120.9842, 1e-9));
  assert(agos::parsePosPacket("AGOS,POS,v=1,node=B,seq=5,fix=NO_FIX,sat=NA", info));
  assert(strcmp(info.fix, "NO_FIX") == 0 && !info.hasCoords);
  assert(agos::parsePosPacket("AGOS,POS,v=1,node=B,seq=6,fix=VALID,age_ms=1,lat=95.0,lon=1.0,sat=NA", info));
  assert(!info.hasCoords);
  assert(!agos::parsePosPacket("AGOS,POS,v=2,node=B,seq=6,fix=VALID", info));

  // CSV quoting keeps every byte recoverable on one line.
  char out[64];
  const char raw[] = "a,\"b\"\\\n\x01";
  agos::csvQuote(raw, sizeof(raw) - 1, out, sizeof(out));
  assert(strcmp(out, "\"a,\"\"b\"\"\\\\\\x0A\\x01\"") == 0);
  char tiny[6];
  agos::csvQuote("abcdef", 6, tiny, sizeof(tiny));
  assert(strcmp(tiny, "\"abc\"") == 0);

  // Console: immediate keys still act at once; m starts a line ended by Enter.
  agos::Console console("mM");
  char key = 0;
  assert(console.feed('t', key) == 1 && key == 't');
  assert(console.feed('\r', key) == 0 && console.feed('\n', key) == 0);
  const char typed[] = "m  dist=100m LOSx\b\r";
  int event = 0;
  for (const char *c = typed; *c; ++c) event = console.feed(*c, key);
  assert(event == 2 && strcmp(console.argument(), "dist=100m LOS") == 0);
  assert(console.feed('s', key) == 1 && key == 's');

  puts("PASS: sequence accounting, link statistics, time on air, distance, parsers, CSV, console");
}
