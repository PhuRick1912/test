/*
 * w25q64.c
 *
 *  Created on: Oct 8, 2026
 *      Author: PHU
 *
 *  Driver W25Q64 giao tiếp qua SPI2 - STM32F103C8 Bare Metal
 *  Chế độ Polling (Không chiếm kênh DMA1 Channel 4/5, an toàn tuyệt đối với UART1 DMA)
 *
 *  SPI2 Pinout:
 *    PB13 = SCK  (AF Push-Pull)
 *    PB14 = MISO (Input Floating)
 *    PB15 = MOSI (AF Push-Pull)
 *    PB12 = CS   (GPIO Output Push-Pull)
 */

#include "w25q64.h"
#include "w25q64_assets.h"
#include "timer.h"
#include "uart.h"
#include "spi1_lcd_st7789.h"
#include <string.h>

/* ============================================================
 *  ĐỊNH NGHĨA NỘI BỘ
 * ============================================================ */

/* --- Clock Enable --- */
#define RCC_GPIOBEN         (1U << 3)
#define RCC_SPI2EN          (1U << 14)   /* SPI2 trên bus APB1 */

/* --- SPI CR1 Bits --- */
#define SPI_CR1_CPHA        (1U << 0)
#define SPI_CR1_CPOL        (1U << 1)
#define SPI_CR1_MSTR        (1U << 2)
#define SPI_CR1_BR_DIV4     (1U << 3)    /* BR[2:0] = 001 => fPCLK/4 = 9MHz */
#define SPI_CR1_SPE         (1U << 6)
#define SPI_CR1_LSBFIRST    (1U << 7)
#define SPI_CR1_SSI         (1U << 8)
#define SPI_CR1_SSM         (1U << 9)
#define SPI_CR1_RXONLY      (1U << 10)
#define SPI_CR1_DFF         (1U << 11)

/* --- SPI SR Bits --- */
#define SPI_SR_RXNE         (1U << 0)
#define SPI_SR_TXE          (1U << 1)
#define SPI_SR_BSY          (1U << 7)

/* ============================================================
 *  BẢNG ĐỊA CHỈ ICON THỜI TIẾT 32x32
 * ============================================================ */
const uint32_t WEATHER_ICON_ADDR_TABLE[WEATHER_ICON_MAX_COUNT] = {
    FLASH_ADDR_ICON_WX_SUNNY,   /* Icon 0  - Sunny / Clear Sky */
    FLASH_ADDR_ICON_WX_CLOUDY,  /* Icon 1  - Few Clouds        */
    FLASH_ADDR_ICON_WX_RAIN,    /* Icon 2  - Rain              */
    FLASH_ADDR_ICON_WX_STORM,   /* Icon 3  - Thunderstorm      */
    FLASH_ADDR_ICON_WX_SNOW,    /* Icon 4  - Snow              */
    FLASH_ADDR_ICON_WX_MIST,    /* Icon 5  - Mist / Fog        */
    0x010000,                   /* Icon 6  - Dự phòng          */
    0x011000,                   /* Icon 7  - Dự phòng          */
    0x012000,                   /* Icon 8  - Dự phòng          */
    0x013000,                   /* Icon 9  - Dự phòng          */
    0x014000,                   /* Icon 10 - Dự phòng          */
    0x015000,                   /* Icon 11 - Dự phòng          */
    0x016000,                   /* Icon 12 - Dự phòng          */
    0x017000,                   /* Icon 13 - Dự phòng          */
    0x018000,                   /* Icon 14 - Dự phòng          */
    0x019000,                   /* Icon 15 - Dự phòng          */
};

/* ============================================================
 *  HÀM SPI2 CƠ BẢN (POLLING)
 * ============================================================ */

/**
 * @brief  Gửi 1 byte qua SPI2 và nhận 1 byte trả về (Full-duplex)
 * @param  tx_data: Byte cần gửi
 * @return Byte nhận được từ MISO
 */
static uint8_t SPI2_TransferByte(uint8_t tx_data)
{
    /* Đợi TXE (TX buffer trống) */
    while (!(SPI2->SR & SPI_SR_TXE)) {}

    /* Ghi data vào Data Register => phần cứng tự bắn ra MOSI */
    SPI2->DR = tx_data;

    /* Đợi RXNE (RX buffer có data) => đã nhận xong 1 byte từ MISO */
    while (!(SPI2->SR & SPI_SR_RXNE)) {}

    /* Đọc data nhận được (đồng thời xóa cờ RXNE) */
    return (uint8_t)SPI2->DR;
}

