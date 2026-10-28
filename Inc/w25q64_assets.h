/**
 * @file w25q64_assets.h
 * @brief Bản đồ phân vùng bộ nhớ và định nghĩa địa chỉ Asset trên W25Q64 Flash (8MB).
 *
 * QUY TẮC PHÂN BỔ BỘ NHỚ (SECTOR-ALIGNED):
 *   - W25Q64 có 2048 Sectors (mỗi Sector = 4,096 bytes = 4KB).
 *   - Các asset luôn bắt đầu ở ranh giới Sector 4KB (địa chỉ chia hết cho 0x1000).
 *   - Icon 50x50 RGB565 = 5,000 bytes => cấp phát 2 Sectors (8KB = 0x2000).
 *   - Icon 20x20 RGB565 = 800 bytes   => cấp phát 1 Sector  (4KB = 0x1000).
 *   - Icon 32x32 RGB565 = 2,048 bytes => cấp phát 1 Sector  (4KB = 0x1000).
 *   - Font Arial 12px   = ~10.5 KB    => cấp phát 4 Sectors (16KB = 0x4000).
 *
 * LỢI ÍCH CỦA PHÂN BỔ THEO SECTOR:
 *   1. Cập nhật độc lập: Muốn thay đổi bất kỳ icon nào, chỉ cần xóa sector của nó,
 *      không làm mất hay ảnh hưởng đến bất kỳ asset nào khác.
 *   2. Địa chỉ hằng số cố định: Truy xuất siêu nhanh, không cần tính toán runtime.
 *   3. Phân nhóm rõ ràng, dễ nhớ, dễ mở rộng thêm hàng trăm icon khác.
 */

#ifndef W25Q64_ASSETS_H_
#define W25Q64_ASSETS_H_

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 *  BẢN ĐỒ ĐỊA CHỈ FLASH (FLASH MEMORY MAP)
 * ============================================================ */

/* --- Sector 0: Danh mục Metadata & Header hệ thống (4KB) --- */
#define FLASH_ADDR_ASSET_TABLE          0x000000U   /* Sector 0: Bảng tra cứu Asset Directory */

/* --- Nhóm 1: Icon Giao diện Chính 50x50 RGB565 (Mỗi icon cấp 8KB = 2 Sectors) --- */
#define FLASH_ADDR_ICON_THOI_TIET       0x001000U   /* Sector 1-2: Icon Thời tiết (50x50, 5000B) */
#define FLASH_ADDR_ICON_NHIET_DO        0x003000U   /* Sector 3-4: Icon Nhiệt độ  (50x50, 5000B) */
#define FLASH_ADDR_ICON_DO_AM           0x005000U   /* Sector 5-6: Icon Độ ẩm     (50x50, 5000B) */
#define FLASH_ADDR_ICON_SO_NGUOI        0x007000U   /* Sector 7-8: Icon Số người  (50x50, 5000B) */

/* --- Nhóm 2: Icon Phụ & Logo --- */
#define FLASH_ADDR_ICON_LOGO            0x009000U   /* Sector 9:   Logo / Test (20x20, 800B)     */

/* --- Nhóm 3: Icon Thời tiết chi tiết 32x32 (Mỗi icon cấp 4KB = 1 Sector) --- */
#define FLASH_ADDR_ICON_WX_BASE         0x00A000U   /* Bắt đầu vùng icon thời tiết 32x32         */
#define FLASH_ADDR_ICON_WX_SUNNY        0x00A000U   /* Sector 10: Trời nắng / Clear sky          */
#define FLASH_ADDR_ICON_WX_CLOUDY       0x00B000U   /* Sector 11: Có mây / Few clouds            */
#define FLASH_ADDR_ICON_WX_RAIN         0x00C000U   /* Sector 12: Mưa rào / Rain                 */
#define FLASH_ADDR_ICON_WX_STORM        0x00D000U   /* Sector 13: Sấm sét / Thunderstorm         */
#define FLASH_ADDR_ICON_WX_SNOW         0x00E000U   /* Sector 14: Tuyết / Snow                   */
#define FLASH_ADDR_ICON_WX_MIST         0x00F000U   /* Sector 15: Sương mù / Mist                */

/* --- Nhóm 4: Bộ Font chữ Tiếng Việt (Cấp 64KB Block 1) --- */
#define FLASH_ADDR_FONT_ARIAL_12        0x010000U   /* Block 1: Bộ font Arial 12px (~10.5 KB)    */

