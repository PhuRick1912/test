/*
 * w25q64.h
 *
 *  Created on: Oct 8, 2026
 *      Author: PHU
 *
 *  Driver giao tiếp W25Q64 (8MB SPI Flash) qua SPI2
 *  STM32F103C8 - Bare Metal Register Level (Polling Mode - An toàn không xung đột DMA với UART1)
 *
 *  Kết nối phần cứng SPI2:
 *    PB13 = SCK
 *    PB14 = MISO
 *    PB15 = MOSI
 *    PB12 = CS (Software control)
 */

#ifndef W25Q64_H_
#define W25Q64_H_

#include "stm32f1xx.h"
#include <stdint.h>
#include <stdbool.h>
#include "w25q64_assets.h"

/* ============================================================
 *  THÔNG SỐ CHIP W25Q64
 * ============================================================ */
#define W25Q64_PAGE_SIZE          256           /* 1 Page = 256 bytes                */
#define W25Q64_SECTOR_SIZE        4096          /* 1 Sector = 4KB = 16 pages         */
#define W25Q64_BLOCK_32K_SIZE     32768         /* 1 Block 32KB                      */
#define W25Q64_BLOCK_64K_SIZE     65536         /* 1 Block 64KB = 16 sectors         */
#define W25Q64_TOTAL_SIZE         (8*1024*1024) /* 8MB = 64Mbit                      */
#define W25Q64_NUM_SECTORS        2048          /* Tổng số sectors                   */
#define W25Q64_NUM_PAGES          32768         /* Tổng số pages                     */

/* ============================================================
 *  BẢNG LỆNH SPI CỦA W25Q64 (INSTRUCTION SET)
 * ============================================================ */

/* --- Lệnh đọc/ghi trạng thái --- */
#define W25Q64_CMD_WRITE_ENABLE         0x06  /* Cho phép ghi                   */
#define W25Q64_CMD_WRITE_DISABLE        0x04  /* Cấm ghi                        */
#define W25Q64_CMD_READ_STATUS_REG1     0x05  /* Đọc Status Register 1          */
#define W25Q64_CMD_READ_STATUS_REG2     0x35  /* Đọc Status Register 2          */
#define W25Q64_CMD_WRITE_STATUS_REG     0x01  /* Ghi Status Register            */

/* --- Lệnh đọc dữ liệu --- */
#define W25Q64_CMD_READ_DATA            0x03  /* Đọc thường (max 50MHz)         */
#define W25Q64_CMD_FAST_READ            0x0B  /* Đọc nhanh (cần dummy byte)     */

/* --- Lệnh ghi dữ liệu --- */
#define W25Q64_CMD_PAGE_PROGRAM         0x02  /* Ghi 1 page (max 256 bytes)     */

/* --- Lệnh xóa --- */
#define W25Q64_CMD_SECTOR_ERASE_4K      0x20  /* Xóa 1 Sector 4KB              */
#define W25Q64_CMD_BLOCK_ERASE_32K      0x52  /* Xóa 1 Block 32KB              */
#define W25Q64_CMD_BLOCK_ERASE_64K      0xD8  /* Xóa 1 Block 64KB              */
#define W25Q64_CMD_CHIP_ERASE           0xC7  /* Xóa toàn bộ chip              */

/* --- Lệnh nhận dạng --- */
#define W25Q64_CMD_JEDEC_ID             0x9F  /* Đọc JEDEC ID (Manufacturer)   */
#define W25Q64_CMD_READ_UNIQUE_ID       0x4B  /* Đọc Unique ID                 */
#define W25Q64_CMD_DEVICE_ID            0xAB  /* Đọc Device ID                 */
#define W25Q64_CMD_MFTR_DEVICE_ID       0x90  /* Manufacturer + Device ID      */

/* --- Lệnh điều khiển nguồn --- */
#define W25Q64_CMD_POWER_DOWN           0xB9  /* Vào chế độ tiết kiệm năng lượng */
#define W25Q64_CMD_RELEASE_POWER_DOWN   0xAB  /* Thoát chế độ tiết kiệm         */

/* --- Bit mask Status Register 1 --- */
#define W25Q64_STATUS_BUSY              0x01  /* Bit BUSY: Chip đang xử lý     */
#define W25Q64_STATUS_WEL               0x02  /* Bit WEL: Write Enable Latch   */

