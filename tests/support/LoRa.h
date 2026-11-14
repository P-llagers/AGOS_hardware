#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>

struct FakeLoRa {
  bool initialized = true, listening = false, busy = false;
  void (*done)() = nullptr;
  std::string incoming, outgoing;
  void setPins(int, int, int) {}
  bool begin(long) { return initialized; }
  void setSignalBandwidth(long) {}
  void setSpreadingFactor(int) {}
  void setCodingRate4(int) {}
  void setSyncWord(int) {}
  void enableCrc() {}
  void setTxPower(int) {}
  void onTxDone(void (*callback)()) { done = callback; }
  void receive() { listening = true; busy = false; }
  void idle() { listening = false; busy = false; }
  bool beginPacket() { listening = false; outgoing.clear(); return !busy; }
  size_t write(const uint8_t *data, size_t length) {
    outgoing.assign(reinterpret_cast<const char *>(data), length); return length;
  }
  bool endPacket(bool async) { busy = async; return true; }
  void completeTx() { busy = false; if (done) done(); }
  int parsePacket() { return static_cast<int>(incoming.size()); }
  int available() { return static_cast<int>(incoming.size()); }
  int read() { unsigned char c = incoming.front(); incoming.erase(0, 1); return c; }
  int packetRssi() { return -55; }
  float packetSnr() { return 9.5f; }
};
extern FakeLoRa LoRa;