/* --- Nhóm 5: Vùng trống mở rộng (Từ 0x020000 đến 0x7FFFFF = ~7.8 MB) --- */
#define FLASH_ADDR_USER_DATA_START      0x020000U

/* ============================================================
 *  THÔNG SỐ KÍCH THƯỚC CÁC ASSET
 * ============================================================ */
#define ICON_50X50_WIDTH                50
#define ICON_50X50_HEIGHT               50
#define ICON_50X50_BYTES                (50 * 50 * 2)   /* 5,000 bytes RGB565 */
#define ICON_50X50_ALLOC_SIZE           0x2000          /* 8KB = 2 sectors    */

#define ICON_20X20_WIDTH                20
#define ICON_20X20_HEIGHT               20
#define ICON_20X20_BYTES                (20 * 20 * 2)   /* 800 bytes RGB565   */

#define ICON_32X32_WIDTH                32
#define ICON_32X32_HEIGHT               32
#define ICON_32X32_BYTES                (32 * 32 * 2)   /* 2,048 bytes RGB565 */

/* ============================================================
 *  ENUM ĐỊNH DANH ASSET (ASSET ID)
 * ============================================================ */
typedef enum {
    ASSET_ID_THOI_TIET = 0,     /* 50x50 */
    ASSET_ID_NHIET_DO  = 1,     /* 50x50 */
    ASSET_ID_DO_AM     = 2,     /* 50x50 */
    ASSET_ID_SO_NGUOI  = 3,     /* 50x50 */
    ASSET_ID_LOGO      = 4,     /* 20x20 */
    ASSET_ID_COUNT     = 5
} Asset_Icon_ID_t;

/* ============================================================
 *  CẤU TRÚC DIRECTORY ASSET (LƯU TẠI SECTOR 0 ĐỂ TRA CỨU)
 * ============================================================ */
#define ASSET_DIRECTORY_MAGIC           0x57323551U     /* "W25Q" */

typedef struct __attribute__((packed)) {
    uint32_t flash_addr;        /* Địa chỉ Flash bắt đầu                         */
    uint32_t size_bytes;        /* Kích thước dữ liệu thật                       */
    uint16_t width;             /* Chiều rộng (pixel)                            */
    uint16_t height;            /* Chiều cao (pixel)                             */
    uint8_t  format;            /* 0: RGB565, 1: 4bpp font, 2: raw               */
    uint8_t  reserved[3];       /* Căn chỉnh 16 bytes                            */
} Asset_Entry_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;             /* 0x57323551                                    */
    uint16_t version;           /* Phiên bản định dạng (ví dụ 0x0100 = v1.0)     */
    uint16_t asset_count;       /* Tổng số asset trong danh mục                  */
    Asset_Entry_t entries[16];  /* Danh mục tối đa 16 asset cơ bản              */
} Asset_Directory_t;

/* ============================================================
 *  API HIỂN THỊ TRỰC TIẾP TỪ FLASH W25Q64 LÊN MÀN HÌNH ST7789
 * ============================================================ */

/**
 * @brief  Đọc trực tiếp dữ liệu ảnh từ W25Q64 và vẽ lên màn hình ST7789
 *         Sử dụng cơ chế Streaming qua bộ đệm nhỏ (500 bytes)
 *         => Cực kỳ tiết kiệm RAM (không cần cấp phát 5,000 bytes trong RAM)!
 *
 * @param  x: Tọa độ X góc trên bên trái màn hình
 * @param  y: Tọa độ Y góc trên bên trái màn hình
 * @param  flash_addr: Địa chỉ ảnh trong W25Q64 (ví dụ FLASH_ADDR_ICON_NHIET_DO)
 * @param  w: Chiều rộng ảnh (pixel)
 * @param  h: Chiều cao ảnh (pixel)
 */
void W25Q64_DrawAsset_ST7789(uint16_t x, uint16_t y, uint32_t flash_addr, uint16_t w, uint16_t h);

/**
 * @brief  Hàm tiện ích vẽ nhanh icon 50x50 theo ID
 * @param  x, y: Tọa độ trên LCD
 * @param  id: ASSET_ID_THOI_TIET, ASSET_ID_NHIET_DO, ASSET_ID_DO_AM, ASSET_ID_SO_NGUOI
 */
void W25Q64_DrawIcon50_ST7789(uint16_t x, uint16_t y, Asset_Icon_ID_t id);

#endif /* W25Q64_ASSETS_H_ */