/* --- JEDEC ID kỳ vọng của W25Q64 --- */
#define W25Q64_JEDEC_MANUFACTURER       0xEF  /* Winbond                        */
#define W25Q64_JEDEC_MEMORY_TYPE        0x40
#define W25Q64_JEDEC_CAPACITY           0x17  /* 64Mbit                         */

/* ============================================================
 *  CHÂN CS ĐIỀU KHIỂN (PB12 - Software NSS)
 * ============================================================ */
#define W25Q64_CS_LOW()     (GPIOB->BRR  = (1U << 12))  /* Kéo CS xuống 0 */
#define W25Q64_CS_HIGH()    (GPIOB->BSRR = (1U << 12))  /* Kéo CS lên 1   */

/* ============================================================
 *  API FUNCTIONS
 * ============================================================ */

/* --- Khởi tạo --- */
void W25Q64_Init(void);                 /* Khởi tạo SPI2 + GPIO */

/* --- Nhận dạng chip --- */
uint32_t W25Q64_ReadJEDEC_ID(void);     /* Trả về 3 byte: Manufacturer + Type + Capacity (Kỳ vọng: 0xEF4017) */
uint16_t W25Q64_ReadDeviceID(void);     /* Trả về Manufacturer + DeviceID */

/* --- Đọc dữ liệu --- */
void W25Q64_ReadData(uint32_t address, uint8_t *buffer, uint32_t length);
void W25Q64_FastRead(uint32_t address, uint8_t *buffer, uint32_t length);

/* --- Ghi dữ liệu --- */
void W25Q64_WritePage(uint32_t address, const uint8_t *data, uint16_t length);
void W25Q64_WriteData(uint32_t address, const uint8_t *data, uint32_t length);

/* --- Xóa --- */
void W25Q64_EraseSector(uint32_t sector_address);    /* Xóa 4KB Sector   */
void W25Q64_EraseBlock32K(uint32_t block_address);   /* Xóa 32KB Block   */
void W25Q64_EraseBlock64K(uint32_t block_address);   /* Xóa 64KB Block   */
void W25Q64_EraseChip(void);                         /* Xóa toàn bộ chip */

/* --- Trạng thái --- */
void W25Q64_WriteEnable(void);
void W25Q64_WriteDisable(void);
void W25Q64_WaitBusy(void);
uint8_t W25Q64_ReadStatusReg1(void);

/* --- Nguồn --- */
void W25Q64_PowerDown(void);
void W25Q64_WakeUp(void);

/* ============================================================
 *  API GIAO TIẾP VÀ HIỂN THỊ LÊN MÀN HÌNH LCD ST7789
 * ============================================================ */
void W25Q64_DrawAsset_ST7789(uint16_t x, uint16_t y, uint32_t flash_addr, uint16_t w, uint16_t h);
void W25Q64_DrawIcon50_ST7789(uint16_t x, uint16_t y, Asset_Icon_ID_t id);

/* ============================================================
 *  API GIAO THỨC CHUNK NẠP ẢNH TỪ UART VÀO FLASH (FLASH LOADER)
 * ============================================================ */
#define FLASH_LOADER_SYNC1          0x55
#define FLASH_LOADER_SYNC2          0xAA

#define FLASH_LOADER_CMD_HANDSHAKE  0xAA
#define FLASH_LOADER_CMD_ERASE      0xA1
#define FLASH_LOADER_CMD_WRITE      0xA2
#define FLASH_LOADER_CMD_READ       0xA3

#define FLASH_LOADER_RESP_OK        0x4F    /* 'O' - ACK  */
#define FLASH_LOADER_RESP_ERR       0x45    /* 'E' - NACK */

/* Xử lý 1 gói lệnh Chunk từ UART - gọi trong vòng lặp chính */
void W25Q64_UART_FlashLoader_Process(void);

/* ============================================================
 *  BẢNG ĐỊA CHỈ ICON THỜI TIẾT 32x32
 * ============================================================ */
#define WEATHER_ICON_WIDTH          32
#define WEATHER_ICON_HEIGHT         32
#define WEATHER_ICON_SIZE           (WEATHER_ICON_WIDTH * WEATHER_ICON_HEIGHT * 2) /* 2048 bytes */
#define WEATHER_ICON_MAX_COUNT      16

extern const uint32_t WEATHER_ICON_ADDR_TABLE[WEATHER_ICON_MAX_COUNT];
void W25Q64_ReadWeatherIcon(uint8_t icon_id, uint8_t *buffer);

#endif /* W25Q64_H_ */
