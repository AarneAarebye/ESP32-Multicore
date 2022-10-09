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

/*
 * This blinks two LEDs independently and not synchronized. Both have other blink frequencies. 
 * The blink sketches run in two tasks and on two cores.
- RGB LED ESP WROVER KIT 4.1:
  -	GPIO0	Red
  -	GPIO2	Green
  -	GPIO4	Blue
- Display Backlight:
  - GPIO5 Display backlight
*/

TaskHandle_t Task0, Task1;
SemaphoreHandle_t mutexReceive;

/*** you may use section "Blink Configuration" in Kconfig.projbuild file or define it here ***/
#ifndef CONFIG_BLINK_TASK_0_STACKSIZE
#define CONFIG_BLINK_TASK_0_STACKSIZE 1792 // actually something between 1536 and 1792
#endif
#ifndef CONFIG_BLINK_TASK_1_STACKSIZE
#define CONFIG_BLINK_TASK_1_STACKSIZE 1792 // actually something between 1536 and 1792
#endif
#ifndef CONFIG_BLINK_GPIO_0 
#define CONFIG_BLINK_GPIO_0 0
#endif
#ifndef CONFIG_BLINK_GPIO_1 
#define CONFIG_BLINK_GPIO_1 2 // 2 = LED_BUILTIN on ESP32 boards
#endif

int counter = 0;

void blink(uint8_t pin, TickType_t duration) {
  /* Blink off (output low) */
	printf("Turning on  the LED on GPIO%02d for %u ticks.\n", pin, pdMS_TO_TICKS(duration));
  gpio_set_level(pin, 1);
  vTaskDelay(pdMS_TO_TICKS(duration));
  /* Blink on (output high) */
	printf("Turning off the LED on GPIO%02d for %u ticks.\n", pin, pdMS_TO_TICKS(duration));
  gpio_set_level(pin, 0);
  vTaskDelay(pdMS_TO_TICKS(duration));
}

void codeForTask0( void * parameter )
{
  for (;;) {
    printf("Counter on Task 0 (Core %d): %d\n", xPortGetCoreID(), counter);
    xSemaphoreTake( mutexReceive, portMAX_DELAY );
    counter++;
    blink(CONFIG_BLINK_GPIO_0, 1000);
    xSemaphoreGive( mutexReceive );
    // vTaskDelay(pdMS_TO_TICKS(50));
  }
}
void codeForTask1( void * parameter )
{
  for (;;) {
    printf("Counter on Task 1 (Core %d): %d\n", xPortGetCoreID(), counter);
    xSemaphoreTake( mutexReceive, portMAX_DELAY );
    counter++;
    blink(CONFIG_BLINK_GPIO_1, 1000);
    xSemaphoreGive( mutexReceive );
    // vTaskDelay(pdMS_TO_TICKS(50));
  }
}
void app_main()
{
  /* Configure the IOMUX register for pad BLINK_GPI?? (some pads are
      muxed to GPIO on reset already, but some default to other
      functions and need to be switched to CONFIG_BLINK_GPIO_?. Consult the
      Technical Reference for a list of pads and their default
      functions.)
  */
  gpio_pad_select_gpio(CONFIG_BLINK_GPIO_0);
  gpio_pad_select_gpio(CONFIG_BLINK_GPIO_1);
  /* Set the GPIO as a push/pull output */
  gpio_set_direction(CONFIG_BLINK_GPIO_0, GPIO_MODE_OUTPUT);
  gpio_set_direction(CONFIG_BLINK_GPIO_1, GPIO_MODE_OUTPUT);
  /* Create the Semaphore */
  mutexReceive = xSemaphoreCreateMutex();
  /* Create task on core 0 */
  xTaskCreatePinnedToCore(
    &codeForTask0,
    "BLINK_Task_0",
    CONFIG_BLINK_TASK_0_STACKSIZE,
    NULL,
    1,
    &Task0,
    0
  );
  /* Create task on core 1 */
  xTaskCreatePinnedToCore(
    &codeForTask1,
    "BLINK_Task_1",
    CONFIG_BLINK_TASK_1_STACKSIZE,
    NULL,
    1,
    &Task1,
    1
  );
}
