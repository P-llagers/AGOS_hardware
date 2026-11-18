// Includes the production node loop; only serial/SPI/radio hardware is mocked.
#include "../src/agos_node.cpp"
#include <assert.h>

uint32_t testMillis = 0;
HardwareSerial Serial;
FakeSPI SPI;
FakeLoRa LoRa;

static std::string nmea(const std::string &body) {
  uint8_t checksum = 0;
  for (char c : body) checksum ^= static_cast<uint8_t>(c);
  char suffix[8];
  snprintf(suffix, sizeof(suffix), "*%02X\r\n", checksum);
  return "$" + body + suffix;
}

static void feed(const std::string &sentence) {
  gpsSerial.input += sentence;
  loopNode();
}

int main() {
  setupNode();
  assert(radioReady && LoRa.listening && !LoRa.done);
  Serial.input = "t\n";
  loopNode();
  assert(transmitting && LoRa.done);
  assert(LoRa.outgoing.find("fix=NO_FIX") != std::string::npos);

  // GPS is still parsed while an asynchronous radio transmission is active.
  feed(nmea("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W"));
  assert(transmitting);
  assert(agos::fixState(position()) == agos::FixState::Valid);
  assert(fabs(position().latitude - 48.1173) < 0.000001);

  LoRa.completeTx();
  loopNode();
  assert(!transmitting && LoRa.listening && !LoRa.done);
  LoRa.incoming = "AGOS,POS,v=1,node=B,seq=1,fix=NO_FIX,sat=NA";
  loopNode();
  assert(received == 1 && Serial.output.find("node=B") != std::string::npos);

  Serial.input = "t\n";
  loopNode();
  assert(LoRa.outgoing.find("lat=48.117300") != std::string::npos);
  LoRa.completeTx(); loopNode();
  testMillis += 5001;
  assert(agos::fixState(position()) == agos::FixState::Stale);

  // GN talker is accepted, while an invalid checksum cannot overwrite a fix.
  feed(nmea("GNRMC,123520,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W"));
  assert(agos::fixState(position()) == agos::FixState::Valid);
  feed("$GNRMC,123521,V,,,,,,,230394,,,N*00\r\n");
  assert(agos::fixState(position()) == agos::FixState::Valid);
  feed(nmea("GNRMC,123521,V,,,,,,,230394,,,N"));
  assert(gps.location.isValid()); // Library retains the old location.
  assert(agos::fixState(position()) == agos::FixState::NoFix);
  Serial.input = "t\n"; loopNode();
  assert(LoRa.outgoing.find("lat=") == std::string::npos);

  // Missing DIO0/TX completion must time out and restore polled reception.
  testMillis += TX_TIMEOUT_MS + 1;
  loopNode();
  assert(!transmitting && LoRa.listening && !LoRa.done);
  assert(Serial.output.find("TX timeout") != std::string::npos);
  LoRa.incoming = "reply after timeout";
  loopNode();
  assert(received == 2);

  // Machine-readable rows: configuration, TX outcome, first fix and RX rows.
  assert(Serial.output.find("\nCFG,") != std::string::npos);
  assert(Serial.output.find(",node,A,1,433000000,125000,7,4/5,8,0x12,1,2,") != std::string::npos);
  assert(Serial.output.find("\nTX,") != std::string::npos);
  assert(Serial.output.find(",timeout\n") != std::string::npos);
  assert(Serial.output.find("\nFIX,") != std::string::npos);
  assert(Serial.output.find("\nTTFF,") != std::string::npos);
  assert(Serial.output.find(",unknown,NO_FIX,,,,,,\"reply after timeout\"") != std::string::npos);

  // Per-peer accounting: seq 2 missing, then a duplicate of seq 3.
  LoRa.incoming = "AGOS,POS,v=1,node=B,seq=3,fix=NO_FIX,sat=NA"; loopNode();
  LoRa.incoming = "AGOS,POS,v=1,node=B,seq=3,fix=NO_FIX,sat=NA"; loopNode();
  assert(Serial.output.find(",B,3,NO_FIX,43,-55,9.50,-1200,dup,") != std::string::npos);
  Serial.input = "s"; loopNode();
  assert(Serial.output.find(",B,,,partial,3,seq_span,2,1,1,0,0,66.67,") != std::string::npos);

  // With both fixes VALID the row carries a calculated distance (about 742.5 m).
  feed(nmea("GPRMC,123521,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W"));
  LoRa.incoming = "AGOS,POS,v=1,node=B,seq=4,fix=VALID,age_ms=10,lat=48.117300,lon=11.526667,sat=7";
  loopNode();
  const size_t rx = Serial.output.find(",B,4,VALID,");
  assert(rx != std::string::npos);
  assert(Serial.output.find(",48.1173000,11.5166667,48.117300,11.526667,742.", rx) != std::string::npos);

  // GPS logging rows only when switched on; coordinates only with a VALID fix.
  // (TinyGPSPlus reads the two-digit year 94 of this sample sentence as 2094.)
  assert(Serial.output.find("\nGPS,") == std::string::npos);
  Serial.input = "l"; loopNode();
  testMillis += GPS_LOG_INTERVAL_MS; loopNode();
  assert(Serial.output.find(",2094-03-23,12:35:21.00,VALID,48.1173000,11.5166667,") != std::string::npos);

  // Operator notes are logged as quoted text, never as a measurement field.
  Serial.input = "m dist=100m, \"LOS\"\r\n"; loopNode();
  assert(Serial.output.find(",A,\"dist=100m, \"\"LOS\"\"\"\n") != std::string::npos);
  Serial.input = "f"; loopNode();
  assert(Serial.output.find(",A,50,-120,-120.0,-120\n") != std::string::npos);

  radioReady = false;
  Serial.input = "t\n"; loopNode();
  assert(!transmitting);
  feed(nmea("GPRMC,123522,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W"));
  assert(agos::fixState(position()) == agos::FixState::Valid);
  puts("PASS: NMEA replay, fix loss, async TX, RX return, timeout and radio failure");
}
