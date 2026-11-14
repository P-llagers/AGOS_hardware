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

  radioReady = false;
  Serial.input = "t\n"; loopNode();
  assert(!transmitting);
  feed(nmea("GPRMC,123522,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W"));
  assert(agos::fixState(position()) == agos::FixState::Valid);
  puts("PASS: NMEA replay, fix loss, async TX, RX return, timeout and radio failure");
}
