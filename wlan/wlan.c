/***
 * 1) https://docs.espressif.com/projects/esp-idf/en/latest/hw-reference/get-started-wrover-kit.html#functionality-overview
 * 2) https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html#get-started-step-by-step
 */
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include "sdkconfig.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char* TAG = "PlayGround";
TaskHandle_t      Task0, Task1;
SemaphoreHandle_t mutexReceive;

/*** you may use section "##Task Configuration" in Kconfig.projbuild file or define it here ***/
#ifndef CONFIG_WLAN_TASK_0_STACKSIZE
#define CONFIG_WLAN_TASK_0_STACKSIZE 2048
#endif
#ifndef CONFIG_WLAN_TASK_1_STACKSIZE
#define CONFIG_WLAN_TASK_1_STACKSIZE 2048
#endif
/*** you may use section "##WLAN Configuration" in Kconfig.projbuild file or define it here ***/
#ifndef CONFIG_WLAN_SSID
#define CONFIG_WLAN_SSID "<ENTER YOUR WLAN SSID HERE>" 
#endif
#ifndef CONFIG_WLAN_PASS
#define CONFIG_WLAN_PASS "<ENTER YOUR WLAN PASSWORD HERE>"
#endif

void codeForTask0( void * parameter )
{
  ESP_LOGI(TAG, "START: Task 0 @ Core %d", xPortGetCoreID());
  for (;;) {
    xSemaphoreTake( mutexReceive, portMAX_DELAY );
    xSemaphoreGive( mutexReceive );
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
void codeForTask1( void * parameter )
{
  ESP_LOGI(TAG, "START: Task 1 @ Core %d", xPortGetCoreID());
  for (;;) {
    xSemaphoreTake( mutexReceive, portMAX_DELAY );
    xSemaphoreGive( mutexReceive );
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
void app_main()
{
//   esp_log_level_set(TAG, ESP_LOG_WARN); 
  esp_log_level_set(TAG, ESP_LOG_INFO); 
  ESP_LOGI(TAG, "Create the Semaphore");
  mutexReceive = xSemaphoreCreateMutex();
  ESP_LOGI(TAG, "Create task on core 0");
  xTaskCreatePinnedToCore(
    &codeForTask0,
    "Task_0",
    CONFIG_WLAN_TASK_0_STACKSIZE,
    NULL,
    1,
    &Task0,
    0
  );
  /* Provide some time for task creation */
//   vTaskDelay(pdMS_TO_TICKS(500));
  ESP_LOGI(TAG, "Create task on core 1");
  xTaskCreatePinnedToCore(
    &codeForTask1,
    "Task_1",
    CONFIG_WLAN_TASK_1_STACKSIZE,
    NULL,
    1,
    &Task1,
    1
  );
}
