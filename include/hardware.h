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

// Radio settings shared by every LoRa mode (lora_receiver, lora_sender, node_a,
// node_b). Boards only hear each other when all of these match, so they are
// defined once here and printed in each board's CFG log line. These are bench
// settings, not measured optimums or a regulatory approval. Changing any value
// here changes it for every environment; rebuild and reflash BOTH boards.
constexpr long LORA_BANDWIDTH_HZ = 125000L;
constexpr int LORA_SPREADING_FACTOR = 7;
constexpr int LORA_CODING_RATE_DENOM = 5;  // coding rate 4/5
constexpr long LORA_PREAMBLE_SYMBOLS = 8;  // LoRa library default, set explicitly
constexpr int LORA_SYNC_WORD = 0x12;
constexpr bool LORA_CRC_ENABLED = true;
constexpr int LORA_TX_POWER_DBM = 2;       // configured value, not a measured output
