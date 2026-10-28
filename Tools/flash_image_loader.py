#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
flash_image_loader.py

Script nạp ảnh icon và font vào W25Q64 Flash qua UART của STM32 theo từng Chunk 128 bytes.

Hỗ trợ:
  1. Nạp trực tiếp từ file .c (thoi_tiet.c, nhiet_do.c, do_am.c, so_nguoi.c, image.c, font_12px_arial.c)
  2. Nạp từ file ảnh PNG/JPG/BMP (tự động resize và convert RGB565)
  3. Giao thức Stop-and-Wait với Frame Chunk 128 bytes và Checksum XOR.
  4. Phân bổ bộ nhớ theo Sector 4KB (Sector-Aligned) để truy xuất và cập nhật độc lập.

Cách sử dụng:
  # Nạp tất cả tài nguyên từ file .c vào Flash (Khuyên dùng):
  python flash_image_loader.py --port COM3 --flash-all-c-assets

  # Nạp 1 file .c cụ thể:
  python flash_image_loader.py --port COM3 --flash-c-file ../Src/nhiet_do.c --addr 0x003000

  # Nạp 1 ảnh PNG/JPG:
  python flash_image_loader.py --port COM3 --flash-image icon.png --addr 0x001000 --width 50 --height 50
"""

import argparse
import os
import sys
import struct
import time
import re
import glob

try:
    import serial
except ImportError:
    serial = None

try:
    from PIL import Image
except ImportError:
    Image = None

# ============================================================
#  CONSTANTS
# ============================================================
SYNC1         = 0x55
SYNC2         = 0xAA

CMD_PING      = 0xAA
CMD_ERASE     = 0xA1
CMD_WRITE     = 0xA2
CMD_READ      = 0xA3

RESP_OK       = 0x4F   # 'O'
RESP_ERR      = 0x45   # 'E'

CHUNK_SIZE    = 128    # 128 bytes mỗi lần gửi
SECTOR_SIZE   = 4096   # 4KB

# Bản đồ phân vùng bộ nhớ W25Q64
ASSET_MAP = [
    {"name": "thoi_tiet",  "file": "thoi_tiet.c",       "addr": 0x001000, "w": 50, "h": 50, "sectors": 2, "type": "rgb565"},
    {"name": "nhiet_do",   "file": "nhiet_do.c",        "addr": 0x003000, "w": 50, "h": 50, "sectors": 2, "type": "rgb565"},
    {"name": "do_am",      "file": "do_am.c",           "addr": 0x005000, "w": 50, "h": 50, "sectors": 2, "type": "rgb565"},
    {"name": "so_nguoi",   "file": "so_nguoi.c",        "addr": 0x007000, "w": 50, "h": 50, "sectors": 2, "type": "rgb565"},
    {"name": "image",      "file": "image.c",           "addr": 0x009000, "w": 20, "h": 20, "sectors": 1, "type": "rgb565"},
    {"name": "font_arial", "file": "font_12px_arial.c", "addr": 0x010000, "w": 0,  "h": 0,  "sectors": 4, "type": "font"}
]

# ============================================================
#  TRÍCH XUẤT DỮ LIỆU TỪ FILE .C
# ============================================================
def extract_bytes_from_c_file(file_path, asset_type):
    if not os.path.exists(file_path):
        raise FileNotFoundError(f"Không tìm thấy file: {file_path}")

    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    if asset_type == "rgb565":
        m = re.search(r'#if\s+LV_COLOR_DEPTH\s*==\s*16\s*&&\s*LV_COLOR_16_SWAP\s*==\s*0(.*?)(?:#if|#endif)', content, re.DOTALL)
        if m:
            hex_vals = re.findall(r'0x([0-9a-fA-F]{2})', m.group(1))
            return bytes([int(h, 16) for h in hex_vals])
        else:
            raise ValueError(f"Không tìm thấy section 16-bit RGB565 trong {file_path}")

    elif asset_type == "font":
        m = re.search(r'glyph_bitmap\[\]\s*=\s*\{(.*?)\};', content, re.DOTALL)
        if m:
            hex_vals = re.findall(r'0x([0-9a-fA-F]{1,2})', m.group(1))
            return bytes([int(h, 16) for h in hex_vals])
        else:
            raise ValueError(f"Không tìm thấy mảng glyph_bitmap trong {file_path}")

    raise ValueError(f"Loại asset không hợp lệ: {asset_type}")

# ============================================================
#  CONVERT ẢNH SANG RGB565
# ============================================================
def image_to_rgb565(image_path, width, height):
    if Image is None:
        raise ImportError("Cần cài đặt Pillow: pip install Pillow")

    img = Image.open(image_path).convert('RGB')
    img = img.resize((width, height), Image.Resampling.LANCZOS)
    pixels = list(img.getdata())
    data = bytearray()

    for r, g, b in pixels:
        r5 = (r >> 3) & 0x1F
        g6 = (g >> 2) & 0x3F
        b5 = (b >> 3) & 0x1F
        color = (r5 << 11) | (g6 << 5) | b5
        # Big-endian: High byte first
        data.append((color >> 8) & 0xFF)
        data.append(color & 0xFF)

    return bytes(data)

# ============================================================
#  GIAO TIẾP VỚI STM32
# ============================================================
class W25Q64Loader:
    def __init__(self, port, baud=115200, timeout=5):
        if serial is None:
            raise ImportError("Cần cài đặt pyserial: pip install pyserial")
        self.ser = serial.Serial(port, baud, timeout=timeout)
        time.sleep(0.2)
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send_frame(self, cmd, addr, data=b''):
        length = len(data) if data else 0
        frame = bytearray([SYNC1, SYNC2, cmd])
        frame.extend(struct.pack('>I', addr))
        frame.extend(struct.pack('>H', length))

        # Checksum XOR
        csum = cmd ^ ((addr >> 24) & 0xFF) ^ ((addr >> 16) & 0xFF) ^ ((addr >> 8) & 0xFF) ^ (addr & 0xFF) ^ ((length >> 8) & 0xFF) ^ (length & 0xFF)
        if data:
            for b in data:
                csum ^= b
            frame.extend(data)

        frame.append(csum)
        self.ser.write(frame)

        resp = self.ser.read(1)
        if len(resp) == 0:
            raise TimeoutError("Hết thời gian chờ phản hồi từ STM32!")
        return resp[0]

    def ping(self):
        resp = self.send_frame(CMD_PING, 0)
        return resp == RESP_OK

    def erase_sector(self, addr):
        resp = self.send_frame(CMD_ERASE, addr)
        if resp != RESP_OK:
            raise RuntimeError(f"Lỗi xóa sector tại 0x{addr:06X}")

    def flash_data(self, addr, data, num_sectors):
        # 1. Xóa các sectors
        print(f"  Đang xóa {num_sectors} sector(s)...", end='', flush=True)
        for s in range(num_sectors):
            sec_addr = addr + s * SECTOR_SIZE
            self.erase_sector(sec_addr)
        print(" [XONG]")

        # 2. Ghi từng chunk
        total = len(data)
        offset = 0
        chunk_idx = 0
        total_chunks = (total + CHUNK_SIZE - 1) // CHUNK_SIZE

        print(f"  Đang ghi {total} bytes ({total_chunks} chunks):")
        while offset < total:
            chunk_len = min(CHUNK_SIZE, total - offset)
            chunk = data[offset:offset + chunk_len]
            curr_addr = addr + offset

            retries = 3
            success = False
            while retries > 0 and not success:
                try:
                    resp = self.send_frame(CMD_WRITE, curr_addr, chunk)
                    if resp == RESP_OK:
                        success = True
                    else:
                        retries -= 1
                        time.sleep(0.01)
                except Exception:
                    retries -= 1
                    time.sleep(0.01)

            if not success:
                raise RuntimeError(f"Ghi thất bại tại 0x{curr_addr:06X}")

            offset += chunk_len
            chunk_idx += 1
            pct = (offset / total) * 100
            print(f"\r    Tiến độ: {pct:.1f}% ({offset}/{total} bytes) Chunk {chunk_idx}/{total_chunks}", end='', flush=True)

        print()
        print(f"  -> Ghi thành công vào 0x{addr:06X}!")

# ============================================================
#  CLI
# ============================================================
def main():
    parser = argparse.ArgumentParser(description="W25Q64 Flash Loader - Nạp tài nguyên xuống STM32")
    parser.add_argument('--port', default='COM3', help='Cổng COM (mặc định: COM3)')
    parser.add_argument('--baud', type=int, default=115200, help='Baudrate (mặc định: 115200)')
    parser.add_argument('--flash-all-c-assets', action='store_true', help='Nạp toàn bộ icon và font từ các file .c')
    parser.add_argument('--flash-c-file', help='Đường dẫn file .c cần nạp')
    parser.add_argument('--addr', default='0x001000', help='Địa chỉ Flash (hex)')
    parser.add_argument('--flash-image', help='Đường dẫn ảnh PNG/JPG')
    parser.add_argument('--width', type=int, default=50, help='Chiều rộng ảnh')
    parser.add_argument('--height', type=int, default=50, help='Chiều cao ảnh')
    args = parser.parse_args()

    loader = None
    try:
        print(f"\nKết nối STM32 tại {args.port} @ {args.baud} bps...")
        loader = W25Q64Loader(args.port, args.baud)

        if not loader.ping():
            print("LỖI: STM32 không phản hồi lệnh Ping! Kiểm tra lại kết nối UART.")
            return

        print("✓ Kết nối thành công với STM32!\n")

        if args.flash_all_c_assets:
            script_dir = os.path.dirname(os.path.abspath(__file__))
            src_dir = os.path.join(os.path.dirname(script_dir), "Src")

            print(f"=== BẮT ĐẦU NẠP TẤT CẢ ASSETS TỪ {src_dir} ===")
            for item in ASSET_MAP:
                fpath = os.path.join(src_dir, item["file"])
                print(f"\n--- ASSET: {item['name'].upper()} (0x{item['addr']:06X}) ---")
                raw_bytes = extract_bytes_from_c_file(fpath, item["type"])
                print(f"  Kích thước data: {len(raw_bytes)} bytes")
                loader.flash_data(item["addr"], raw_bytes, item["sectors"])

            print("\n============================================================")
            print("✓ ĐÃ NẠP TOÀN BỘ ASSET THÀNH CÔNG VÀO W25Q64 FLASH!")
            print("============================================================\n")

        elif args.flash_c_file:
            addr = int(args.addr, 16)
            fpath = args.flash_c_file
            atype = "font" if "font" in fpath.lower() else "rgb565"
            raw_bytes = extract_bytes_from_c_file(fpath, atype)
            sectors = (len(raw_bytes) + SECTOR_SIZE - 1) // SECTOR_SIZE
            print(f"Nạp file: {fpath} ({len(raw_bytes)} bytes) -> 0x{addr:06X}")
            loader.flash_data(addr, raw_bytes, sectors)

        elif args.flash_image:
            addr = int(args.addr, 16)
            data = image_to_rgb565(args.flash_image, args.width, args.height)
            sectors = (len(data) + SECTOR_SIZE - 1) // SECTOR_SIZE
            print(f"Nạp ảnh: {args.flash_image} ({len(data)} bytes) -> 0x{addr:06X}")
            loader.flash_data(addr, data, sectors)

    except Exception as e:
        print(f"\nLỖI: {e}")
    finally:
        if loader:
            loader.close()

if __name__ == '__main__':
    main()
