/*
 * @Author: HoGC
 * @Date: 2022-04-16 13:32:25
 * @Last Modified time: 2022-04-16 13:32:25
 */
#include "app_shell.h"

#include <string.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <sys/select.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "driver/uart.h"
#include "esp_vfs.h"
#include "esp_vfs_dev.h"

#include "lwip/tcp.h"
#include "lwip/sockets.h"


static const char* TAG = "app_shell";

Shell shell;

static char g_shellBuffer[512];

static int g_uart_fd = -1;
static int g_tcps_fd = -1;
static int g_tcpc_fd = -1;

signed short _shell_write(char *data, unsigned short len)
{
    signed short uart_len = 0, tcp_len = 0;
    if(g_uart_fd != -1){
        uart_len = write(g_uart_fd, data, len);
    }
    if(g_tcpc_fd != -1){
        tcp_len = write(g_tcpc_fd, data, len);
    }
    return (uart_len>tcp_len?uart_len:tcp_len);
}

static void _shell_task(void *param)
{
    while(1)
    {
        int s;
        char data;
        int max_fd = -1;
        fd_set shell_rfds;
        struct timeval tv = {
            .tv_sec = 5,
            .tv_usec = 0,
        };

        if(g_uart_fd == -1){
            if ((g_uart_fd = open("/dev/uart/0", O_RDWR)) == -1) {
                ESP_LOGE(TAG, "Cannot open UART");
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }
            esp_vfs_dev_uart_use_driver(0);
        }

        FD_ZERO(&shell_rfds);
        FD_SET(g_tcps_fd, &shell_rfds);
        if(g_uart_fd != -1){
            FD_SET(g_uart_fd, &shell_rfds);
        }
        if(g_tcpc_fd != -1){
            FD_SET(g_tcpc_fd, &shell_rfds);
        }

        max_fd = (g_uart_fd>g_tcps_fd)?g_uart_fd:g_tcps_fd;
        if(g_tcpc_fd > max_fd){
            max_fd = g_tcpc_fd;
        }

        s = select(max_fd + 1, &shell_rfds, NULL, NULL, &tv);
        if (s < 0) {
            ESP_LOGE(TAG, "Select failed: errno %d", errno);
            break;
        } else if (s == 0) {
            continue;
        } else {
            if (FD_ISSET(g_tcps_fd, &shell_rfds)) {
                struct sockaddr_in client_addr;
				socklen_t client_addr_size = sizeof(client_addr);
                g_tcpc_fd = accept(g_tcps_fd, (struct sockaddr *)&client_addr, &client_addr_size);
                if(g_tcpc_fd < 0){
                    continue;
                }
                ESP_LOGI(TAG, "client connect: %d", g_tcpc_fd);
            }else if(FD_ISSET(g_uart_fd, &shell_rfds)){
                if (read(g_uart_fd, &data, 1) > 0) {
                    shellHandler(&shell, data);
                }else {
                    ESP_LOGE(TAG, "UART read error");
                    close(g_uart_fd);
                    g_uart_fd = -1;
                }
            }else if(FD_ISSET(g_tcpc_fd, &shell_rfds)){
                if (read(g_tcpc_fd, &data, 1) > 0) {
                    shellHandler(&shell, data);
                }else{
                    ESP_LOGI(TAG, "client disconnect: %d", g_tcpc_fd);
                    close(g_tcpc_fd);
                    g_tcpc_fd = -1;
                }
            }
        }
    }
}

static void _uart_init(){
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_driver_install(SHELL_UART, 256 * 2, 0, 0, NULL, 0);
    uart_param_config(SHELL_UART, &uart_config);

    uart_set_pin(SHELL_UART, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    if ((g_uart_fd = open("/dev/uart/0", O_RDWR)) == -1) {
        ESP_LOGE(TAG, "Cannot open UART");
        return;
    }

    esp_vfs_dev_uart_use_driver(0);
}

static void _tcp_init(void){

    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));

    if ((g_tcps_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        ESP_LOGE(TAG, "Cannot create socket");
        return;
    }

    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SHELL_TCPS_PORT);

    if (bind(g_tcps_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
        ESP_LOGE(TAG, "tsps bind error");
        return;
    }

    if (listen(g_tcps_fd, 1) != 0) {
        ESP_LOGE(TAG, "tsps listen error");
        return;
    }
}

int _log_vprintf(const char * fmt, va_list va){

    int ret;
    char *pbuf;

#if SHELL_SUPPORT_END_LINE == 1
    ret = vasprintf(&pbuf, fmt, va);
    if (ret < 0)
        return ret;
    shellWriteEndLine(&shell, pbuf, strlen(pbuf));
#else
    ret = vasprintf(&pbuf, fmt, va);
    printf(pbuf);
#endif
    free(pbuf);
    return ret;
}

#if SHELL_SUPPORT_END_LINE == 1
int _esp_log_vprintf(const char * fmt, va_list va){

    int ret;
    char *pbuf;

    ret = vasprintf(&pbuf, fmt, va);
    if (ret < 0)
        return ret;

    if(*pbuf != '\0' && *pbuf == '\n' && *(pbuf+1) == '\0'){
        shellWriteEndLine(&shell, pbuf, 0);
    }else{
        shellWriteEndLine(&shell, pbuf, strlen(pbuf));
    }
    free(pbuf);
    return ret;
}
#endif

int app_shell_printf(const char *fmt, ...){
    
    int ret;
    va_list va;

    va_start(va, fmt);
    ret = _log_vprintf(fmt, va);
    va_end(va);
    return ret;
}

int app_shell_init(void)
{
    _uart_init();

    _tcp_init();
    
    shell.write = _shell_write;
    shellInit(&shell, g_shellBuffer, 512);

    xTaskCreate(_shell_task, "_shell_task", 4096, NULL, 5, NULL);

#if SHELL_SUPPORT_END_LINE == 1
    esp_log_set_vprintf(_esp_log_vprintf);
#endif

    return 0;
}