/**
 * @brief  Gửi nhiều byte liên tục (bỏ qua data nhận)
 */
static void SPI2_TransmitBytes(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        SPI2_TransferByte(data[i]);
    }
}

/**
 * @brief  Nhận nhiều byte (gửi 0xFF dummy để kích clock)
 */
static void SPI2_ReceiveBytes(uint8_t *buffer, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        buffer[i] = SPI2_TransferByte(0xFF);
    }
}

/* ============================================================
 *  KHỞI TẠO SPI2 + GPIO
 * ============================================================ */

void W25Q64_Init(void)
{
    /* ---- 1. Cấp xung clock ---- */
    RCC->APB2ENR |= RCC_GPIOBEN;          /* Clock cho GPIOB       */
    RCC->APB1ENR |= RCC_SPI2EN;           /* Clock cho SPI2 (APB1) */

    /* ---- 2. Cấu hình GPIO cho SPI2 ---- */
    /*
     * PB12 = CS   : Output Push-Pull, 50MHz    => 0x3
     * PB13 = SCK  : AF Output Push-Pull, 50MHz => 0xB
     * PB14 = MISO : Input Floating              => 0x4
     * PB15 = MOSI : AF Output Push-Pull, 50MHz => 0xB
     */
    GPIOB->CRH &= ~(0xFFFFU << 16);
    GPIOB->CRH |=  (0x3U << 16)   /* PB12: CS   */
                |  (0xBU << 20)   /* PB13: SCK  */
                |  (0x4U << 24)   /* PB14: MISO */
                |  (0xBU << 28);  /* PB15: MOSI */

    /* CS mặc định HIGH (không chọn chip) */
    W25Q64_CS_HIGH();

    /* ---- 3. Cấu hình SPI2 ---- */
    SPI2->CR1 = 0;
    SPI2->CR1 |= SPI_CR1_MSTR;        /* Master mode                         */
    SPI2->CR1 |= SPI_CR1_SSI;         /* Internal Slave Select = HIGH        */
    SPI2->CR1 |= SPI_CR1_SSM;         /* Software Slave Management           */
    SPI2->CR1 &= ~SPI_CR1_CPOL;       /* CPOL = 0 (Clock idle = LOW)         */
    SPI2->CR1 &= ~SPI_CR1_CPHA;       /* CPHA = 0 (Sample on rising edge)    */
    SPI2->CR1 &= ~SPI_CR1_DFF;        /* 8-bit data frame                    */
    SPI2->CR1 &= ~SPI_CR1_LSBFIRST;   /* MSB first                           */
    SPI2->CR1 |= SPI_CR1_BR_DIV4;     /* fPCLK1 / 4 = 36MHz / 4 = 9MHz       */

    /* Bật SPI2 */
    SPI2->CR1 |= SPI_CR1_SPE;

    /* Đọc bỏ data rác */
    (void)SPI2->DR;
    (void)SPI2->SR;

    /* Đánh thức chip nếu đang ở chế độ ngủ */
    W25Q64_WakeUp();

    /* Delay ngắn ổn định */
    volatile uint32_t cnt = 1000;
    while (cnt--) {}
}

/* ============================================================
 *  LỆNH TRẠNG THÁI & ĐIỀU KHIỂN
 * ============================================================ */

void W25Q64_WriteEnable(void)
{
    W25Q64_CS_LOW();
    SPI2_TransferByte(W25Q64_CMD_WRITE_ENABLE);
    W25Q64_CS_HIGH();
}

void W25Q64_WriteDisable(void)
{
    W25Q64_CS_LOW();
    SPI2_TransferByte(W25Q64_CMD_WRITE_DISABLE);
    W25Q64_CS_HIGH();
}

uint8_t W25Q64_ReadStatusReg1(void)
{
    W25Q64_CS_LOW();
    SPI2_TransferByte(W25Q64_CMD_READ_STATUS_REG1);
    uint8_t status = SPI2_TransferByte(0xFF);
    W25Q64_CS_HIGH();
    return status;
}

void W25Q64_WaitBusy(void)
{
    while (W25Q64_ReadStatusReg1() & W25Q64_STATUS_BUSY) {}
}

void W25Q64_PowerDown(void)
{
    W25Q64_CS_LOW();
    SPI2_TransferByte(W25Q64_CMD_POWER_DOWN);
    W25Q64_CS_HIGH();
}

