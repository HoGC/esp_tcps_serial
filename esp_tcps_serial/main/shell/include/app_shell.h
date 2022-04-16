/*
 * @Author: HoGC
 * @Date: 2022-04-16 13:32:43
 * @Last Modified time: 2022-04-16 13:32:43
 */
#ifndef __APP_SHELL_H__
#define __APP_SHELL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "shell.h"
#include "shell_cfg.h"

#define SHELL_UART                UART_NUM_0
#define SHELL_UART_TX             UART_PIN_NO_CHANGE
#define SHELL_UART_RX             UART_PIN_NO_CHANGE
#define SHELL_UART_BAUDRATE       115200

#define SHELL_TCPS_PORT           5568

extern Shell shell;

int app_shell_init(void);

int app_shell_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif // __APP_SHELL_H__
