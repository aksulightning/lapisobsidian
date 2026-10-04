#include <stdio.h>
#include <stdint.h>
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
#endif
#include <unistd.h>

#include "globals.h"
#include "server_config.h"

#ifdef ESP_PLATFORM
  #include "esp_task_wdt.h"
  #include "esp_timer.h"

  // Time between vTaskDelay calls in microseconds
  #define TASK_YIELD_INTERVAL 1000 * 1000
  // How many ticks to delay for on each yield
  #define TASK_YIELD_TICKS 1

  int64_t last_yield = 0;
  void task_yield () {
    int64_t time_now = esp_timer_get_time();
    if (time_now - last_yield < TASK_YIELD_INTERVAL) return;
    vTaskDelay(TASK_YIELD_TICKS);
    last_yield = time_now;
  }
#endif

ssize_t recv_count;
uint8_t recv_buffer[MAX_RECV_BUF_LEN] = {0};

uint8_t world_mirror_horizontal = 0;


ServerConfig server_config = SERVER_CONFIG_DEFAULTS;

#ifdef SEND_BRAND
  char brand[] = { "Lapis Obsidian" };
  uint8_t brand_len = sizeof(brand) - 1;
#endif

uint16_t client_count;


PlayerData player_data[MAX_PLAYERS];
int player_data_count = 0;


WorldState legacy_world = {.seed=INITIAL_WORLD_SEED,.random=INITIAL_RNG_SEED};
WorldState *active_world = &legacy_world;