void W25Q64_WakeUp(void)
{
    W25Q64_CS_LOW();
    SPI2_TransferByte(W25Q64_CMD_RELEASE_POWER_DOWN);
    W25Q64_CS_HIGH();
}

/* ============================================================
 *  NHẬN DẠNG CHIP
 * ============================================================ */

uint32_t W25Q64_ReadJEDEC_ID(void)
{
    uint32_t id = 0;

    W25Q64_CS_LOW();
    SPI2_TransferByte(W25Q64_CMD_JEDEC_ID);
    id  = (uint32_t)SPI2_TransferByte(0xFF) << 16;  /* Manufacturer ID (0xEF) */
    id |= (uint32_t)SPI2_TransferByte(0xFF) << 8;   /* Memory Type     (0x40) */
    id |= (uint32_t)SPI2_TransferByte(0xFF);         /* Capacity        (0x17) */
    W25Q64_CS_HIGH();

    return id;
}

uint16_t W25Q64_ReadDeviceID(void)
{
    uint16_t id = 0;

    W25Q64_CS_LOW();
    SPI2_TransferByte(W25Q64_CMD_MFTR_DEVICE_ID);
    SPI2_TransferByte(0x00);
    SPI2_TransferByte(0x00);
    SPI2_TransferByte(0x00);
    id  = (uint16_t)SPI2_TransferByte(0xFF) << 8;
    id |= (uint16_t)SPI2_TransferByte(0xFF);
    W25Q64_CS_HIGH();

    return id;
}

/* ============================================================
 *  ĐỌC DỮ LIỆU
 * ============================================================ */

void W25Q64_ReadData(uint32_t address, uint8_t *buffer, uint32_t length)
{
    if (length == 0) return;

    W25Q64_WaitBusy();

    W25Q64_CS_LOW();
    SPI2_TransferByte(W25Q64_CMD_READ_DATA);
    SPI2_TransferByte((address >> 16) & 0xFF);
    SPI2_TransferByte((address >> 8)  & 0xFF);
    SPI2_TransferByte(address & 0xFF);

    SPI2_ReceiveBytes(buffer, length);
    W25Q64_CS_HIGH();
}

void W25Q64_FastRead(uint32_t address, uint8_t *buffer, uint32_t length)
{
    if (length == 0) return;

    W25Q64_WaitBusy();

    W25Q64_CS_LOW();
    SPI2_TransferByte(W25Q64_CMD_FAST_READ);
    SPI2_TransferByte((address >> 16) & 0xFF);
    SPI2_TransferByte((address >> 8)  & 0xFF);
    SPI2_TransferByte(address & 0xFF);
    SPI2_TransferByte(0xFF); /* Dummy byte */

    SPI2_ReceiveBytes(buffer, length);
    W25Q64_CS_HIGH();
}

/* ============================================================
 *  GHI DỮ LIỆU
 * ============================================================ */

void W25Q64_WritePage(uint32_t address, const uint8_t *data, uint16_t length)
{
    if (length == 0 || length > W25Q64_PAGE_SIZE) return;

    W25Q64_WaitBusy();
    W25Q64_WriteEnable();

    W25Q64_CS_LOW();
    SPI2_TransferByte(W25Q64_CMD_PAGE_PROGRAM);
    SPI2_TransferByte((address >> 16) & 0xFF);
    SPI2_TransferByte((address >> 8)  & 0xFF);
    SPI2_TransferByte(address & 0xFF);

    SPI2_TransmitBytes(data, length);
    W25Q64_CS_HIGH();

    W25Q64_WaitBusy();
}

void W25Q64_WriteData(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0;

    while (offset < length) {
        uint16_t page_remain = W25Q64_PAGE_SIZE - (uint16_t)(address % W25Q64_PAGE_SIZE);
        uint16_t write_len = (length - offset > page_remain) ? page_remain : (uint16_t)(length - offset);

        W25Q64_WritePage(address, data + offset, write_len);

        address += write_len;
        offset  += write_len;
    }
}

/* ============================================================
 *  XÓA DỮ LIỆU
 * ============================================================ */

void W25Q64_EraseSector(uint32_t sector_address)
{
    W25Q64_WaitBusy();
    W25Q64_WriteEnable();

    W25Q64_CS_LOW();
    SPI2_TransferByte(W25Q64_CMD_SECTOR_ERASE_4K);
    SPI2_TransferByte((sector_address >> 16) & 0xFF);
    SPI2_TransferByte((sector_address >> 8)  & 0xFF);
    SPI2_TransferByte(sector_address & 0xFF);
    W25Q64_CS_HIGH();

    W25Q64_WaitBusy();
}

