<#
.SYNOPSIS
    SMU runtime diagnosis via J-Link SWD memory read (no serial / no network needed).
.DESCRIPTION
    Reads the running firmware state directly from RAM:
      - g_smu  : mode / setpoints / measurements / status word / duty
      - gnetif : LwIP netif (ip, mask, gw, flags)
      - ETH    : MACCR / MACMIIAR / DMAOMR / DMA status
      - FreeRTOS: scheduler running flag, task count
    Prints a human-readable verdict for each item.

    Usage:
        .\tools\jlink_diag.ps1
        .\tools\jlink_diag.ps1 -JLink "C:\Program Files\SEGGER\JLink_V942\JLink.exe"
#>
param(
    [string]$JLink = "C:\Program Files\SEGGER\JLink_V942\JLink.exe",
    [string]$Elf = "build\smu.elf"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot      # .../code/firmware
$Elf = Join-Path $Root $Elf
$Nm = "arm-none-eabi-nm"
$JLinkDir = Split-Path -Parent $JLink
$Work = Join-Path $Root "build\diag"
New-Item -ItemType Directory -Force -Path $Work | Out-Null

function Get-SymAddr([string]$name) {
    $line = (& $Nm $Elf) | Select-String -Pattern (" [bBdDtT] " + [regex]::Escape($name) + "$")
    if (-not $line) { return $null }
    return $line.Line.Split()[0]
}

$syms = @{
    g_smu  = Get-SymAddr "g_smu"
    gnetif = Get-SymAddr "gnetif"
    heth   = Get-SymAddr "heth"
    g_dbg_stage = Get-SymAddr "g_dbg_stage"
    g_dbg_hook_stage = Get-SymAddr "g_dbg_hook_stage"
    g_dbg_hook_lr = Get-SymAddr "g_dbg_hook_lr"
}
foreach ($k in $syms.Keys) {
    if (-not $syms[$k]) { Write-Error "symbol $k not found in $Elf (is it built?)" ; exit 1 }
}

# ---- build J-Link command script ----
# 注意：不要 r/h 再读（那读到的是复位瞬间的初始值）；
# 先让固件跑 1.5s，读运行态，最后 halt 看寄存器。
$script = @"
sleep 1500
mem32 $($syms['g_dbg_stage']), 1
mem32 $($syms['g_dbg_hook_stage']), 2
mem32 $($syms['g_smu']), 17
mem32 $($syms['gnetif']), 16
mem32 $($syms['heth']), 4
mem32 0x40028000, 4
mem32 0x40028100, 4
mem32 0xE000ED28, 4
mem32 0x08000000, 4
h
regs
exit
"@
$jl = Join-Path $Work "diag.jlink"
$script | Set-Content $jl -Encoding ASCII

Write-Host "=== J-Link runtime diagnosis (SWD) ==="
$out = & $JLink -Device STM32F407VE -If SWD -Speed 4000 -CommanderScript $jl 2>&1
$out | Out-File (Join-Path $Work "diag.log") -Encoding ASCII

# ---- parse hex words ----
$words = @{}
foreach ($line in $out) {
    if ($line -match "^([0-9A-Fa-f]{8})\s*=\s*(.+)$") {
        $base = [Convert]::ToUInt32($matches[1], 16)
        $vals = ($matches[2] -split "\s+") | Where-Object { $_ -match "^[0-9A-Fa-f]{8}$" }
        for ($i = 0; $i -lt $vals.Count; $i++) { $words[[uint32]($base + $i * 4)] = [Convert]::ToUInt32($vals[$i], 16) }
    }
}
if ($words.Count -eq 0) {
    Write-Host "[FAIL] no memory returned - is the board powered and connected?"
    Write-Host ($out | Select-Object -Last 8) -ForegroundColor DarkYellow
    exit 1
}

function F32([uint32]$v) { [BitConverter]::ToSingle([BitConverter]::GetBytes($v), 0) }
function Hex2Ip([uint32]$v) { "{0}.{1}.{2}.{3}" -f ($v -band 0xFF), (($v -shr 8) -band 0xFF), (($v -shr 16) -band 0xFF), (($v -shr 24) -band 0xFF) }

# ---- g_smu (smu_state_t) ----
$g = [Convert]::ToUInt32($syms['g_smu'], 16)
function Gw([int]$off) { return $words[[uint32]($g + $off)] }

$mode = Gw 0
$vset = F32 (Gw 4);  $iset = F32 (Gw 8)
$vprot = F32 (Gw 12); $iprot = F32 (Gw 16)
$outp = Gw 20
$vmeas = F32 (Gw 24); $imeas = F32 (Gw 28)
$vraw = F32 (Gw 32);  $iraw = F32 (Gw 36)
$adc0 = [int16]((Gw 40) -band 0xFFFF); $adc1 = [int16]((Gw 40) -shr 16)
$adc2 = [int16]((Gw 44) -band 0xFFFF); $adc3 = [int16]((Gw 44) -shr 16)
$stat = Gw 48 -band 0xFFFF
$err  = [int16]((Gw 50) -band 0xFFFF)
$j18  = Gw 52 -band 0xFF
$duty = F32 (Gw 56)
$uptime = Gw 64

$modeName = switch ($mode) { 0 {"HIZ"} 1 {"CV"} 2 {"CC"} 3 {"VM"} 4 {"IM"} default {"?"} }
$stFlags = @()
if ($stat -band 1) { $stFlags += "READY" }
if ($stat -band 2) { $stFlags += "PROT" }
if ($stat -band 4) { $stFlags += "ADC_OK" }
if ($stat -band 8) { $stFlags += "OUT_ACTIVE" }

Write-Host ""
Write-Host "--- g_smu (running state) ---"
Write-Host ("  mode      : {0}" -f $modeName)
Write-Host ("  setpoint  : V={0:F3} V  I={1:F3} mA   prot: {2:F1}V / {3:F1}mA" -f $vset, $iset, $vprot, $iprot)
Write-Host ("  measured  : V={0:F4} V  I={1:F4} mA   (raw {2:F4}/{3:F4})" -f $vmeas, $imeas, $vraw, $iraw)
Write-Host ("  ADC codes : AIN0={0} AIN1={1} AIN2={2} AIN3={3}" -f $adc0, $adc1, $adc2, $adc3)
Write-Host ("  status    : {0} [{1}]   err={2}  J18={3}  outp={4}" -f $stat, ($stFlags -join "|"), $err, $j18, $outp)
Write-Host ("  duty      : {0:P2}    uptime={1}s" -f $duty, $uptime)

# ---- gnetif ----
$n = [Convert]::ToUInt32($syms['gnetif'], 16)
function Nw([int]$off) { return $words[[uint32]($n + $off)] }
$ip = Nw 4; $mask = Nw 8; $gw = Nw 12
$flags = (Nw 41) -band 0xFF
$nflags = @()
if ($flags -band 1) { $nflags += "UP" }
if ($flags -band 2) { $nflags += "ETHARP" }
if ($flags -band 4) { $nflags += "BROADCAST" }
if ($flags -band 16) { $nflags += "LINK_UP" }
Write-Host ""
Write-Host "--- gnetif (LwIP) ---"
Write-Host ("  ip={0} mask={1} gw={2}" -f (Hex2Ip $ip), (Hex2Ip $mask), (Hex2Ip $gw))
Write-Host ("  flags=0x{0:X2} [{1}]" -f $flags, ($nflags -join "|"))
if (-not ($flags -band 16)) { Write-Host "  !! LINK_DOWN - check cable / PHY / lan8720_poll" -ForegroundColor Yellow }

# ---- ETH registers ----
$e = [Convert]::ToUInt32($syms['heth'], 16)
function Ew([int]$off) { return $words[[uint32]($e + $off)] }
$maccr = Ew 0
$te = ($maccr -shr 2) -band 1
$re = ($maccr -shr 3) -band 1
$dmaomr = $words[0x40028100]
$sr = $dmaomr -band 1; $st = ($dmaomr -shr 25) -band 1
$miiar = $words[0x40028010]
$miidr = $words[0x40028014]
Write-Host ""
Write-Host "--- ETH MAC/DMA ---"
Write-Host ("  MACCR=0x{0:X8}  TX_enable={1} RX_enable={2}" -f $maccr, $te, $re)
Write-Host ("  DMAOMR=0x{0:X8}  RX_started={1} TX_started={2}" -f $dmaomr, $sr, $st)
Write-Host ("  MIIAR=0x{0:X8} MIIDR=0x{1:X8}" -f $miiar, $miidr)
if (-not $te -or -not $re) { Write-Host "  !! MAC not enabled - ethernetif low_level_init failed?" -ForegroundColor Yellow }
if (-not $sr) { Write-Host "  !! DMA RX not started" -ForegroundColor Yellow }

# ---- FreeRTOS / Cortex-M ----
$pc = ($out | Select-String -Pattern "^PC = ([0-9A-Fa-f]+)") | ForEach-Object { $_.Matches[0].Groups[1].Value } | Select-Object -First 1
$cfsr = $words[0xE000ED28]
$hfsr = $words[0xE000ED2C]
$vtor = $words[0x08000000]
$sp0  = $words[0x08000000 -band 0xFFFFFFFF]
$sp0  = $words[0x08000000]
$resetVec = $words[0x08000004]
Write-Host ""
Write-Host "--- Cortex-M ---"
if ($pc) { Write-Host ("  PC = 0x{0}" -f $pc) }
Write-Host ("  CFSR=0x{0:X8} HFSR=0x{1:X8}" -f $cfsr, $hfsr)
if ($cfsr -ne 0 -or ($hfsr -band 0x40000000)) { Write-Host "  !! fault status non-zero - check HardFault handler" -ForegroundColor Red }
Write-Host ("  VTOR: initial SP=0x{0:X8}, Reset=0x{1:X8}" -f $sp0, $resetVec)
if ($uptime -eq 0 -and $mode -eq 0) {
    Write-Host "  !! uptime=0 and mode=HIZ -> scheduler may not be running" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== verdict ===" -ForegroundColor Green
$stage = $words[[Convert]::ToUInt32($syms['g_dbg_stage'],16)]
$hstage = $words[[Convert]::ToUInt32($syms['g_dbg_hook_stage'],16)]
$hlr = $words[[Convert]::ToUInt32($syms['g_dbg_hook_lr'],16)]
$stageNames = @{0="init";1="CalInit";2="MeasInit";3="ModeInit";4="CtrlInit";5="ProtInit";6="CmdInit";7="UartInit";8="SelfTest done";10="xTaskCreate(ctrl)";11="xTaskCreate(cmd)";12="xTaskCreate(net)";13="xTaskCreate(mon)";14="WebInit";99="AppInit done";100="tcpip_init";101="netif_add";102="MX_LWIP_Init done"}
$sname = if ($stageNames.ContainsKey([int]$stage)) { $stageNames[[int]$stage] } else { "?" }
Write-Host ("  stage={0} ({1})  hook_stage={2} hook_lr=0x{3:X8}" -f $stage, $sname, $hstage, $hlr)
if ($hstage -ne 0) {
    Write-Host ("  !! malloc/overflow hook fired at stage {0} ({1})" -f $hstage, $(if ($stageNames.ContainsKey([int]$hstage)) { $stageNames[[int]$hstage] } else { "?" })) -ForegroundColor Red
}
$issues = @()
if (-not ($stat -band 1)) { $issues += "READY not set (self-test failed - check ADS1115/I2C)" }
if (-not ($stat -band 4)) { $issues += "ADC_OK not set (ADS1115 not responding)" }
if (-not ($nflags -contains "LINK_UP")) { $issues += "ETH link down" }
if (-not $te -or -not $re) { $issues += "ETH MAC not enabled" }
if ($uptime -eq 0) { $issues += "ctrl task not ticking (uptime=0)" }
if ($issues.Count -eq 0) {
    Write-Host "  ALL OK - firmware running, ADC online, ETH link up."
    Write-Host "  Next: open http://$((Hex2Ip $ip))/ in a browser, or POST to /set and /cmd."
} else {
    foreach ($i in $issues) { Write-Host ("  - {0}" -f $i) -ForegroundColor Yellow }
}
