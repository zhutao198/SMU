<#
.SYNOPSIS
    SMU firmware one-click build script (arm-none-eabi-gcc, no make needed).
.DESCRIPTION
    Compiles: Core/Src (app + main) + HAL + CMSIS + FreeRTOS + LwIP
              + tools/verify (ETH low level / IRQ / peripheral init / configs)
    Outputs : build/smu.elf / smu.bin / smu.hex

.PARAMETER Fw
    CubeFW root (contains Drivers/ and Middlewares/).
    Default: $env:TEMP\cube_fw

.PARAMETER Out
    Output directory, default: build/

.EXAMPLE
    .\tools\build.ps1
    .\tools\build.ps1 -Fw D:\STM32Cube\Repository\STM32Cube_FW_F4_V1.28.1
#>
param(
    [string]$Fw = "$env:TEMP\cube_fw",
    [string]$Out = "build"
)

$ErrorActionPreference = "Continue"   # gcc 的 warning 走 stderr，不能终止脚本
$Root = Split-Path -Parent $PSScriptRoot      # .../code/firmware
$Core = Join-Path $Root "Core"
$Tools = Join-Path $Root "tools"
$Verify = Join-Path $Tools "verify"

$GCC = "arm-none-eabi-gcc"
$OBJCOPY = "arm-none-eabi-objcopy"

if (-not (Test-Path (Join-Path $Fw "Drivers"))) {
    Write-Error "CubeFW not found: $Fw (use -Fw to specify a root containing Drivers/ and Middlewares/)"
    exit 1
}
$HAL   = Join-Path $Fw "Drivers\STM32F4xx_HAL_Driver"
$CMSIS = Join-Path $Fw "Drivers\CMSIS"
$FRT   = Join-Path $Fw "Middlewares\Third_Party\FreeRTOS\Source"
$LWIP  = Join-Path $Fw "Middlewares\Third_Party\LwIP\src"

$BuildDir = Join-Path $Root $Out
$ObjDir = Join-Path $BuildDir "obj"
New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null

