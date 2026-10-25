#pragma once

#include <Arduino.h>
#include <stdint.h>

enum class ConnectionState : uint8_t {
  kDisconnected,
  kSyncing,
  kLinked,
  kStale,
};

struct AppState {
  uint8_t remaining_percent = 0;
  uint64_t tokens_today = 0;
  uint64_t tokens_7d = 0;
  uint32_t calls_today = 0;
  uint32_t errors_today = 0;
  uint32_t quota_reset_seconds = 0;
  uint32_t limit_window_minutes = 0;
  uint16_t success_permille = 1000;
  uint32_t average_ttft_ms = 0;
  char latest_model[32] = "-";
  uint16_t latest_status = 0;
  uint32_t latest_latency_ms = 0;
  bool latest_failed = false;
  uint16_t session_count = 0;
  uint32_t total_calls = 0;
  uint64_t total_input = 0;
  uint64_t total_output = 0;
  uint64_t total_cache_read = 0;
  uint64_t total_reasoning = 0;
  uint64_t total_cost_cny_micros = 0;
  uint64_t cost_today_cny_micros = 0;
  uint16_t cache_hit_permille = 0;
  char top_model[24] = "-";
  uint32_t top_model_calls = 0;

  uint64_t unix_time = 0;
  int16_t utc_offset_minutes = 0;
  ConnectionState connection = ConnectionState::kDisconnected;
  bool time_valid = false;
  bool data_valid = false;
};

String formatCompactTokens(uint64_t tokens);
String formatCountdown(uint32_t seconds);
String formatDateTime(const AppState& state, uint32_t elapsed_seconds = 0);
const char* connectionLabel(ConnectionState state);
