param(
    [string]$SourceRoot = 'C:\projects\doom-fx-reference\source',
    [string]$RomPath = 'Doom (USA).sfc',
    [string]$OutputPath = 'build-evidence/physics/object-diameters.json'
)
$ErrorActionPreference = 'Stop'
$constants = @{}
foreach ($line in [IO.File]::ReadAllLines((Join-Path $SourceRoot 'rlobjects.i'))) {
    if ($line -match '^\s*(odd\w+)\s+equ\s+(\d+)\b') {
        $constants[$Matches[1]] = [int]$Matches[2]
    }
}
$diameters = @()
$needDiameter = $false
foreach ($line in [IO.File]::ReadAllLines((Join-Path $SourceRoot 'objdata.a'))) {
    if ($line -match '^\s*dc\.w\s+') { $needDiameter = $true; continue }
    if ($needDiameter -and $line -match '^\s*dc\.b\s+(\w+)\b') {
        $value = $Matches[1]
        if ($value -match '^\d+$') { $diameters += [int]$value }
        elseif ($constants.ContainsKey($value)) { $diameters += $constants[$value] }
        else { throw "Unresolved original diameter: $value" }
        $needDiameter = $false
    }
}
if ($diameters.Count -ne 70) { throw "Unexpected original object count: $($diameters.Count)" }
$rom = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $RomPath).Path)
if ($rom.Length -ne 0x200000) { throw 'Expected headerless retail US ROM' }
$records = for ($type = 0; $type -lt $diameters.Count; $type++) {
    $actual = $rom[0x1bfe9c + $type * 5]
    if ($actual -ne $diameters[$type]) { throw "Type $type diameter mismatch: source=$($diameters[$type]), ROM=$actual" }
    [ordered]@{ type = $type; diameter = $actual }
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
$records | ConvertTo-Json | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Output "All 70 original object diameters match retail ROM"
