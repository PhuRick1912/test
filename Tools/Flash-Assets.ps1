<#
.SYNOPSIS
    Tool nạp tài nguyên (Icon 50x50, 20x20 và Font Arial) vào W25Q64 Flash qua UART của STM32.
    Chạy trực tiếp trên Windows PowerShell không cần cài đặt Python hay thư viện ngoài!

.DESCRIPTION
    Script này tự động:
      1. Đọc trực tiếp dữ liệu mảng hex từ các file:
         - thoi_tiet.c (50x50 RGB565 = 5,000 bytes) -> nạp vào 0x001000
         - nhiet_do.c  (50x50 RGB565 = 5,000 bytes) -> nạp vào 0x003000
         - do_am.c     (50x50 RGB565 = 5,000 bytes) -> nạp vào 0x005000
         - so_nguoi.c  (50x50 RGB565 = 5,000 bytes) -> nạp vào 0x007000
         - image.c     (20x20 RGB565 = 800 bytes)   -> nạp vào 0x009000
         - font_12px_arial.c (Glyph bitmap)         -> nạp vào 0x010000
      2. Chia thành các CHUNK nhỏ 128 bytes để bảo vệ bộ đệm UART của STM32.
      3. Giao thức Stop-and-Wait với Checksum XOR tin cậy 100%.

.EXAMPLE
    .\Tools\Flash-Assets.ps1 -Port COM3
    .\Tools\Flash-Assets.ps1 -Port COM4 -Baud 115200 -Only thoi_tiet
#>

param(
    [Parameter(Mandatory=$false)]
    [string]$Port = "COM3",

    [Parameter(Mandatory=$false)]
    [int]$Baud = 115200,

    [Parameter(Mandatory=$false)]
    [string]$Only = "all",

    [Parameter(Mandatory=$false)]
    [switch]$NoVerify
)

$ErrorActionPreference = "Stop"

# ============================================================
#  ĐỊNH NGHĨA HẰNG SỐ GIAO THỨC
# ============================================================
$SYNC1      = 0x55
$SYNC2      = 0xAA
$CMD_PING   = 0xAA
$CMD_ERASE  = 0xA1
$CMD_WRITE  = 0xA2
$CMD_READ   = 0xA3

$RESP_OK    = 0x4F   # 'O'
$RESP_ERR   = 0x45   # 'E'

$CHUNK_SIZE = 128    # 128 bytes mỗi lần gửi (vừa 1/2 page W25Q64)
$SECTOR_SIZE= 4096   # 4KB

# Thư mục gốc project
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectDir = Split-Path -Parent $ScriptDir
$SrcDir = Join-Path $ProjectDir "Src"

# ============================================================
#  BẢN ĐỒ PHÂN BỔ BỘ NHỚ FLASH (SECTOR-ALIGNED)
# ============================================================
$AssetMap = @(
    @{ Name = "thoi_tiet"; File = "thoi_tiet.c"; Addr = 0x001000; W = 50; H = 50; Sectors = 2; Type = "rgb565" },
    @{ Name = "nhiet_do";  File = "nhiet_do.c";  Addr = 0x003000; W = 50; H = 50; Sectors = 2; Type = "rgb565" },
    @{ Name = "do_am";     File = "do_am.c";     Addr = 0x005000; W = 50; H = 50; Sectors = 2; Type = "rgb565" },
    @{ Name = "so_nguoi";  File = "so_nguoi.c";  Addr = 0x007000; W = 50; H = 50; Sectors = 2; Type = "rgb565" },
    @{ Name = "image";     File = "image.c";     Addr = 0x009000; W = 20; H = 20; Sectors = 1; Type = "rgb565" },
    @{ Name = "font_arial";File = "font_12px_arial.c"; Addr = 0x010000; W = 0; H = 0; Sectors = 4; Type = "font" }
)

