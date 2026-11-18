#include <Arduino.h>
#include "hardware.h"

#ifndef AGOS_MODE
#define AGOS_MODE 0
#endif

#if AGOS_MODE == 4
#include "agos_node.h"
#endif

#if AGOS_MODE == 1 || AGOS_MODE == 2
#include <SPI.h>
#include <LoRa.h>
#include "link_stats.h"
#include "test_log.h"
bool radioReady = false;
uint32_t sequence = 0;
uint16_t session = 0;
#if AGOS_MODE == 1
constexpr char NODE_ID = 'B';
agos::Console console("mM");
agos::LinkRun run;  // current burst run being received
#else
constexpr char NODE_ID = 'A';
agos::Console console("mMnNiI");
// Burst state: N packets at a fixed interval, numbered 1..N within a run.
uint32_t burstCount = 100;
uint32_t burstIntervalMs = 1000;
bool burstActive = false;
uint32_t burstRun = 0, burstSent = 0, burstFailed = 0, burstNextAt = 0;
#endif
#elif AGOS_MODE == 3
HardwareSerial gpsSerial(2);
uint32_t gpsBytes = 0;
#endif

uint32_t lastStatus = 0;

#if AGOS_MODE == 1 || AGOS_MODE == 2
void printHeaders()
{
  agos::printCommonHeaders();
#if AGOS_MODE == 1
  agos::printHeader("RX", "ms,session,src_node,src_session,run,seq,of,bytes,"
                          "rssi_dbm,snr_db,ferr_hz,result,payload");
  agos::printSumHeader();
#else
  agos::printHeader("TX", "ms,session,node,run,seq,of,bytes,tx_ms,toa_calc_ms,result");
  agos::printHeader("RUN", "ms,session,node,run,event,sent,tx_failed,of,interval_ms");
#endif
}

void printConfig()
{
  printHeaders();
  agos::printCfg(session, AGOS_MODE, AGOS_MODE == 1 ? "receiver" : "sender", NODE_ID, radioReady);
}

void printHelp()
{
#if AGOS_MODE == 1
  Serial.println("Receiver: s=summary of current run | r=reset counters | f=noise floor (sender idle)");
  Serial.println("          c=print config | m <text>+Enter=log a test note | h=help");
#else
  Serial.println("Sender: t=send ONE packet | b=start burst | x=stop burst | f=noise floor");
  Serial.printf("        n <count>+Enter=burst size (now %lu, max %lu) | i <ms>+Enter=interval (now %lu)\n",
                (unsigned long)burstCount, (unsigned long)agos::MAX_TRACKED_SEQ,
                (unsigned long)burstIntervalMs);
  Serial.println("        c=print config | m <text>+Enter=log a test note | h=help");
#endif
}
#endif

#if AGOS_MODE == 1
void handleReceived(int packetSize)
{
  char payload[256];
  size_t count = 0;
  while (LoRa.available())
  {
    const int value = LoRa.read();
    if (count < sizeof(payload) - 1)
      payload[count++] = static_cast<char>(value);
  }
  payload[count] = '\0';
  const int rssi = LoRa.packetRssi();
  const float snr = LoRa.packetSnr();
  const long ferr = LoRa.packetFrequencyError();
  const uint32_t now = millis();

  // Original human-readable line, unchanged.
  Serial.printf("RX bytes=%d payload=", packetSize);
  Serial.write(reinterpret_cast<const uint8_t *>(payload), count);
  Serial.printf(" | RSSI=%d dBm SNR=%.1f dB\n", rssi, snr);

  agos::BurstPacket burst;
  char legacyNode;
  unsigned long legacySeq;
  const char *result = "unknown";
  char fields[96] = ",,,,";  // empty src_node,src_session,run,seq,of
  if (strlen(payload) == count && agos::parseBurstPacket(payload, burst))
  {
    if (burst.of == 0 || burst.of > agos::MAX_TRACKED_SEQ)
      result = "bad_total";
    else
    {
      if (!run.sameRun(burst.node, burst.session, burst.run))
      {
        if (run.active)
          agos::printSum(session, run, "closed");
        run.start(burst.node, burst.session, burst.run, 1, burst.of, now);
      }
      result = agos::seqResultName(run.record(burst.seq, rssi, snr, ferr, now));
    }
    snprintf(fields, sizeof(fields), "%c,%04X,%lu,%lu,%lu", burst.node, burst.session,
             burst.run, burst.seq, burst.of);
  }
  else if (strlen(payload) == count && agos::parseLegacyTestPacket(payload, legacyNode, legacySeq))
  {
    // Single `t` packets carry no run size, so they are logged but not scored.
    result = "single";
    snprintf(fields, sizeof(fields), "%c,,,%lu,", legacyNode, legacySeq);
  }
  Serial.printf("RX,%lu,%04X,%s,%d,%d,%.2f,%ld,%s,", (unsigned long)now, session, fields,
                packetSize, rssi, snr, ferr, result);
  agos::printQuoted(payload, count);
  Serial.println();
}
#endif

