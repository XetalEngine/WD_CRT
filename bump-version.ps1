param([ValidateSet('Patch', 'Minor', 'Major')][string]$Part = 'Patch')
$ErrorActionPreference = 'Stop'
$path = Join-Path $PSScriptRoot 'src\version.h'
$source = [IO.File]::ReadAllText($path)
$versionMatches = [regex]::Matches($source, '(?<=X-Engine v)\d+\.\d+\.\d+(?=")')
if ($versionMatches.Count -ne 1) { throw 'Expected one X-Engine version in src\version.h.' }
$current = [version]$versionMatches[0].Value
$major = $current.Major
$minor = $current.Minor
$patch = $current.Build
switch ($Part) {
    'Major' { $major++; $minor = 0; $patch = 0 }
    'Minor' { $minor++; $patch = 0 }
    'Patch' { $patch++ }
}
$next = "$major.$minor.$patch"
$updated = $source.Remove($versionMatches[0].Index, $versionMatches[0].Length).Insert($versionMatches[0].Index, $next)
[IO.File]::WriteAllText($path, $updated, [Text.UTF8Encoding]::new($false))
Write-Host "X-Engine v$next"