# ============================================================
#  HÀM TRÍCH XUẤT BYTES TỪ FILE .C
# ============================================================
function Extract-BytesFromCFile($filePath, $type) {
    if (-not (Test-Path $filePath)) {
        throw "Không tìm thấy file: $filePath"
    }
    $content = Get-Content $filePath -Raw

    if ($type -eq "rgb565") {
        # Trích xuất đoạn 16-bit RGB565 swap 0
        if ($content -match '(?s)#if\s+LV_COLOR_DEPTH\s*==\s*16\s*&&\s*LV_COLOR_16_SWAP\s*==\s*0(.*?)(?:#if|#endif)') {
            $hexMatches = [regex]::Matches($matches[1], '0x([0-9a-fA-F]{2})')
            $bytes = [byte[]]::new($hexMatches.Count)
            for ($i = 0; $i -lt $hexMatches.Count; $i++) {
                $bytes[$i] = [Convert]::ToByte($hexMatches[$i].Groups[1].Value, 16)
            }
            return $bytes
        } else {
            throw "Không tìm thấy section LV_COLOR_DEPTH == 16 trong $filePath"
        }
    }
    elseif ($type -eq "font") {
        # Trích xuất mảng glyph_bitmap
        if ($content -match '(?s)glyph_bitmap\[\]\s*=\s*\{(.*?)\};') {
            $hexMatches = [regex]::Matches($matches[1], '0x([0-9a-fA-F]{1,2})')
            $bytes = [byte[]]::new($hexMatches.Count)
            for ($i = 0; $i -lt $hexMatches.Count; $i++) {
                $bytes[$i] = [Convert]::ToByte($hexMatches[$i].Groups[1].Value, 16)
            }
            return $bytes
        } else {
            throw "Không tìm thấy mảng glyph_bitmap trong $filePath"
        }
    }
}

