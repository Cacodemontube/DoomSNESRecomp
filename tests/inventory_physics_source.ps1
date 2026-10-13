param(
    [string]$SourceRoot = 'C:\projects\doom-fx-reference\source',
    [string]$OutputPath = 'build-evidence/physics/source-inventory.json'
)
$ErrorActionPreference = 'Stop'
# Inventory is deliberately not a parity verdict. Internal labels are included
# because public exports alone omit collision branches and gravity routines.
$names = @('rlplayer.a','rlplayer2.a','rlobjects.a','rlobjects2.a',
    'rlprojectiles.a','rlradius.a','rlfloors.a','rlceilings.a','rldoors.a',
    'rllifts.a','rlplats.a','rlenemies.a','rlenemies2.a','rlenemies3.a',
    'rlenemies4.a','rlenemies5.a','rlenemies6.a','rlenemies7.a',
    'rlmobjects.a','rlmobjects2.a','rlmobjects3.a')
$names += @('rlmove.a') + (2..8 | ForEach-Object { "rlmove$_.a" })
$files = foreach ($name in $names) {
    $path = Join-Path $SourceRoot $name
    $lines = [IO.File]::ReadAllLines($path)
    $labels = for ($i = 0; $i -lt $lines.Length; $i++) {
        if ($lines[$i] -match '^(_[A-Za-z0-9_]+)\s*(?:;.*)?$') {
            [ordered]@{ symbol = $Matches[1]; line = $i + 1; status = 'unverified' }
        }
    }
    [ordered]@{ file = $name; sha256 = (Get-FileHash -LiteralPath $path).Hash;
        labels = @($labels); status = 'unverified' }
}
$parent = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Force -Path $parent | Out-Null
[ordered]@{ source_root = $SourceRoot; scope_note = 'Includes internal branch labels; source variant and retail addresses still require verification. Historical old/a variants are not silently treated as retail.';
    files = @($files) } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Output "Inventoried $($files.Count) source modules in $OutputPath"
