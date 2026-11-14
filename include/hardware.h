#pragma once

// Provisional: classic ESP32 only. Verify the module marking before upload.
constexpr int LORA_SS = 5;
constexpr int LORA_RST = 14;
constexpr int LORA_DIO0 = 26;
constexpr int LORA_SCK = 18;
constexpr int LORA_MISO = 19;
constexpr int LORA_MOSI = 23;
constexpr long LORA_FREQUENCY_HZ = 433000000L;
constexpr int GPS_RX_PIN = 16;  // ESP32 RX <- GPS TX
constexpr int GPS_TX_PIN = 17;  // ESP32 TX -> GPS RX
constexpr unsigned long GPS_BAUD = 9600;