void W25Q64_EraseBlock32K(uint32_t block_address)
{
    W25Q64_WaitBusy();
    W25Q64_WriteEnable();

    W25Q64_CS_LOW();
    SPI2_TransferByte(W25Q64_CMD_BLOCK_ERASE_32K);
    SPI2_TransferByte((block_address >> 16) & 0xFF);
    SPI2_TransferByte((block_address >> 8)  & 0xFF);
    SPI2_TransferByte(block_address & 0xFF);
    W25Q64_CS_HIGH();

    W25Q64_WaitBusy();
}

void W25Q64_EraseBlock64K(uint32_t block_address)
{
    W25Q64_WaitBusy();
    W25Q64_WriteEnable();

    W25Q64_CS_LOW();
    SPI2_TransferByte(W25Q64_CMD_BLOCK_ERASE_64K);
    SPI2_TransferByte((block_address >> 16) & 0xFF);
    SPI2_TransferByte((block_address >> 8)  & 0xFF);
    SPI2_TransferByte(block_address & 0xFF);
    W25Q64_CS_HIGH();

    W25Q64_WaitBusy();
}

void W25Q64_EraseChip(void)
{
    W25Q64_WaitBusy();
    W25Q64_WriteEnable();

    W25Q64_CS_LOW();
    SPI2_TransferByte(W25Q64_CMD_CHIP_ERASE);
    W25Q64_CS_HIGH();

    W25Q64_WaitBusy();
}

/* ============================================================
 *  VẼ TRỰC TIẾP TỪ FLASH W25Q64 LÊN MÀN HÌNH ST7789
 * ============================================================ */

/**
 * @brief  Đọc trực tiếp dữ liệu ảnh từ W25Q64 và stream ra LCD ST7789
 *         Sử dụng bộ đệm 500 bytes (chỉ 250 pixel) => cực kỳ tiết kiệm RAM!
 */
void W25Q64_DrawAsset_ST7789(uint16_t x, uint16_t y, uint32_t flash_addr, uint16_t w, uint16_t h)
{
    uint32_t total_bytes = (uint32_t)w * h * 2;
    uint32_t offset = 0;
    uint8_t stream_buf[500];

    /* 1. Khoanh vùng hiển thị trên LCD */
    ST7789_SetWindow(x, y, x + w - 1, y + h - 1);

    /* 2. Chuẩn bị chân truyền dữ liệu sang LCD */
    ST7789_DC_DATA();
    ST7789_CS_SELECT();

    /* 3. Đọc từng chunk 500 bytes từ W25Q64 và bắn sang LCD qua SPI1 DMA */
    while (offset < total_bytes) {
        uint16_t chunk = (total_bytes - offset > sizeof(stream_buf)) ? sizeof(stream_buf) : (uint16_t)(total_bytes - offset);

        /* Đọc từ Flash W25Q64 (SPI2 Polling) */
        W25Q64_ReadData(flash_addr + offset, stream_buf, chunk);

        /* Bắn sang LCD (SPI1 DMA) */
        spi1_dma_transmit(stream_buf, chunk);
        spi1_dma_wait_complete();

        offset += chunk;
    }

    /* 4. Nhả CS LCD */
    ST7789_CS_DESELECT();
}

void W25Q64_DrawIcon50_ST7789(uint16_t x, uint16_t y, Asset_Icon_ID_t id)
{
    uint32_t addr;
    uint16_t w = 50, h = 50;

    switch (id) {
    case ASSET_ID_THOI_TIET: addr = FLASH_ADDR_ICON_THOI_TIET; break;
    case ASSET_ID_NHIET_DO:  addr = FLASH_ADDR_ICON_NHIET_DO;  break;
    case ASSET_ID_DO_AM:     addr = FLASH_ADDR_ICON_DO_AM;     break;
    case ASSET_ID_SO_NGUOI:  addr = FLASH_ADDR_ICON_SO_NGUOI;  break;
    case ASSET_ID_LOGO:      addr = FLASH_ADDR_ICON_LOGO; w = 20; h = 20; break;
    default: return;
    }

    W25Q64_DrawAsset_ST7789(x, y, addr, w, h);
}

void W25Q64_ReadWeatherIcon(uint8_t icon_id, uint8_t *buffer)
{
    if (icon_id >= WEATHER_ICON_MAX_COUNT) return;
    uint32_t addr = WEATHER_ICON_ADDR_TABLE[icon_id];
    W25Q64_ReadData(addr, buffer, WEATHER_ICON_SIZE);
}

