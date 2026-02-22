@echo off
set ESPTOOL="C:\Users\Administrador\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe"
set PORT=COM3
set SKETCH_DIR=C:\Users\Administrador\AppData\Local\arduino\sketches\E039086BBB90CCB3BF8F31564A6DD2ED
set BOOT_APP0="C:\Users\Administrador\AppData\Local\Arduino15\packages\esp32\hardware\esp32\2.0.14\tools\partitions\boot_app0.bin"

echo Flasheando AT.ino en %PORT%...

%ESPTOOL% --chip esp32 --port %PORT% --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x1000 "%SKETCH_DIR%\ATdebug.ino.bootloader.bin" 0x8000 "%SKETCH_DIR%\ATdebug.ino.partitions.bin" 0xe000 %BOOT_APP0% 0x10000 "%SKETCH_DIR%\ATdebug.ino.bin"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo [OK] Flasheo completado con exito.
) else (
    echo.
    echo [ERROR] Hubo un problema al flashear.
)

pause