#if AGOS_MODE == 2
// Sends one packet with a blocking endPacket(). tx_ms is the measured time
// from endPacket() start to return: airtime plus SPI/driver overhead.
bool transmit(const char *packet, size_t length, float &txMs)
{
  if (!LoRa.beginPacket())
    return false;
  LoRa.write(reinterpret_cast<const uint8_t *>(packet), length);
  const uint32_t t0 = micros();
  const bool ok = LoRa.endPacket();
  txMs = (micros() - t0) / 1000.0f;
  return ok;
}

void logTx(unsigned long runNumber, unsigned long seq, unsigned long of, size_t length,
           bool ok, float txMs)
{
  Serial.printf("TX,%lu,%04X,%c,", (unsigned long)millis(), session, NODE_ID);
  if (runNumber)
    Serial.printf("%lu,%lu,%lu", runNumber, seq, of);
  else
    Serial.printf(",%lu,", seq);
  Serial.printf(",%u,", (unsigned)length);
  if (ok)
    Serial.printf("%.1f", txMs);
  Serial.printf(",%.1f,%s\n", agos::timeOnAirCalcMs(length), ok ? "ok" : "fail");
}

void logRun(const char *event)
{
  Serial.printf("RUN,%lu,%04X,%c,%lu,%s,%lu,%lu,%lu,%lu\n", (unsigned long)millis(), session,
                NODE_ID, (unsigned long)burstRun, event, (unsigned long)burstSent,
                (unsigned long)burstFailed, (unsigned long)burstCount,
                (unsigned long)burstIntervalMs);
}

void sendSingle()
{
  // Original manual packet and messages, unchanged.
  const String packet = "AGOS,TEST,node=A,seq=" + String(sequence + 1);
  if (!LoRa.beginPacket())
  {
    Serial.println("Radio busy; try again.");
    return;
  }
  ++sequence;
  LoRa.print(packet);
  const uint32_t t0 = micros();
  const bool ok = LoRa.endPacket();
  const float txMs = (micros() - t0) / 1000.0f;
  Serial.println(ok ? "TX complete: " + packet : String("TX failed"));
  logTx(0, sequence, 0, packet.length(), ok, txMs);
}

void sendBurstPacket()
{
  char packet[64];
  const int length = agos::formatBurstPacket(packet, sizeof(packet), NODE_ID, session,
                                             burstRun, burstSent + 1, burstCount);
  float txMs = 0;
  const bool ok = length > 0 && (size_t)length < sizeof(packet) &&
                  transmit(packet, length, txMs);
  ++burstSent;  // the sequence number is used even if this attempt failed
  if (!ok)
    ++burstFailed;
  logTx(burstRun, burstSent, burstCount, length, ok, txMs);
}

void handleLine(const char *line, const char *argument)
{
  const char command = static_cast<char>(tolower(line[0]));
  if (command == 'm')
  {
    agos::printNote(session, NODE_ID, argument);
    return;
  }
  if (burstActive)
  {
    Serial.println("Burst running; settings not changed. Type x to stop it first.");
    return;
  }
  char *end = nullptr;
  const unsigned long value = strtoul(argument, &end, 10);
  if (end == argument || *end != '\0')
  {
    Serial.println("Expected a whole number, e.g. n 100 or i 1000.");
    return;
  }
  if (command == 'n')
  {
    if (value < 1 || value > agos::MAX_TRACKED_SEQ)
      Serial.printf("Burst size must be 1..%lu.\n", (unsigned long)agos::MAX_TRACKED_SEQ);
    else
      burstCount = value;
  }
  else
  {
    // A shorter interval than the TX time would make the schedule slip.
    if (value < 200 || value > 60000)
      Serial.println("Interval must be 200..60000 ms.");
    else
      burstIntervalMs = value;
  }
  Serial.printf("Burst: %lu packets every %lu ms.\n", (unsigned long)burstCount,
                (unsigned long)burstIntervalMs);
}
#endif