$CPU = @("-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard")
$CFLAGS = @(
    $CPU, "-Os", "-g3", "-ffunction-sections", "-fdata-sections",
    "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-sign-compare",
    "-DSTM32F407xx", "-DUSE_HAL_DRIVER", "-DSMU_IWDG_ENABLE=0",
    "-std=gnu11"
)
# 注意：MinGW GCC 的 -I 不接受纯反斜杠路径，统一转成正斜杠
# lwip/arch.h 里是 #include "arch/cc.h"，故 -I 需指向 LwIP/system（其下有 arch/）
$LWIP_SYS = Join-Path $Fw "Middlewares\Third_Party\LwIP\system"
$INCLUDES = @(
    "$Core\Inc",
    "$Verify",
    "$HAL\Inc",
    "$CMSIS\Device\ST\STM32F4xx\Include",
    "$CMSIS\Include",
    "$FRT\include",
    "$FRT\portable\GCC\ARM_CM4F",
    "$FRT\CMSIS_RTOS",
    "$LWIP\include",
    "$LWIP\include\lwip",
    "$LWIP_SYS"
) | ForEach-Object { "-I" + $_.Replace('\', '/') }

$AppSrc = Get-ChildItem "$Core\Src\*.c" -File
# HAL：排除 CubeMX 默认不编译的模板文件
$HalSrc = Get-ChildItem "$HAL\Src\*.c" -File | Where-Object { $_.Name -notmatch 'template' }
$FrtSrc = @(
    "$FRT\tasks.c", "$FRT\queue.c", "$FRT\list.c", "$FRT\timers.c",
    "$FRT\event_groups.c", "$FRT\stream_buffer.c",
    "$FRT\portable\GCC\ARM_CM4F\port.c",
    "$FRT\portable\MemMang\heap_4.c"
)
# LwIP: core + api + netif + ipv4 (static IP, no dhcp/autoip/dns/ppp/slip)
$LwipSrc = Get-ChildItem "$LWIP\core\*.c" -File |
           Where-Object { $_.Name -ne "sys_arch.c" } |
           ForEach-Object { $_.FullName }
$LwipSrc = @($LwipSrc) | Where-Object { $_ -ne $null }
$LwipSrc += Get-ChildItem "$LWIP\api\*.c" -File | ForEach-Object { $_.FullName }
$LwipSrc += Get-ChildItem "$LWIP\netif\*.c" -File |
            Where-Object { $_.Name -notmatch 'ppp|slipif' } |
            ForEach-Object { $_.FullName }
$LwipSrc += Get-ChildItem "$LWIP\core\ipv4\*.c" -File | ForEach-Object { $_.FullName }
$LwipSysArch = Join-Path $Verify "sys_arch.c"     # 自写的 FreeRTOS 原生 sys_arch（已在 Verify glob 中，勿重复添加）
$LwipSrc = @($LwipSrc) | Where-Object { $_ -ne $LwipSysArch }

$VerifySrc = Get-ChildItem "$Verify\*.c" -File
# CMSIS system 文件（SystemCoreClock / AHBPrescTable / APBPrescTable）
$CmsisSrc = Get-ChildItem "$CMSIS\Device\ST\STM32F4xx\Source\Templates\system_stm32f4xx.c" -File
$Startup = "$CMSIS\Device\ST\STM32F4xx\Source\Templates\gcc\startup_stm32f407xx.s"
$Ld = Join-Path $Verify "STM32F407VETx_FLASH.ld"

$AllC = @($AppSrc.FullName) + @($VerifySrc.FullName) + @($CmsisSrc.FullName) + @($HalSrc.FullName) + @($FrtSrc) + @($LwipSrc)
Write-Host ("sources: App {0} / Verify {1} / CMSIS {2} / HAL {3} / FreeRTOS {4} / LwIP {5}" -f `
    $AppSrc.Count, $VerifySrc.Count, $CmsisSrc.Count, $HalSrc.Count, $FrtSrc.Count, $LwipSrc.Count)
if ($env:SMU_DEBUG_INC) { Write-Host "INCLUDES:"; $INCLUDES | ForEach-Object { Write-Host "  $_" } }

$objs = @()
$fail = 0
$log = Join-Path $BuildDir "compile.log"
"" | Set-Content $log
foreach ($src in $AllC) {
    $name = (Split-Path $src -Leaf) -replace '\.c$', ''
    $obj = Join-Path $ObjDir "$name.o"
    & $GCC @CFLAGS @INCLUDES -c $src -o $obj 2>&1 | ForEach-Object {
        $_ | Add-Content $log
        if ($_ -match 'error') { $script:fail = 1 }
    }
    if ($LASTEXITCODE -ne 0) { $fail = 1; continue }
    $objs += $obj
}
if ($fail -ne 0) {
    Write-Host "=== compile errors (see build/compile.log) ===" -ForegroundColor Red
    Get-Content $log | Select-String -Pattern "error" | Select-Object -First 30 | ForEach-Object { Write-Host $_ }
    Write-Error "compile failed"
    exit 1
}

& $GCC $CPU -c -x assembler-with-cpp $Startup -o (Join-Path $ObjDir "startup.o") 2>&1 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { Write-Error "startup assembly failed"; exit 1 }
$objs += (Join-Path $ObjDir "startup.o")

if ($fail -ne 0) { Write-Error "compile failed"; exit 1 }

$elf = Join-Path $BuildDir "smu.elf"
$map = Join-Path $BuildDir "smu.map"
$LIBS = @("-lgcc", "-lc", "-lm", "-lnosys")
# 注意：不能用 nano.specs —— 其 newlib-nano 裁剪了 printf %f 支持，
# snprintf("%f") 会走 _dtoa/malloc 失败链导致 BusFault（BFAR 垃圾地址）。
# 这里用完整 newlib 并显式拉入浮点格式化/解析。
& $GCC $CPU "-T$Ld" "-Wl,--gc-sections" "-Wl,-Map=$map" `
    "-specs=nosys.specs" "-u" "_printf_float" "-u" "_scanf_float" `
    "-Wl,--start-group" @objs @LIBS "-Wl,--end-group" `
    -o $elf 2>&1 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { Write-Error "link failed"; exit 1 }

& $OBJCOPY -O binary $elf (Join-Path $BuildDir "smu.bin") 2>&1 | Out-Null
& $OBJCOPY -O ihex   $elf (Join-Path $BuildDir "smu.hex") 2>&1 | Out-Null

$size = (Get-Item (Join-Path $BuildDir "smu.bin")).Length
Write-Host ""
Write-Host "===== BUILD OK =====" -ForegroundColor Green
Write-Host ("  ELF : {0}" -f $elf)
Write-Host ("  BIN : {0}  ({1:N1} KB of 512 KB Flash)" -f (Join-Path $BuildDir "smu.bin"), ($size/1KB))
Write-Host "  size:"; arm-none-eabi-size $elf | Select-Object -Last 1
