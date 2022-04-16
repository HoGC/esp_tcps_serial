/*
 * @Author: HoGC
 * @Date: 2022-04-16 13:34:25
 * @Last Modified time: 2022-04-16 13:34:25
 */
#ifndef __APP_PT_H__
#define __APP_PT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"

#define PT_DEFAULT_UART                UART_NUM_1
#define PT_DEFAULT_UART_TX             17
#define PT_DEFAULT_UART_RX             16
#define PT_DEFAULT_UART_BAUDRATE       115200

#define PT_DEFAULT_TCPS_PORT           5567

int app_pt_init(void);

#ifdef __cplusplus
}
#endif

#endif // __APP_PT_H__