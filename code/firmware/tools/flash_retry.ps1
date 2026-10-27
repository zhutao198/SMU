<#
.SYNOPSIS
    Loop-flash the SMU board, retrying to catch the SWD attach window.
.DESCRIPTION
    The previous firmware locks up the AHB bus (DMA overflow), so J-Link cannot
    attach while it is running. Power-cycle the board and run this script; it
    retries connect+program until it succeeds (or N attempts).

    Usage:  .\tools\flash_retry.ps1 [-Tries 25]
#>
param([int]$Tries = 25)

$JLink = "C:\Program Files\SEGGER\JLink_V942\JLink.exe"
$Root = Split-Path -Parent $PSScriptRoot
$Script = Join-Path $Root "tools\flash.jlink"

for ($i = 1; $i -le $Tries; $i++) {
    $out = & $JLink -Device STM32F407VE -If SWD -Speed 1000 -CommanderScript $Script 2>&1
    $text = ($out -join "`n")
    if ($text -match "Verify successful") {
        Write-Host ("[{0}] FLASH OK (verify successful)" -f $i) -ForegroundColor Green
        exit 0
    }
    Write-Host ("[{0}] attach failed, retrying..." -f $i)
    Start-Sleep -Milliseconds 400
}
Write-Error "all attempts failed - power-cycle the board and retry"
exit 1