/* ============================================================
 *  NẠP ẢNH / FONT QUA UART THEO TỪNG CHUNK (FLASH LOADER)
 * ============================================================
 *
 * Giao thức Chunk 128 bytes (Stop-and-Wait ARQ):
 *
 *  [SYNC: 0x55 0xAA (2B)]
 *  [CMD:  1B] (0xAA: Ping, 0xA1: Erase Sector, 0xA2: Write Chunk, 0xA3: Read Chunk)
 *  [ADDR: 4B] (Big-endian)
 *  [LEN:  2B] (Big-endian, tối đa 128 bytes)
 *  [DATA: LEN bytes]
 *  [CHECKSUM: 1B] (XOR của CMD + 4 bytes ADDR + 2 bytes LEN + DATA bytes)
 *
 *  Phản hồi:
 *  0x4F ('O'): ACK thành công
 *  0x45 ('E'): NACK lỗi
 */
void W25Q64_UART_FlashLoader_Process(void)
{
    /* Kiểm tra tối thiểu 9 bytes: SYNC(2) + CMD(1) + ADDR(4) + LEN(2) */
    if (UART1_Get_Available_Bytes() < 9) return;

    /* Đồng bộ: tìm chuỗi 0x55, 0xAA */
    bool sync_found = false;
    while (UART1_Get_Available_Bytes() >= 2) {
        uint8_t s1 = UART1_Read_Raw_Byte();
        if (s1 == FLASH_LOADER_SYNC1) {
            uint8_t s2 = UART1_Read_Raw_Byte();
            if (s2 == FLASH_LOADER_SYNC2) {
                sync_found = true;
                break;
            }
        }
    }

    if (!sync_found) return;

    /* Đợi đủ Header: CMD(1) + ADDR(4) + LEN(2) = 7 bytes */
    uint32_t timeout = 50000;
    while (UART1_Get_Available_Bytes() < 7 && --timeout) {}
    if (timeout == 0) return;

    uint8_t cmd = UART1_Read_Raw_Byte();
    uint32_t addr = ((uint32_t)UART1_Read_Raw_Byte() << 24)
                  | ((uint32_t)UART1_Read_Raw_Byte() << 16)
                  | ((uint32_t)UART1_Read_Raw_Byte() << 8)
                  | ((uint32_t)UART1_Read_Raw_Byte());

    uint16_t len = ((uint16_t)UART1_Read_Raw_Byte() << 8)
                 | ((uint16_t)UART1_Read_Raw_Byte());

    /* Đợi đủ DATA + CHECKSUM(1) */
    timeout = 100000;
    while (UART1_Get_Available_Bytes() < (uint16_t)(len + 1) && --timeout) {}
    if (timeout == 0) return;

    /* Đọc DATA và tính Checksum */
    uint8_t rx_data[256];
    uint8_t calc_csum = cmd
                      ^ ((addr >> 24) & 0xFF)
                      ^ ((addr >> 16) & 0xFF)
                      ^ ((addr >> 8) & 0xFF)
                      ^ (addr & 0xFF)
                      ^ ((len >> 8) & 0xFF)
                      ^ (len & 0xFF);

    for (uint16_t i = 0; i < len; i++) {
        rx_data[i] = UART1_Read_Raw_Byte();
        calc_csum ^= rx_data[i];
    }

    uint8_t recv_csum = UART1_Read_Raw_Byte();

    uint8_t resp = FLASH_LOADER_RESP_OK;

    if (calc_csum != recv_csum) {
        resp = FLASH_LOADER_RESP_ERR;
    } else {
        switch (cmd) {
        case FLASH_LOADER_CMD_HANDSHAKE:
            resp = FLASH_LOADER_RESP_OK;
            break;

        case FLASH_LOADER_CMD_ERASE:
            W25Q64_EraseSector(addr);
            resp = FLASH_LOADER_RESP_OK;
            break;

        case FLASH_LOADER_CMD_WRITE:
            W25Q64_WriteData(addr, rx_data, len);
            resp = FLASH_LOADER_RESP_OK;
            break;

        case FLASH_LOADER_CMD_READ:
            W25Q64_ReadData(addr, rx_data, len);
            UART1_DMA_Transmit(rx_data, len);
            resp = FLASH_LOADER_RESP_OK;
            break;

        default:
            resp = FLASH_LOADER_RESP_ERR;
            break;
        }
    }

    /* Gửi phản hồi ACK/NACK */
    UART1_DMA_Transmit(&resp, 1);
}
