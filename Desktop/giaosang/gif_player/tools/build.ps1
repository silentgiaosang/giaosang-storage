# ESP-IDF Build Script
# Source the ESP-IDF environment and build

$env:IDF_PATH = "D:\Espressif\frameworks\esp-idf-v5.3.1"
& "$env:IDF_PATH\export.ps1" | Out-Null

Set-Location "C:\Users\giao\Desktop\giaosang\gif_player"
Write-Host "Setting target to esp32s3..."
idf.py set-target esp32s3
if ($LASTEXITCODE -ne 0) {
    Write-Error "set-target failed"
    exit $LASTEXITCODE
}

Write-Host "Building..."
idf.py build
if ($LASTEXITCODE -ne 0) {
    Write-Error "build failed"
    exit $LASTEXITCODE
}

Write-Host "Build successful!"
