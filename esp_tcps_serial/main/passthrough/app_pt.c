/*
 * @Author: HoGC
 * @Date: 2022-04-16 13:34:14
 * @Last Modified time: 2022-04-16 13:34:14
 */
#include "app_pt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/tcp.h"
#include "lwip/sockets.h"

#include "esp_log.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "driver/uart.h"

#include "app_shell.h"

static char *TAG = "app_th";

#define BUF_SIZE        (1024)
#define RD_BUF_SIZE     (BUF_SIZE)

#define STORAGE_NAMESPACE "storage"

static QueueHandle_t g_uart_queue;

struct tcp_pcb* g_tcps_pcb = NULL;
struct tcp_pcb* g_tcpc_pcb = NULL;

static QueueHandle_t g_pt_data_queue = NULL;

typedef struct{
    uint8_t *data;
    uint16_t len;
}_pt_data_t;

typedef struct {
    uint16_t port;
}_tcps_cfg_t;

typedef struct {
    uint32_t baud;
    uint8_t tx_pin;
    uint8_t rx_pin;
}_uart_cfg_t;

static bool _set_uart_cfg(_uart_cfg_t uart_cfg){
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "nvs_open error");
        return false;
    }

    // Read
    size_t required_size = sizeof(_uart_cfg_t); 
    err = nvs_set_blob(nvs_handle, "uart_cfg", &uart_cfg, required_size);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "nvs_set_blob error");
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "nvs_commit error");
        nvs_close(nvs_handle);
        return false;
    }

    // Close
    nvs_close(nvs_handle);
    return true;
}

static bool _get_uart_cfg(_uart_cfg_t* uart_cfg){
    nvs_handle_t nvs_handle;
    esp_err_t err;

    if(!uart_cfg){
        return false;
    }

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "nvs_open error");
        return false;
    }

    // Read
    size_t required_size = sizeof(_uart_cfg_t); // value will default to 0, if not set yet in NVS
    err = nvs_get_blob(nvs_handle, "uart_cfg", uart_cfg, &required_size);
    if (err != ESP_OK){
        if(err == ESP_ERR_NVS_NOT_FOUND){
            ESP_LOGI(TAG, "nvs_get_blob blob not found");
        }else{
            ESP_LOGE(TAG, "nvs_get_blob error");
            printf("Error (%s) reading!\n", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
        return false;
    }

    // Close
    nvs_close(nvs_handle);
    return true;
}

static bool _set_tcps_cfg(_tcps_cfg_t tcps_cfg){
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "nvs_open error");
        return false;
    }

    // Read
    size_t required_size = sizeof(_tcps_cfg_t); 
    err = nvs_set_blob(nvs_handle, "tcps_cfg", &tcps_cfg, required_size);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "nvs_set_blob error");
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "nvs_commit error");
        nvs_close(nvs_handle);
        return false;
    }

    // Close
    nvs_close(nvs_handle);
    return true;
}

static bool _get_tcps_cfg(_tcps_cfg_t* tcps_cfg){
    nvs_handle_t nvs_handle;
    esp_err_t err;

    if(!tcps_cfg){
        return false;
    }

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "nvs_open error");
        return false;
    }

    // Read
    size_t required_size = sizeof(_tcps_cfg_t); // value will default to 0, if not set yet in NVS
    err = nvs_get_blob(nvs_handle, "tcps_cfg", tcps_cfg, &required_size);
    if (err != ESP_OK){
        if(err == ESP_ERR_NVS_NOT_FOUND){
            ESP_LOGI(TAG, "nvs_get_blob blob not found");
        }else{
            ESP_LOGE(TAG, "nvs_get_blob error");
            printf("Error (%s) reading!\n", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
        return false;
    }

    // Close
    nvs_close(nvs_handle);
    return true;
}

static void _pt_task(void *arg){
    _pt_data_t *pt_data = NULL;

    while(1){
        if(xQueueReceive(g_pt_data_queue, &pt_data, (TickType_t) portMAX_DELAY) == pdPASS){
            if(g_tcpc_pcb){
                err_t err;
                err = tcp_write(g_tcpc_pcb, pt_data->data, pt_data->len, TCP_WRITE_FLAG_COPY);
                if(err == ERR_OK){
                    tcp_output(g_tcpc_pcb);
                }
            }
            free(pt_data->data);
            free(pt_data);
        }
    }
}

static err_t _tcps_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    if(p != NULL){
        uart_write_bytes(PT_DEFAULT_UART, (const char*)p->payload, p->tot_len);
        pbuf_free(p);
    }else if(err == ERR_OK){
        tcp_close(pcb);
        g_tcpc_pcb = NULL;
    }

    return ERR_OK;
}

