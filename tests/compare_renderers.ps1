param(
  [string]$BinaryDirectory = '.',
  [Parameter(Mandatory=$true)][string]$Rom,
  [string]$ConfigTemplate,
  [switch]$Enhancements,
  [ValidateSet(0,2,3,4)][int]$ResolutionScale = 0,
  [switch]$IncludeVSync
)
$ErrorActionPreference = 'Stop'
$BinaryDirectory = (Resolve-Path -LiteralPath $BinaryDirectory).Path
$Rom = (Resolve-Path -LiteralPath $Rom).Path
if ($ConfigTemplate) { $ConfigTemplate = (Resolve-Path -LiteralPath $ConfigTemplate).Path }
$repo = Split-Path -Parent $PSScriptRoot
$label = 'renderer-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
$cases = @(
  @{ renderer='direct3d11'; fps='60'; vsync='0' },
  @{ renderer='direct3d11'; fps='120'; vsync='0' },
  @{ renderer='direct3d11'; fps='360'; vsync='0' },
  @{ renderer='opengl'; fps='120'; vsync='0' }
)
if ($IncludeVSync) {
  # Decoupled interpolation forces swap interval zero. Use stock mode to
  # compare actual driver VSync rather than changing an overridden setting.
  $cases += @{ renderer='direct3d11'; fps='Auto'; vsync='0'; stock=$true }
  $cases += @{ renderer='direct3d11'; fps='Auto'; vsync='1'; stock=$true }
}
$results = @()
$reportDirectory = Join-Path $repo ('build-lag-evidence/' + $label + '-report')
New-Item -ItemType Directory -Path $reportDirectory | Out-Null
foreach ($case in $cases) {
  $run = $label + '-' + $case.renderer + '-' + $case.fps + '-vsync' + $case.vsync
  if ($case.stock) { $run += '-stock' }
  $options = @{
    BinaryDirectory=$BinaryDirectory; Rom=$Rom; RunLabel=$run;
    Desktop=$true; Paced=$true; Audio=$true;
    Renderer=$case.renderer; Fps=$case.fps; VSync=$case.vsync;
    ResolutionScale=$ResolutionScale
  }
  if ($ConfigTemplate) { $options.ConfigTemplate=$ConfigTemplate }
  if ($case.stock) { $options.ResolutionScale=0 }
  elseif ($Enhancements) { $options.Enhancements=$true }
  else { $options.InterpolationOnly=$true }
  $directory = Join-Path $repo ('build-lag-evidence/' + $run)
  try {
    & (Join-Path $PSScriptRoot 'measure_lag.ps1') @options | Out-Host
    $results += Get-Content (Join-Path $directory 'summary.json') -Raw | ConvertFrom-Json
  } catch {
    $results += [pscustomobject]@{run=$run; error=$_.Exception.Message}
    Write-Warning "Run $run failed: $($_.Exception.Message)"
  }
  $runReport = Join-Path $reportDirectory $run
  New-Item -ItemType Directory -Path $runReport | Out-Null
  foreach ($name in 'summary.json','timing.csv','stdout.log','stderr.log') {
    $file = Join-Path $directory $name
    if (Test-Path -LiteralPath $file) { Copy-Item -LiteralPath $file -Destination $runReport }
  }
}
$report = Join-Path $reportDirectory 'comparison.json'
$results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $report
$archive = Join-Path $repo ('build-lag-evidence/' + $label + '.zip')
Compress-Archive -LiteralPath $reportDirectory -DestinationPath $archive
$results | Select-Object run,guestFieldsPerSecond,presentationsPerSecond,compose_ms,present_ms,vsyncEffective,audio_underflows_delta,error | Format-Table -AutoSize
Write-Host "Diagnostic report: $archive"
Write-Host 'The report contains timings and logs. Your ROM, saves and settings are excluded.'
