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
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

// TWDT (Task Watch Dog Timer)
#define TASK_0_RESET_PERIOD_S    2
#define TASK_1_RESET_PERIOD_S    2
#define TWDT_TIMEOUT_S           TASK_0_RESET_PERIOD_S  > TASK_1_RESET_PERIOD_S ? TASK_0_RESET_PERIOD_S + 1 : TASK_1_RESET_PERIOD_S + 1 

static const char* TAG = "PlayGround";
static TaskHandle_t task_handles[portNUM_PROCESSORS];
SemaphoreHandle_t mutexReceive;

/*** you may use section "##Task Configuration" in Kconfig.projbuild file or define it here ***/
#ifndef CONFIG_TASK_0_STACKSIZE
#define CONFIG_TASK_0_STACKSIZE 2048
#endif
#ifndef CONFIG_TASK_1_STACKSIZE
#define CONFIG_TASK_1_STACKSIZE 2048
#endif

/*
 * Macro to check the outputs of TWDT functions and trigger an abort if an
 * incorrect code is returned.
 */
#define CHECK_ERROR_CODE(returned, expected) ({                \
    if (returned != expected) {                                \
        ESP_LOGE(TAG, "TWDT ERROR");                           \
        abort();                                               \
    }                                                          \
})
void codeForTask0( void * parameter )
{
    ESP_LOGD(TAG, "START: Task 0 @ Core %d", xPortGetCoreID());
    //Subscribe this task to TWDT, then check if it is subscribed
    CHECK_ERROR_CODE(esp_task_wdt_add(NULL), ESP_OK);
    CHECK_ERROR_CODE(esp_task_wdt_status(NULL), ESP_OK);
    for (;;) {
        xSemaphoreTake( mutexReceive, portMAX_DELAY );
        xSemaphoreGive( mutexReceive );
        CHECK_ERROR_CODE(esp_task_wdt_reset(), ESP_OK);  //Comment this line to trigger a TWDT timeout
        vTaskDelay(pdMS_TO_TICKS(TASK_0_RESET_PERIOD_S * 1000));
    }
}
void codeForTask1( void * parameter )
{
    ESP_LOGD(TAG, "START: Task 1 @ Core %d", xPortGetCoreID());
    //Subscribe this task to TWDT, then check if it is subscribed
    CHECK_ERROR_CODE(esp_task_wdt_add(NULL), ESP_OK);
    CHECK_ERROR_CODE(esp_task_wdt_status(NULL), ESP_OK);
    for (;;) {
        xSemaphoreTake( mutexReceive, portMAX_DELAY );
        xSemaphoreGive( mutexReceive );
        CHECK_ERROR_CODE(esp_task_wdt_reset(), ESP_OK);  //Comment this line to trigger a TWDT timeout
        vTaskDelay(pdMS_TO_TICKS(TASK_1_RESET_PERIOD_S * 1000));
    }
}
void app_main()
{
//   esp_log_level_set(TAG, ESP_LOG_WARN); 
    esp_log_level_set(TAG, ESP_LOG_INFO); 
    ESP_LOGI(TAG, "Start Up");
    ESP_LOGD(TAG, "Create the Semaphore");
    mutexReceive = xSemaphoreCreateMutex();
    ESP_LOGD(TAG, "Initialize or reinitialize TWDT");
    CHECK_ERROR_CODE(esp_task_wdt_init(TWDT_TIMEOUT_S, false), ESP_OK);
    //Subscribe Idle Tasks to TWDT if they were not subscribed at startup
#ifndef CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU0
    ESP_LOGD(TAG, "Subscribing idle task on core %d", 0);
    esp_task_wdt_add(xTaskGetIdleTaskHandleForCPU(0));
#endif
#ifndef CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU1
    ESP_LOGD(TAG, "Subscribing idle task on core %d", 1);
    esp_task_wdt_add(xTaskGetIdleTaskHandleForCPU(1));
#endif
    ESP_LOGD(TAG, "Create task on core 0");
    xTaskCreatePinnedToCore(
        &codeForTask0,
        "Task_0",
        CONFIG_TASK_0_STACKSIZE,
        NULL,
        1,
        &task_handles[0],
        0
    );
    ESP_LOGD(TAG, "Create task on core 1");
    xTaskCreatePinnedToCore(
        &codeForTask1,
        "Task_1",
        CONFIG_TASK_1_STACKSIZE,
        NULL,
        1,
        &task_handles[1],
        1
    );
    ESP_LOGD(TAG, "Delay for 10 seconds");
    vTaskDelay(pdMS_TO_TICKS(10000));   //Delay for 10 seconds
    ESP_LOGD(TAG, "Unsubscribing and deleting tasks");
    //Delete and unsubscribe Users Tasks from Task Watchdog, then unsubscribe idle task
    for(int i = 0; i < 2; i++) { // portNUM_PROCESSORS = 2
        ESP_LOGD(TAG, "Deleting task %d", i);
        vTaskDelete(task_handles[i]);   //Delete user task first (prevents the resetting of an unsubscribed task)
        ESP_LOGD(TAG, "Unsubscribing task %d", i);
        CHECK_ERROR_CODE(esp_task_wdt_delete(task_handles[i]), ESP_OK);     //Unsubscribe task from TWDT
        ESP_LOGD(TAG, "Unsubscribing task %d check", i);
        CHECK_ERROR_CODE(esp_task_wdt_status(task_handles[i]), ESP_ERR_NOT_FOUND);  //Confirm task is unsubscribed
        ESP_LOGD(TAG, "Unsubscribing idle task on core %d", i);
        CHECK_ERROR_CODE(esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(i)), ESP_OK);     //Unsubscribe Idle Task from TWDT
        ESP_LOGD(TAG, "Unsubscribing idle task %d check", i);
        CHECK_ERROR_CODE(esp_task_wdt_status(xTaskGetIdleTaskHandleForCPU(i)), ESP_ERR_NOT_FOUND);      //Confirm Idle task has unsubscribed
    }
    //Deinit TWDT after all tasks have unsubscribed
    CHECK_ERROR_CODE(esp_task_wdt_deinit(), ESP_OK);
    CHECK_ERROR_CODE(esp_task_wdt_status(NULL), ESP_ERR_INVALID_STATE);     //Confirm TWDT has been deinitialized
    ESP_LOGI(TAG, "Complete");
}
