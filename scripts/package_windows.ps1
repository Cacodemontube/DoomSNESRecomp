param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][ValidateSet('x64','x86')][string]$Architecture,
    [string]$Objdump = 'C:/msys64/mingw64/bin/objdump.exe'
)
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath("$PSScriptRoot/..")
$build = [System.IO.Path]::GetFullPath((Join-Path $root $BuildDirectory))
$version = (Get-Content "$root/VERSION" -Raw).Trim()
$name = "DoomSNESRecomp-v$version-Windows-$Architecture"
$stage = Join-Path $root "dist/$name"
if (Test-Path -LiteralPath $stage) { throw "Staging directory already exists: $stage" }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
$exe = Join-Path $build 'DoomSNESRecomp.exe'
if (!(Test-Path -LiteralPath $exe)) { throw "Executable missing: $exe" }
$data = [System.IO.File]::ReadAllBytes($exe)
$pe = [BitConverter]::ToInt32($data, 0x3c)
$machine = [BitConverter]::ToUInt16($data, $pe + 4)
$expected = if ($Architecture -eq 'x64') { 0x8664 } else { 0x14c }
if ($machine -ne $expected) { throw "Executable architecture does not match $Architecture" }
Copy-Item -LiteralPath $exe -Destination $stage
foreach ($folder in @('assets', 'mods')) {
    if (!(Test-Path -LiteralPath "$build/$folder")) { throw "Missing runtime directory: $folder" }
    Copy-Item -LiteralPath "$build/$folder" -Destination $stage -Recurse
}
Get-ChildItem -LiteralPath "$stage/mods" -Recurse -File |
    Where-Object { $_.Name -in @('state.toml','state.toml.tmp') } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName }
# Resolve imports recursively; only Windows system libraries stay external.
$system = '^(api-ms-|ext-ms-|kernel32|user32|gdi32|advapi32|shell32|ole32|oleaut32|comdlg32|comctl32|imm32|version|winmm|ws2_32|secur32|bcrypt|crypt32|ntdll|msvcrt|ucrtbase|setupapi|hid|cfgmgr32|dwmapi|shlwapi|opengl32|glu32|d2d1|dwrite|d3d[0-9]*|dxgi|dxguid|dinput8|dsound|powrprof|iphlpapi|propsys|rpcrt4|wintrust|normaliz|wldap32|winhttp|wininet|psapi|dbghelp|uxtheme|msimg32|avrt|mf|mfplat|mfreadwrite|mfuuid|ksuser)(\.|-)'
$queue = New-Object 'System.Collections.Generic.Queue[string]'
$queue.Enqueue((Join-Path $stage 'DoomSNESRecomp.exe'))
$seen = @{}
while ($queue.Count) {
    $binary = $queue.Dequeue()
    $imports = & $Objdump -p $binary
    if ($LASTEXITCODE) { throw "Cannot inspect imports: $binary" }
    foreach ($line in $imports) {
        if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
        $dll = $Matches[1]
        if ($dll -match $system -or $seen.ContainsKey($dll.ToLowerInvariant())) { continue }
        $seen[$dll.ToLowerInvariant()] = $true
        $source = Join-Path $build $dll
        if (!(Test-Path -LiteralPath $source)) { throw "Missing dependency: $dll" }
        Copy-Item -LiteralPath $source -Destination $stage
        $queue.Enqueue((Join-Path $stage $dll))
    }
}
Copy-Item -LiteralPath "$root/VERSION" -Destination $stage
foreach ($license in @('LICENSE','LICENSE.txt','LICENSE.md')) {
    if (Test-Path -LiteralPath "$root/$license") { Copy-Item -LiteralPath "$root/$license" -Destination $stage }
}
foreach ($file in (& git -C $root ls-files --recurse-submodules)) {
    if ([System.IO.Path]::GetFileName($file) -notmatch '^(LICENSE|COPYING|NOTICE)([._-]|$)') { continue }
    $destination = Join-Path $stage "licenses/$file"
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $destination
}
$sdlLicense = Join-Path $build '_deps/sdl3-src/LICENSE.txt'
if (!(Test-Path -LiteralPath $sdlLicense)) {
    $sdlLicense = Join-Path $root 'build/_deps/sdl3-src/LICENSE.txt'
}
if (!(Test-Path -LiteralPath $sdlLicense)) { throw 'SDL license missing.' }
New-Item -ItemType Directory -Path "$stage/licenses/SDL3" -Force | Out-Null
Copy-Item -LiteralPath $sdlLicense -Destination "$stage/licenses/SDL3/LICENSE.txt"
@"
DoomSNESRecomp v$version - Windows $Architecture

Extract the entire ZIP, then run DoomSNESRecomp.exe.
Select your own Doom (USA).sfc ROM in the launcher. No ROM is included.
Use the launcher's Mods page to enable display enhancements, transparent
automap, PC keyboard cheats, unlocked episodes, or modern controls.
The executable icon uses logo1.png. All enhancements are optional.

Keep assets/ and mods/ beside the executable.
"@ | Set-Content -LiteralPath "$stage/README.txt" -Encoding UTF8
$revision = & git -C $root rev-parse HEAD
$framework = & git -C "$root/snesrecomp" rev-parse HEAD
"game=$revision`nsnesrecomp=$framework" | Set-Content -LiteralPath "$stage/SOURCE_REVISIONS" -Encoding ASCII
$forbidden = Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object {
    $_.Extension -in @('.sfc','.smc','.srm','.fig') -or $_.Name -in @('rom.cfg','config.ini','keybinds.ini','input.ini')
}
if ($forbidden) { throw 'Runtime package contains ROM or machine-specific state.' }
$zip = "$stage.zip"
Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal
Get-FileHash -LiteralPath $zip -Algorithm SHA256
