/***
 * 1) https://docs.espressif.com/projects/esp-idf/en/latest/hw-reference/get-started-wrover-kit.html#functionality-overview
 * 2) https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html#get-started-step-by-step
 */

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

#include <lwip/err.h>
#include <lwip/sys.h>

#include "app_prov.h"

#include "sdkconfig.h"


// TWDT (Task Watch Dog Timer)
#define TASK_0_RESET_PERIOD_S    2 // CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
#define TASK_1_RESET_PERIOD_S    2 // CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
#define TWDT_TIMEOUT_S           TASK_0_RESET_PERIOD_S  > TASK_1_RESET_PERIOD_S ? TASK_0_RESET_PERIOD_S + 1 : TASK_1_RESET_PERIOD_S + 1 // CONFIG_ESP_TASK_WDT_TIMEOUT_S

static const char* TAG = "bleProvisioning";
static TaskHandle_t      task_handles[portNUM_PROCESSORS];
static SemaphoreHandle_t mutexReceive;
 
/*** you may use section "##Task Configuration" in Kconfig.projbuild file or define it here ***/
#ifndef CONFIG_TASK_0_STACKSIZE
#define CONFIG_TASK_0_STACKSIZE 2048
#endif
#ifndef CONFIG_TASK_1_STACKSIZE
#define CONFIG_TASK_1_STACKSIZE 2048
#endif
/*** you may use section "##WLAN Configuration" in Kconfig.projbuild file or define it here ***/
#ifndef CONFIG_WLAN_SSID
#define CONFIG_WLAN_SSID "<ENTER YOUR WLAN SSID HERE>" 
#endif
#ifndef CONFIG_WLAN_PASS
#define CONFIG_WLAN_PASS "<ENTER YOUR WLAN PASSWORD HERE>"
#endif

/*
 * Macro to check the outputs of TWDT functions and trigger an abort if an
 * incorrect code is returned.
 */
#define CHECK_ERROR_CODE(returned, expected) ({ \
    if (returned != expected) {                 \
        ESP_LOGE(TAG, "TWDT ERROR");            \
        abort();                                \
    }                                           \
})
static void start_ble_provisioning(void);

static void event_handler(void* arg, esp_event_base_t event_base,
                          int event_id, void* event_data)
{
    static int s_retry_num_ap_not_found = 0;
    static int s_retry_num_ap_auth_fail = 0;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        switch (disconnected->reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_BEACON_TIMEOUT:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            ESP_LOGW(TAG, "connect to the AP fail : auth Error");
            if (s_retry_num_ap_auth_fail < CONFIG_WLAN_AP_RECONN_ATTEMPTS) {
                s_retry_num_ap_auth_fail++;
                esp_wifi_connect();
                ESP_LOGI(TAG, "retry connecting to the AP...");
            } else {
                /* Restart provisioning if authentication fails */
                start_ble_provisioning();
            }
            break;
        case WIFI_REASON_NO_AP_FOUND:
            ESP_LOGW(TAG, "connect to the AP fail : not found");
            if (s_retry_num_ap_not_found < CONFIG_WLAN_AP_RECONN_ATTEMPTS) {
                s_retry_num_ap_not_found++;
                esp_wifi_connect();
                ESP_LOGI(TAG, "retry to connecting to the AP...");
            }
            break;
        default:
            /* None of the expected reasons */
            esp_wifi_connect();
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num_ap_not_found = 0;
        s_retry_num_ap_auth_fail = 0;
    }
}

static void wifi_init_sta(void)
{
    /* Set our event handling */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));

    /* Start Wi-Fi in station mode with credentials set during provisioning */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}
