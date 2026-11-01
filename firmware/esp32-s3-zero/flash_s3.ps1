# Quick Flasher for ESP32-S3-Zero Acoustic Node
$ports = [System.IO.Ports.SerialPort]::GetPortNames()
if ($ports.Count -eq 0) {
    Write-Host "No COM port detected. Please hold BOOT, replug USB, and release BOOT to enter download mode." -ForegroundColor Yellow
    exit 1
}
$port = $ports[0]
Write-Host "Found serial port: $port" -ForegroundColor Green
Set-Location -Path "C:\Users\dell\xiaozhi_dual_architecture\firmware\esp32-s3-zero\build"
python -m esptool --chip esp32s3 -p $port -b 460800 --before default-reset --after hard-reset write-flash "@flash_args"
