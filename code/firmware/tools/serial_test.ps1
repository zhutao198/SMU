<#
.SYNOPSIS
    SMU serial first-boot check: read banner, send test commands, print responses.
.DESCRIPTION
    Usage: connect the board USB (CH340N) to the PC, then run:
        .\tools\serial_test.ps1 -Port COM5
    The script opens the port at 115200-8N1, captures the boot banner,
    then sends a command list and prints each response.
#>
param(
    [string]$Port = "COM5",
    [int]$Baud = 115200,
    [int]$BootWaitMs = 2500
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Ports

$cmds = @("*IDN?", "STAT?", "MODE?", "MEAS:ALL?", "J18?", "RAW?", "VOLT:PROT?", "CURR:PROT?")

try {
    $sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
    $sp.NewLine = "`n"
    $sp.ReadTimeout = 300
    $sp.WriteTimeout = 300
    $sp.DtrEnable = $true
    $sp.RtsEnable = $true
    $sp.Open()
    Write-Host ("[port] {0} opened at {1}-8N1" -f $Port, $Baud)

    Write-Host ("[boot] waiting {0} ms for banner..." -f $BootWaitMs)
    Start-Sleep -Milliseconds $BootWaitMs
    $banner = $sp.ReadExisting()
    if ($banner) {
        Write-Host "----- boot banner -----"
        Write-Host $banner.Trim()
        Write-Host "------------------------"
    } else {
        Write-Host "[warn] no banner received (check wiring / baud / USB cable)"
    }

    foreach ($c in $cmds) {
        $sp.DiscardInBuffer()
        $sp.Write($c + "`n")
        Start-Sleep -Milliseconds 250
        $resp = $sp.ReadExisting()
        $resp = ($resp -replace "`r`n", " ").Trim()
        Write-Host ("  {0,-14} -> {1}" -f $c, $resp)
    }

    $sp.Close()
    Write-Host "[done] serial test finished"
} catch {
    Write-Host ("[error] {0}" -f $_.Exception.Message)
    Write-Host "hint: verify the COM number of 'USB-SERIAL CH340' in Device Manager"
}
