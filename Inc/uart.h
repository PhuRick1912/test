#ifndef UART_H_
#define UART_H_

#include "stm32f1xx.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define RX_RING_SIZE 256

void UART1_DMA_Init(void);
void UART1_DMA_Transmit(uint8_t *data, uint16_t len);

// Các hàm API để tầng Application lấy dữ liệu (dạng text / string)
bool UART1_Is_Data_Ready(void);
void UART1_Clear_Data_Flag(void);
uint8_t* UART1_Get_Rx_Buffer(void);
void UART1_Clear_Rx_Buffer(void);

// Các hàm API đọc raw byte nhị phân (dùng cho Flash Loader nạp W25Q64, không bị nuốt 0x00 và 0xFF)
uint16_t UART1_Get_Available_Bytes(void);
uint8_t  UART1_Read_Raw_Byte(void);
void     UART1_Flush_Rx(void);

#endif /* UART_H_ */
