#pragma once
// Host-only Arduino surface for replaying NMEA through the real GPS parser.
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>

#define IRAM_ATTR
#define SERIAL_8N1 0
#define TWO_PI 6.28318530717958647692
typedef uint8_t byte;
extern uint32_t testMillis;
inline uint32_t millis() { return testMillis; }
inline void delay(uint32_t ms) { testMillis += ms; }
inline uint32_t esp_random() { return 0xBEEF; }
inline double radians(double d) { return d * TWO_PI / 360.0; }
inline double degrees(double r) { return r * 360.0 / TWO_PI; }
inline double sq(double x) { return x * x; }

class HardwareSerial {
public:
  std::string input, output;
  explicit HardwareSerial(int = 0) {}
  void begin(unsigned long, int = 0, int = 0, int = 0) {}
  void setRxBufferSize(size_t) {}
  int available() { return static_cast<int>(input.size()); }
  int read() {
    if (input.empty()) return -1;
    unsigned char c = input.front(); input.erase(0, 1); return c;
  }
  size_t write(const uint8_t *p, size_t n) {
    output.append(reinterpret_cast<const char *>(p), n); return n;
  }
  void println(const char *s = "") { output += s; output += '\n'; }
  void printf(const char *format, ...) {
    char buffer[1024];
    va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args); output += buffer;
  }
};
extern HardwareSerial Serial;
