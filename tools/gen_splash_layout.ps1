param(
    [Parameter(Mandatory = $true)][string]$LayoutPath,
    [Parameter(Mandatory = $true)][string]$OutC
)

function Parse-IntPair {
    param([string]$Value)
    $parts = $Value -split '[, ]+' | Where-Object { $_ -ne '' }
    if ($parts.Count -ge 2) {
        return @([int]$parts[0], [int]$parts[1])
    }
    return $null
}

$map = @{}
Get-Content -LiteralPath $LayoutPath | ForEach-Object {
    $line = $_.Trim()
    if ($line.Length -eq 0) { return }
    if ($line.StartsWith('#') -or $line.StartsWith(';') -or $line.StartsWith('//')) { return }
    $parts = $line -split '=', 2
    if ($parts.Count -ne 2) { return }
    $key = $parts[0].Trim().ToUpperInvariant()
    $value = $parts[1].Trim()
    $map[$key] = $value
}

$keys = @(
    'AUTO','SELECT','L3','R3','START','UP','RIGHT','DOWN','LEFT','L2','R2','L1','R1','TRIANGLE','CIRCLE','CROSS','SQUARE'
)

$positions = @()
foreach ($key in $keys) {
    $x = -1
    $y = -1
    $pair = $null
    if ($map.ContainsKey("HOTKEY_$key")) {
        $pair = Parse-IntPair -Value $map["HOTKEY_$key"]
    }
    if ($pair -ne $null) {
        $x = $pair[0]
        $y = $pair[1]
    } else {
        if ($map.ContainsKey("HOTKEY_${key}_X")) { $x = [int]$map["HOTKEY_${key}_X"] }
        if ($map.ContainsKey("HOTKEY_${key}_Y")) { $y = [int]$map["HOTKEY_${key}_Y"] }
        if ($map.ContainsKey("${key}_X")) { $x = [int]$map["${key}_X"] }
        if ($map.ContainsKey("${key}_Y")) { $y = [int]$map["${key}_Y"] }
    }
    $positions += [pscustomobject]@{ X = $x; Y = $y }
}

function Get-ConsolePos {
    param([string]$BaseKey)
    $x = -1
    $y = -1
    if ($map.ContainsKey($BaseKey)) {
        $pair = Parse-IntPair -Value $map[$BaseKey]
        if ($pair -ne $null) {
            $x = $pair[0]
            $y = $pair[1]
        }
    } else {
        if ($map.ContainsKey("${BaseKey}_X")) { $x = [int]$map["${BaseKey}_X"] }
        if ($map.ContainsKey("${BaseKey}_Y")) { $y = [int]$map["${BaseKey}_Y"] }
    }
    return @($x, $y)
}

$consoleInfo = Get-ConsolePos -BaseKey 'CONSOLE_INFO'
$consoleTemp = Get-ConsolePos -BaseKey 'CONSOLE_TEMP'

$sw = New-Object System.IO.StreamWriter($OutC, $false, [System.Text.Encoding]::ASCII)
$sw.WriteLine('#include "splash_layout.h"')
$sw.WriteLine('')
$sw.WriteLine('const SplashTextPos splash_hotkey_positions[17] = {')
for ($i = 0; $i -lt $positions.Count; $i++) {
    $x = $positions[$i].X
    $y = $positions[$i].Y
    $comma = if ($i -lt $positions.Count - 1) { ',' } else { '' }
    $sw.WriteLine("    { $x, $y }$comma")
}
$sw.WriteLine('};')
$sw.WriteLine('')
$sw.WriteLine("const SplashTextPos splash_console_info_pos = { $($consoleInfo[0]), $($consoleInfo[1]) };")
$sw.WriteLine("const SplashTextPos splash_console_temp_pos = { $($consoleTemp[0]), $($consoleTemp[1]) };")
$sw.Close()