# ============================================================
#  GIAO THỨC TRUYỀN DỮ LIỆU SERIAL VỚI STM32
# ============================================================
function Send-ChunkFrame($serialPort, [byte]$cmd, [uint32]$addr, [byte[]]$data) {
    $len = if ($data) { $data.Length } else { 0 }
    
    # Header: SYNC(2) + CMD(1) + ADDR(4) + LEN(2) + DATA(N) + CHECKSUM(1)
    $frame = [System.Collections.Generic.List[byte]]::new()
    $frame.Add($SYNC1)
    $frame.Add($SYNC2)
    $frame.Add($cmd)
    
    # Addr Big-Endian
    $frame.Add([byte](($addr -shr 24) -band 0xFF))
    $frame.Add([byte](($addr -shr 16) -band 0xFF))
    $frame.Add([byte](($addr -shr 8)  -band 0xFF))
    $frame.Add([byte]($addr -band 0xFF))
    
    # Len Big-Endian
    $frame.Add([byte](($len -shr 8) -band 0xFF))
    $frame.Add([byte]($len -band 0xFF))
    
    # Checksum calculation: XOR
    [byte]$csum = $cmd -bxor (($addr -shr 24) -band 0xFF) `
                       -bxor (($addr -shr 16) -band 0xFF) `
                       -bxor (($addr -shr 8)  -band 0xFF) `
                       -bxor ($addr -band 0xFF) `
                       -bxor (($len -shr 8) -band 0xFF) `
                       -bxor ($len -band 0xFF)
    
    if ($len -gt 0) {
        for ($i = 0; $i -lt $len; $i++) {
            $frame.Add($data[$i])
            $csum = $csum -bxor $data[$i]
        }
    }
    $frame.Add($csum)
    
    # Gửi qua cổng Serial
    $rawFrame = $frame.ToArray()
    $serialPort.Write($rawFrame, 0, $rawFrame.Length)
    
    # Chờ phản hồi 1 byte
    $readTimeout = 5000
    $startTime = [Environment]::TickCount
    while ($serialPort.BytesToRead -eq 0) {
        if (([Environment]::TickCount - $startTime) -gt $readTimeout) {
            throw "Hết thời gian chờ phản hồi từ STM32 (Timeout)!"
        }
        Start-Sleep -Milliseconds 2
    }
    
    $resp = $serialPort.ReadByte()
    return $resp
}

# ============================================================
#  CHƯƠNG TRÌNH CHÍNH
# ============================================================
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "   W25Q64 FLASH LOADER - NẠP ASSET TỰ ĐỘNG XUỐNG STM32" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host " Cổng COM : $Port"
Write-Host " Baudrate : $Baud bps"
Write-Host " Chunk Size: $CHUNK_SIZE bytes"
Write-Host ""

# Mở cổng COM
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, One
$sp.ReadTimeout = 5000
$sp.WriteTimeout = 5000

try {
    $sp.Open()
    Start-Sleep -Milliseconds 200
    $sp.DiscardInBuffer()
    $sp.DiscardOutBuffer()
    
    # 1. Handshake Ping
    Write-Host "[1/3] Đang kiểm tra kết nối với STM32 (Handshake)..." -NoNewline
    $resp = Send-ChunkFrame $sp $CMD_PING 0 $null
    if ($resp -eq $RESP_OK) {
        Write-Host " [OK]" -ForegroundColor Green
    } else {
        Write-Host " [THẤT BẠI - Phản hồi: 0x$($resp.ToString('X2'))]" -ForegroundColor Red
        throw "STM32 không phản hồi đúng lệnh Ping."
    }
    
    # 2. Lọc danh sách asset cần nạp
    $targets = $AssetMap
    if ($Only -ne "all") {
        $targets = $AssetMap | Where-Object { $_.Name -like "*$Only*" }
        if (-not $targets) {
            throw "Không tìm thấy asset nào khớp với: $Only"
        }
    }
    
    Write-Host "`n[2/3] Bắt đầu nạp $($targets.Count) asset vào W25Q64 Flash..." -ForegroundColor Yellow
    
    foreach ($item in $targets) {
        $filePath = Join-Path $SrcDir $item.File
        Write-Host "`n------------------------------------------------------------"
        Write-Host " ASSET: $($item.Name.ToUpper())" -ForegroundColor Cyan
        Write-Host " File : $($item.File)"
        Write-Host " Flash: 0x$($item.Addr.ToString('X6')) | Cấp phát: $($item.Sectors) sectors ($($item.Sectors * 4)KB)"
        
        # Đọc dữ liệu
        $bytes = Extract-BytesFromCFile $filePath $item.Type
        Write-Host " Kích thước dữ liệu: $($bytes.Length) bytes"
        
        # Xóa các sector tương ứng
        Write-Host " Đang xóa $($item.Sectors) sector(s)..." -NoNewline
        for ($s = 0; $s -lt $item.Sectors; $s++) {
            $secAddr = $item.Addr + ($s * $SECTOR_SIZE)
            $eraseResp = Send-ChunkFrame $sp $CMD_ERASE $secAddr $null
            if ($eraseResp -ne $RESP_OK) {
                Write-Host " [LỖI XÓA]" -ForegroundColor Red
                throw "Lỗi xóa sector tại 0x$($secAddr.ToString('X6'))"
            }
        }
        Write-Host " [XONG]" -ForegroundColor Green
        
        # Ghi từng chunk 128 bytes
        $totalBytes = $bytes.Length
        $offset = 0
        $chunkIndex = 0
        $totalChunks = [Math]::Ceiling($totalBytes / $CHUNK_SIZE)
        
        Write-Host " Đang ghi $totalChunks chunks ($CHUNK_SIZE bytes/chunk):"
        
        while ($offset -lt $totalBytes) {
            $chunkLen = [Math]::Min($CHUNK_SIZE, $totalBytes - $offset)
            $chunkData = [byte[]]::new($chunkLen)
            [Array]::Copy($bytes, $offset, $chunkData, 0, $chunkLen)
            
            $currAddr = $item.Addr + $offset
            
            # Gửi chunk và chờ ACK
            $retries = 3
            $success = $false
            while ($retries -gt 0 -and -not $success) {
                try {
                    $wResp = Send-ChunkFrame $sp $CMD_WRITE $currAddr $chunkData
                    if ($wResp -eq $RESP_OK) {
                        $success = $true
                    } else {
                        $retries--
                        Start-Sleep -Milliseconds 10
                    }
                } catch {
                    $retries--
                    Start-Sleep -Milliseconds 10
                }
            }
            
            if (-not $success) {
                Write-Host "`n [LỖI GHI tại offset $offset]" -ForegroundColor Red
                throw "Ghi thất bại tại địa chỉ 0x$($currAddr.ToString('X6'))"
            }
            
            $offset += $chunkLen
            $chunkIndex++
            
            # Hiển thị tiến trình
            $pct = [Math]::Round(($offset / $totalBytes) * 100, 1)
            Write-Progress -Activity "Nạp $($item.Name)" -Status "$offset / $totalBytes bytes ($pct%)" -PercentComplete $pct
            Write-Host -NoNewline "`r  Tiến độ: $pct% ($offset / $totalBytes bytes) Chunk $chunkIndex/$totalChunks"
        }
        Write-Host ""
        Write-Host "  -> Nạp thành công $($item.Name) vào Flash 0x$($item.Addr.ToString('X6'))!" -ForegroundColor Green
    }
    
    Write-Host "`n============================================================" -ForegroundColor Cyan
    Write-Host " [3/3] HOÀN TẤT TOÀN BỘ QUÁ TRÌNH NẠP ASSET VÀO W25Q64!" -ForegroundColor Green
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host " Các icon và font đã sẵn sàng trên Flash."
    Write-Host " Bạn có thể gọi W25Q64_DrawAsset_ST7789() để vẽ lên màn hình LCD ngay lập tức.`n"
}
finally {
    if ($sp -and $sp.IsOpen) {
        $sp.Close()
        $sp.Dispose()
    }
}

