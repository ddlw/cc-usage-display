#include "net_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include <algorithm>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "secrets.h"

namespace {

constexpr uint32_t kRefreshIntervalMs = 15 * 1000;
constexpr uint32_t kPollIntervalMs = 100;
constexpr uint32_t kHttpTimeoutMs = 8000;

QueueHandle_t state_queue = nullptr;
QueueHandle_t result_queue = nullptr;
volatile bool force_refresh = false;
volatile bool open_dashboard = false;

bool postAction(const char* action, char* message, size_t message_size) {
  WiFiClient client;
  HTTPClient http;
  String url = String(DASHBOARD_ACTION_URL) + "/" + action;
  if (!http.begin(client, url)) {
    strlcpy(message, "ACTION URL INVALID", message_size);
    return false;
  }
  http.setTimeout(kHttpTimeoutMs);
  http.addHeader("Authorization", String("Bearer ") + DASHBOARD_TOKEN);
  const int status = http.POST("");
  http.end();

  if (status >= 200 && status < 300) {
    strlcpy(message, action[0] == 'f' ? "CC SWITCH OPENED" : "REFRESHED",
            message_size);
    return true;
  }
  strlcpy(message, "ACTION FAILED", message_size);
  return false;
}

void handleActions() {
  if (!open_dashboard) {
    return;
  }
  open_dashboard = false;
  NetActionResult result{};
  postAction("focus", result.message, sizeof(result.message));
  xQueueOverwrite(result_queue, &result);
}

bool fetchSnapshot(AppState& state) {
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, DASHBOARD_URL)) {
    Serial.println("[net] invalid dashboard URL");
    return false;
  }
  http.setTimeout(kHttpTimeoutMs);
  http.addHeader("Authorization", String("Bearer ") + DASHBOARD_TOKEN);
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("[net] GET failed: %d\n", status);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, payload);
  if (error) {
    Serial.printf("[net] JSON parse failed: %s\n", error.c_str());
    return false;
  }

  state = AppState{};
  state.unix_time = document["generated_at"] | 0ULL;
  state.utc_offset_minutes = document["utc_offset_minutes"] | 0;
  state.tokens_today = document["tokens_today"] | 0ULL;
  state.tokens_7d = document["tokens_7d"] | 0ULL;
  state.cost_today_cny_micros =
      document["cost_today_cny_micros"] | 0ULL;
  state.calls_today = document["calls_today"] | 0UL;
  state.errors_today = document["errors_today"] | 0UL;
  state.success_permille = std::min<uint16_t>(
      document["success_permille"] | 1000, 1000);
  state.average_ttft_ms = document["avg_ttft_ms"] | 0UL;
  strlcpy(state.latest_model, document["latest_model"] | "-",
          sizeof(state.latest_model));
  state.latest_status = document["latest_status"] | 0;
  state.latest_latency_ms = document["latest_latency_ms"] | 0UL;
  state.latest_failed = (document["latest_failed"] | false);
  state.session_count = document["session_count_30d"] | 0UL;
  state.total_calls = document["total_calls_30d"] | 0UL;
  state.total_input = document["input_30d"] | 0ULL;
  state.total_output = document["output_30d"] | 0ULL;
  state.total_cache_read = document["cache_read_30d"] | 0ULL;
  state.total_reasoning = document["cache_write_30d"] | 0ULL;
  state.total_cost_cny_micros = document["cost_30d_cny_micros"] | 0ULL;
  state.cache_hit_permille = std::min<uint16_t>(
      document["cache_hit_permille"] | 0, 1000);
  strlcpy(state.top_model, document["top_model"] | "-", sizeof(state.top_model));
  state.top_model_calls = document["top_model_calls"] | 0UL;
  state.time_valid = state.unix_time > 0;
  state.data_valid = true;
  return true;
}

void networkTask(void*) {
  uint32_t last_fetch_ms = 0;
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      const uint32_t now = millis();
      if (force_refresh || last_fetch_ms == 0 ||
          now - last_fetch_ms >= kRefreshIntervalMs) {
        force_refresh = false;
        AppState state;
        if (fetchSnapshot(state)) {
          xQueueOverwrite(state_queue, &state);
          Serial.printf("[net] snapshot: %llu tokens today, %lu calls\n",
                        static_cast<unsigned long long>(state.tokens_today),
                        static_cast<unsigned long>(state.calls_today));
        }
        last_fetch_ms = now;
      }
      handleActions();
    } else {
      last_fetch_ms = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
  }
}

}  // namespace

bool netBegin() {
  state_queue = xQueueCreate(1, sizeof(AppState));
  result_queue = xQueueCreate(1, sizeof(NetActionResult));
  if (state_queue == nullptr || result_queue == nullptr) {
    Serial.println("[net] queue allocation failed");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[net] connecting to %s\n", WIFI_SSID);

  return xTaskCreate(networkTask, "dashboard-net", 8192, nullptr, 1, nullptr) ==
         pdPASS;
}

bool netOnline() {
  return WiFi.status() == WL_CONNECTED;
}

void netReconnect() {
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void netRequestRefresh() {
  force_refresh = true;
}

void netRequestOpenDashboard() {
  open_dashboard = true;
}

bool netTakeState(AppState& state) {
  return state_queue != nullptr &&
         xQueueReceive(state_queue, &state, 0) == pdTRUE;
}

bool netTakeActionResult(NetActionResult& result) {
  return result_queue != nullptr &&
         xQueueReceive(result_queue, &result, 0) == pdTRUE;
}
