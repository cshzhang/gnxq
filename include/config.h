#ifndef _CONFIG_H
#define _CONFIG_H


#define uchar unsigned char

#define DBG_PRINTF printf
//#define DBG_PRINTF(...)

#define SERVER_PORT 9898

#define CAN1_DEV_NAME "can0"
#define CAN2_DEV_NAME "can1"

#define CANCFG_PATH   	"/etc/can_cfg/"
#define CANDATA_PATH 	"/etc/can_data/"
#define CANLOG_PATH 	"/etc/can_log/"

//UART
#define UART_PORT "/dev/ttyS1"

#define FILE_MACSTATION "/etc/can_cfg/macstation.cfg"

typedef unsigned char u8;

#endif


