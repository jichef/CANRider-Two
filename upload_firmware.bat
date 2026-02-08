@echo off
set ESPTOOL="C:\Users\Administrador\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe"
set PORT=COM4
set BIN_DIR=C:\Users\Administrador\AppData\Local\arduino\sketches\AB91F2139C81EC251C64E3A506C8886C
set BOOT_APP0="C:\Users\Administrador\AppData\Local\Arduino15\packages\esp32\hardware\esp32\2.0.14\tools\partitions\boot_app0.bin"

%ESPTOOL% --chip esp32 --port %PORT% --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x1000 %BIN_DIR%\main.ino.bootloader.bin 0x8000 %BIN_DIR%\main.ino.partitions.bin 0xe000 %BOOT_APP0% 0x10000 %BIN_DIR%\main.ino.bin

pause