static void _tcps_err(void *arg, err_t err){

    if(g_tcpc_pcb){
        tcp_close(g_tcpc_pcb);
    }
}

static err_t _tcps_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    if(g_tcpc_pcb){
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    g_tcpc_pcb = pcb;

    tcp_recv(g_tcpc_pcb, _tcps_recv);
    tcp_err(g_tcpc_pcb, _tcps_err);
    return ERR_OK;
}

int _pt_send(const uint8_t *data, uint16_t len){

    if(len == 0){
        return 0;
    }

    int size = 0;
    uint8_t *data_copy = 0;

    _pt_data_t *pt_data = NULL;

    pt_data = malloc(sizeof(_pt_data_t));
    if(!pt_data){
        return 0;
    }

    data_copy = malloc(len);
    if(!data_copy){
        free(pt_data);
        return 0;
    }
    memcpy(data_copy, data, len);

    pt_data->len = len;
    pt_data->data = data_copy;
    
    if(g_pt_data_queue){
        if(xQueueSendToBack(g_pt_data_queue, &pt_data, (TickType_t) 0/portTICK_PERIOD_MS) == pdPASS){
            size = len;
        }
    }
    return size;
}

static void _recv_task(void *arg)
{
    uart_event_t event;
    uint8_t* data = (uint8_t*) malloc(RD_BUF_SIZE);
    if(!data){
        return;
    }
    bzero(data, RD_BUF_SIZE);
    for(;;) {
        //Waiting for UART event.
        if(xQueueReceive(g_uart_queue, (void * )&event, (TickType_t)portMAX_DELAY)) {
            switch(event.type) {
                case UART_DATA:
                    uart_read_bytes(PT_DEFAULT_UART, data, event.size, portMAX_DELAY);
                    _pt_send(data, event.size);
                    break;
                default:
                    break;
            }
        }
    }
    free(data);
    data = NULL;
    vTaskDelete(NULL);
}

