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
bool radioReady = false;
uint32_t sequence = 0;
#elif AGOS_MODE == 3
HardwareSerial gpsSerial(2);
uint32_t gpsBytes = 0;
#endif

uint32_t lastStatus = 0;

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
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  radioReady = LoRa.begin(LORA_FREQUENCY_HZ);
  if (!radioReady)
  {
    Serial.println("FAIL: Ra-02 not detected. Power off and check 3V3/GND/SPI/CS/reset.");
    return;
  }
  LoRa.setSignalBandwidth(125000);
  LoRa.setSpreadingFactor(7);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();
  LoRa.setTxPower(2); // Bench test starting value; not a regulatory approval.
  Serial.println("PASS: Ra-02 initialized at 433 MHz; BW125/SF7/CR4:5/CRC.");
#if AGOS_MODE == 1
  Serial.println("Node B: receiver. Waiting for AGOS test packets...");
#else
  Serial.println("Node A: sender. Attach the correct antenna and verify test conditions.");
  Serial.println("Type t to send ONE test packet. No automatic transmissions.");
#endif
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
  const int packetSize = LoRa.parsePacket();
  if (packetSize)
  {
    Serial.printf("RX bytes=%d payload=", packetSize);
    while (LoRa.available())
      Serial.write(LoRa.read());
    Serial.printf(" | RSSI=%d dBm SNR=%.1f dB\n", LoRa.packetRssi(), LoRa.packetSnr());
  }
#elif AGOS_MODE == 2
  if (!radioReady)
  {
    delay(100);
    return;
  }
  while (Serial.available())
  {
    const char command = Serial.read();
    if (command != 't' && command != 'T')
      continue;
    if (!LoRa.beginPacket())
    {
      Serial.println("Radio busy; try again.");
      continue;
    }
    const String packet = "AGOS,TEST,node=A,seq=" + String(++sequence);
    LoRa.print(packet);
    if (LoRa.endPacket())
      Serial.println("TX complete: " + packet);
    else
      Serial.println("TX failed");
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