static void wifi_stop_sta() {
    CHECK_ERROR_CODE(esp_wifi_stop(), ESP_OK);
}
static void start_ble_provisioning(void)
{
    /* Security version */
    int security = 0;
    /* Proof of possession */
    const protocomm_security_pop_t *pop = NULL;

#ifdef CONFIG_USE_SEC_1
    security = 1;
#endif

    /* Having proof of possession is optional */
#ifdef CONFIG_USE_POP
    const static protocomm_security_pop_t app_pop = {
        .data = (uint8_t *) CONFIG_POP,
        .len = (sizeof(CONFIG_POP)-1)
    };
    pop = &app_pop;
#endif

    ESP_ERROR_CHECK(app_prov_start_ble_provisioning(security, pop));
}
void codeForTask0( void * parameter )
{
    ESP_LOGD(TAG, "START: Task 0 @ Core %d", xPortGetCoreID());
    //Subscribe this task to TWDT, then check if it is subscribed
    CHECK_ERROR_CODE(esp_task_wdt_add(NULL), ESP_OK);
    CHECK_ERROR_CODE(esp_task_wdt_status(NULL), ESP_OK);
    for (;;) {
        xSemaphoreTake( mutexReceive, portMAX_DELAY );
        vTaskDelay(pdMS_TO_TICKS(500));
        xSemaphoreGive( mutexReceive );
        CHECK_ERROR_CODE(esp_task_wdt_reset(), ESP_OK);  //Comment this line to trigger a TWDT timeout
        vTaskDelay(pdMS_TO_TICKS(TASK_0_RESET_PERIOD_S * 1000));
    }
}
void codeForTask1( void * parameter ) // receiver task
{
    ESP_LOGD(TAG, "START: Task 1 @ Core %d", xPortGetCoreID());
    //Subscribe this task to TWDT, then check if it is subscribed
    CHECK_ERROR_CODE(esp_task_wdt_add(NULL), ESP_OK);
    CHECK_ERROR_CODE(esp_task_wdt_status(NULL), ESP_OK);
    for (;;) {
        xSemaphoreTake( mutexReceive, portMAX_DELAY );
        vTaskDelay(pdMS_TO_TICKS(500));
        xSemaphoreGive( mutexReceive );
        CHECK_ERROR_CODE(esp_task_wdt_reset(), ESP_OK);  //Comment this line to trigger a TWDT timeout
        vTaskDelay(pdMS_TO_TICKS(TASK_1_RESET_PERIOD_S * 1000));
    }
}
void createTasks() {
    ESP_LOGD(TAG, "Subscribing and creating tasks");
    ESP_LOGD(TAG, "Create the Semaphore");
    mutexReceive = xSemaphoreCreateMutex();
    ESP_LOGD(TAG, "Initialize or reinitialize TWDT");
    CHECK_ERROR_CODE(esp_task_wdt_init(TWDT_TIMEOUT_S, false), ESP_OK);
    //Subscribe Idle Tasks to TWDT if they were not subscribed at startup
#ifdef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
    ESP_LOGD(TAG, "Subscribing idle task on core %d", 0);
    esp_task_wdt_add(xTaskGetIdleTaskHandleForCPU(0));
#endif
#ifdef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
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
}
void deleteTasks() {
    ESP_LOGD(TAG, "Unsubscribing and deleting tasks");
    //Delete and unsubscribe Users Tasks from Task Watchdog, then unsubscribe idle task
    for (int i = 0; i < 2; i++) { // portNUM_PROCESSORS = 2
        ESP_LOGD(TAG, "Shutdown task %d", i);
        ESP_LOGD(TAG, "Take Mutex");
        xSemaphoreTake( mutexReceive, portMAX_DELAY );
        ESP_LOGD(TAG, "Deleting task %d", i);
        vTaskDelete(task_handles[i]);   //Delete user task first (prevents the resetting of an unsubscribed task)
        ESP_LOGD(TAG, "Give Mutex");
        xSemaphoreGive( mutexReceive );
        ESP_LOGD(TAG, "Unsubscribing task %d", i);
        CHECK_ERROR_CODE(esp_task_wdt_delete(task_handles[i]), ESP_OK);     //Unsubscribe task from TWDT
        ESP_LOGD(TAG, "Unsubscribing task %d check", i);
        CHECK_ERROR_CODE(esp_task_wdt_status(task_handles[i]), ESP_ERR_NOT_FOUND);  //Confirm task is unsubscribed
        if (i == 0) {
#ifdef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
            ESP_LOGD(TAG, "Unsubscribing idle task on core %d", i);
            CHECK_ERROR_CODE(esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(i)), ESP_OK);     //Unsubscribe Idle Task from TWDT
            ESP_LOGD(TAG, "Unsubscribing idle task on core %d check", i);
            CHECK_ERROR_CODE(esp_task_wdt_status(xTaskGetIdleTaskHandleForCPU(i)), ESP_ERR_NOT_FOUND);      //Confirm Idle task has unsubscribed
#endif
        }
        if (i == 1) {
#ifdef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
            ESP_LOGD(TAG, "Unsubscribing idle task on core %d", i);
            CHECK_ERROR_CODE(esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(i)), ESP_OK);     //Unsubscribe Idle Task from TWDT
            ESP_LOGD(TAG, "Unsubscribing idle task on core %d check", i);
            CHECK_ERROR_CODE(esp_task_wdt_status(xTaskGetIdleTaskHandleForCPU(i)), ESP_ERR_NOT_FOUND);      //Confirm Idle task has unsubscribed
#endif
        }
    }
    ESP_LOGD(TAG, "Deinit TWDT after all tasks have unsubscribed");
    CHECK_ERROR_CODE(esp_task_wdt_deinit(), ESP_OK); // TWDT successfully deinitialized
    ESP_LOGD(TAG, "Deinit TWDT after all tasks have unsubscribed - check");
    CHECK_ERROR_CODE(esp_task_wdt_status(NULL), ESP_ERR_INVALID_STATE);     //Confirm TWDT has been deinitialized
    ESP_LOGD(TAG, "Delete the mutex");
    vSemaphoreDelete(mutexReceive);
}
void initNetworkAndProvisioning() {
    ESP_LOGD(TAG, "Initialize networking stack");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_LOGD(TAG, "Create default event loop needed by the");
    ESP_LOGD(TAG, "main app and the provisioning service");
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGD(TAG, "Initialize NVS needed by Wi-Fi");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGD(TAG, "Initialize Wi-Fi including netif with default config");
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_LOGD(TAG, "Check if device is provisioned");
    bool provisioned;
    if (app_prov_is_provisioned(&provisioned) != ESP_OK) {
        ESP_LOGE(TAG, "Error getting device provisioning state");
        abort();
    }
    if (provisioned == false) {
        /* If not provisioned, start provisioning via BLE */
        ESP_LOGI(TAG, "Starting BLE provisioning");
        start_ble_provisioning();
    } else {
        /* Else start as station with credentials set during provisioning */
        ESP_LOGI(TAG, "Starting WiFi station");
        wifi_init_sta();
    }
}
void deInitNetworkAndProvisioning() {
    app_prov_stop_ble_provisioning();
    wifi_stop_sta(); // ignore "not initialized"
}
void app_main()
{
    // esp_log_level_set(TAG, ESP_LOG_WARN); 
    // esp_log_level_set(TAG, ESP_LOG_INFO); 
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    ESP_LOGI(TAG, "Start Up");
    ESP_LOGD(TAG, "Initialize networking stack");
    initNetworkAndProvisioning();
    ESP_LOGD(TAG, "Starting tasks and Task Watch Dog");
    createTasks();
    ESP_LOGD(TAG, "Dummy Delay for 120 seconds");
    vTaskDelay(pdMS_TO_TICKS(120000));   //Delay for 120 seconds
    ESP_LOGI(TAG, "Shut Down");
    ESP_LOGD(TAG, "Deleting tasks and Task Watch Dog ");
    deleteTasks();
    ESP_LOGD(TAG, "Deinitialize networking stack");
    deInitNetworkAndProvisioning();
    ESP_LOGI(TAG, "Complete");
}
