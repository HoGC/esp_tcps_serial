/*
 * @Author: HoGC
 * @Date: 2022-04-16 13:31:30
 * @Last Modified time: 2022-04-16 13:31:30
 */
#include <stdio.h>

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "app_wifi.h"
#include "app_shell.h"
#include "app_pt.h"

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());

    app_init_wifi();

    app_shell_init();

    app_pt_init();
}