static int _pt_uart_init(void){

    _uart_cfg_t uart_cfg = {0};

    uart_config_t uart_config = {
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    bzero(&uart_cfg, sizeof(_uart_cfg_t));

    if(!_get_uart_cfg(&uart_cfg)){
        uart_cfg.baud = PT_DEFAULT_UART_BAUDRATE;
        uart_cfg.tx_pin = PT_DEFAULT_UART_TX;
        uart_cfg.rx_pin = PT_DEFAULT_UART_RX;
        _set_uart_cfg(uart_cfg);
    }

    uart_config.baud_rate = uart_cfg.baud;

    if((uart_cfg.baud <= 0) || (uart_cfg.tx_pin == uart_cfg.rx_pin)){
        ESP_LOGE(TAG, "uart_cfg error"); 
        return -1;
    }

    ESP_LOGI(TAG, "uart_cfg baud: %d  tx: %d  rx: %d", uart_cfg.baud, uart_cfg.tx_pin, uart_cfg.rx_pin);

    if(uart_is_driver_installed(PT_DEFAULT_UART)){
        uart_driver_delete(PT_DEFAULT_UART);
    }

    //Install UART driver, and get the queue.
    uart_driver_install(PT_DEFAULT_UART, BUF_SIZE * 2, BUF_SIZE * 2, 20, &g_uart_queue, 0);
    uart_param_config(PT_DEFAULT_UART, &uart_config);

    uart_set_pin(PT_DEFAULT_UART, uart_cfg.tx_pin, uart_cfg.rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    return 0;
}

static int _pt_tcps_init(void){

    _tcps_cfg_t tcps_cfg = {0};

    bzero(&tcps_cfg, sizeof(_tcps_cfg_t));

    if(!_get_tcps_cfg(&tcps_cfg)){
        tcps_cfg.port = PT_DEFAULT_TCPS_PORT;
        _set_tcps_cfg(tcps_cfg);
    }

    ESP_LOGI(TAG, "tcps_cfg port: %d", tcps_cfg.port);

    if(g_tcps_pcb){
        tcp_close(g_tcps_pcb);
    }

    g_tcps_pcb = tcp_new();
    if(!g_tcps_pcb){
        ESP_LOGE(TAG, "Cannot create pcb");
        return -1;
    }

    tcp_bind(g_tcps_pcb, INADDR_ANY, tcps_cfg.port);
    g_tcps_pcb = tcp_listen(g_tcps_pcb);
    tcp_accept(g_tcps_pcb, _tcps_accept);

    return 0;
}


int app_pt_init(void){

    _pt_uart_init();

    _pt_tcps_init();

    g_pt_data_queue  = xQueueCreate(512, sizeof(_pt_data_t));

    if(!g_pt_data_queue){
        ESP_LOGE(TAG, "Queue create error");
        return -1;
    }

    xTaskCreate(_recv_task, "_recv_task", 2048, NULL, 5, NULL);
    xTaskCreate(_pt_task, "_pt_task", 2048, NULL, 7, NULL);

    return 0;
}

#include "argtable3/argtable3.h"

static struct {
    struct arg_int *baud;
    struct arg_int *tx_pin;
    struct arg_int *rx_pin;
    struct arg_end *end;
} uart_args;

int set_uart(int argc, char *argv[]){

    int nerrors;

    _uart_cfg_t uart_cfg = {0};
    bzero(&uart_cfg, sizeof(_uart_cfg_t));

    if(!_get_uart_cfg(&uart_cfg)){
        uart_cfg.baud = PT_DEFAULT_UART_BAUDRATE;
        uart_cfg.tx_pin = PT_DEFAULT_UART_TX;
        uart_cfg.rx_pin = PT_DEFAULT_UART_RX;
    }

    uart_args.baud = arg_int0("b", "baud", "<baud>", "baud rate");
    uart_args.tx_pin = arg_int0("t", "tx", "<tx_pin>", "tx pin");
    uart_args.rx_pin = arg_int0("r", "rx", "<rx_pin>", "rx pin");
    uart_args.end = arg_end(3);

    nerrors = arg_parse(argc, argv, (void **)&uart_args);
    printf("nerrors: %d\n", nerrors);
    if (nerrors != 0) {
        arg_print_errors(stderr, uart_args.end, argv[0]);
        return -1;
    }

    if(uart_args.baud->count == 0 
            && uart_args.tx_pin->count == 0
            && uart_args.rx_pin->count == 0){
        return -1;   
    }

    if(uart_args.baud->count != 0){
        ESP_LOGI(TAG, "set baud: %d", uart_args.baud->ival[0]);
        uart_cfg.baud = uart_args.baud->ival[0];
    }

    if(uart_args.tx_pin->count != 0){
        ESP_LOGI(TAG, "tx_pin: %d", uart_args.tx_pin->ival[0]);
        uart_cfg.tx_pin = uart_args.tx_pin->ival[0];

    }

    if(uart_args.rx_pin->count != 0){
        ESP_LOGI(TAG, "tx_pin: %d", uart_args.rx_pin->ival[0]);
        uart_cfg.rx_pin = uart_args.rx_pin->ival[0];
    }

    _set_uart_cfg(uart_cfg);

    _pt_uart_init();

    return 0;
}

static struct {
    struct arg_int *port;
    struct arg_end *end;
} tcps_args;

int set_tcps(int argc, char *argv[]){

    int nerrors;

    _tcps_cfg_t tcps_cfg = {0};
    bzero(&tcps_cfg, sizeof(_tcps_cfg_t));

    if(!_get_tcps_cfg(&tcps_cfg)){
        tcps_cfg.port = PT_DEFAULT_TCPS_PORT;
    }

    tcps_args.port = arg_int1("p", "port", "<port>", "tcps port");
    tcps_args.end = arg_end(1);

    nerrors = arg_parse(argc, argv, (void **)&tcps_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, tcps_args.end, argv[0]);
        return -1;
    }

    if(tcps_args.port->count != 0){
        ESP_LOGI(TAG, "set port: %d", tcps_args.port->ival[0]);
        tcps_cfg.port = tcps_args.port->ival[0];
    }

    _set_tcps_cfg(tcps_cfg);

    _pt_tcps_init();

    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), set_tcps, set_tcps, set tcps cfg);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), set_uart, set_uart, set uart cfg);