#if AGOS_MODE == 1 || AGOS_MODE == 2
void handleKey(char key)
{
  switch (tolower(key))
  {
  case 'c':
    printConfig();
    break;
  case 'h':
    printHelp();
    break;
  case 'f':
#if AGOS_MODE == 2
    if (burstActive)
    {
      Serial.println("Noise sample skipped: burst running.");
      break;
    }
#endif
    agos::sampleNoise(session, NODE_ID);
#if AGOS_MODE == 2
    LoRa.idle();
#endif
    break;
#if AGOS_MODE == 1
  case 's':
    agos::printSum(session, run, "partial");
    break;
  case 'r':
    if (run.active)
      agos::printSum(session, run, "reset");
    run = agos::LinkRun();
    Serial.println("--- counters reset ---");
    break;
#else
  case 't':
    if (burstActive)
      Serial.println("Burst running; type x to stop it before sending single packets.");
    else
      sendSingle();
    break;
  case 'b':
    if (burstActive)
    {
      Serial.println("Burst already running.");
      break;
    }
    ++burstRun;
    burstSent = burstFailed = 0;
    burstActive = true;
    burstNextAt = millis();
    logRun("start");
    break;
  case 'x':
    if (burstActive)
    {
      burstActive = false;
      logRun("stopped");
    }
    break;
#endif
  default:
    break;
  }
}

void serviceConsole()
{
  for (int i = 0; i < 32 && Serial.available(); ++i)
  {
    char key = 0;
    const int event = console.feed(Serial.read(), key);
    if (event == 1)
      handleKey(key);
#if AGOS_MODE == 1
    else if (event == 2)
      agos::printNote(session, NODE_ID, console.argument());
#else
    else if (event == 2)
      handleLine(console.line, console.argument());
#endif
  }
}
#endif

void setup()
{
#if AGOS_MODE == 4
  setupNode();
#else
  Serial.begin(115200);
  delay(1000);
  Serial.printf("\nAGOS hardware bring-up | mode=%d | chip=%s | flash=%u bytes\n",
                AGOS_MODE, ESP.getChipModel(), ESP.getFlashChipSize());

#if AGOS_MODE == 1 || AGOS_MODE == 2
  session = agos::newSessionId();
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  radioReady = LoRa.begin(LORA_FREQUENCY_HZ);
  if (!radioReady)
  {
    Serial.println("FAIL: Ra-02 not detected. Power off and check 3V3/GND/SPI/CS/reset.");
    printConfig();
    return;
  }
  agos::applyRadioConfig(); // Bench settings from hardware.h; not a regulatory approval.
  Serial.println("PASS: Ra-02 initialized.");
  agos::printRadioSummary();
  printConfig();
#if AGOS_MODE == 1
  Serial.println("Node B: receiver. Waiting for AGOS test packets...");
#else
  Serial.println("Node A: sender. Attach the correct antenna and verify test conditions.");
  Serial.println("Type t to send ONE test packet. No automatic transmissions.");
  Serial.println("Type b to send a numbered burst (it stops by itself after the set count).");
#endif
  printHelp();
#elif AGOS_MODE == 3
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("GPS UART check: 9600 baud, GPS TX -> GPIO16. Raw data follows.");
  Serial.println("NMEA reception and a valid position fix are separate checks.");
#else
  Serial.println("USB check: heartbeat every 2 seconds. No peripherals required.");
#endif
#endif
}

void loop()
{
#if AGOS_MODE == 4
  loopNode();
#elif AGOS_MODE == 1
  if (!radioReady)
  {
    delay(100);
    return;
  }
  serviceConsole();
  const int packetSize = LoRa.parsePacket();
  if (packetSize)
    handleReceived(packetSize);
#elif AGOS_MODE == 2
  if (!radioReady)
  {
    delay(100);
    return;
  }
  serviceConsole();
  if (burstActive && (int32_t)(millis() - burstNextAt) >= 0)
  {
    // Fixed schedule (not "interval after the previous TX"), so spacing does
    // not drift with TX time.
    burstNextAt += burstIntervalMs;
    sendBurstPacket();
    if (burstSent >= burstCount)
    {
      burstActive = false;
      logRun("done");
    }
  }
#elif AGOS_MODE == 3
  // Bound each pass so a continuous stream cannot starve diagnostics.
  for (int i = 0; i < 256 && gpsSerial.available(); ++i)
  {
    Serial.write(gpsSerial.read());
    ++gpsBytes;
  }
  if (millis() - lastStatus >= 5000)
  {
    lastStatus = millis();
    Serial.printf("\n[GPS received %lu bytes total; raw data is not a validated fix]\n",
                  static_cast<unsigned long>(gpsBytes));
  }
#else
  if (millis() - lastStatus >= 2000)
  {
    lastStatus = millis();
    Serial.printf("AGOS alive | uptime=%lu s | free_heap=%u\n",
                  static_cast<unsigned long>(millis() / 1000), ESP.getFreeHeap());
  }
#endif
  delay(1);
}
