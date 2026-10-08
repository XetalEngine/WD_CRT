param([switch]$Rebuild, [switch]$Test)
$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'Install Visual Studio 2022 C++ build tools and a Windows SDK.' }
$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
function Build-Project([string]$project) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $msbuild
    $info.Arguments = '"' + $project + '" /p:Configuration=Release /p:Platform=x64 /t:' + $target + ' /nologo /v:minimal'
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    # MSBuild's older runtime rejects duplicate PATH/Path entries from some hosts.
    $info.EnvironmentVariables.Clear()
    Get-ChildItem Env: | ForEach-Object { $info.EnvironmentVariables[$_.Name.ToUpperInvariant()] = $_.Value }
    $process = [System.Diagnostics.Process]::Start($info)
    $output = $process.StandardOutput.ReadToEndAsync()
    $errors = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    Write-Host $output.Result
    if ($errors.Result) { Write-Host $errors.Result }
    if ($process.ExitCode) { exit $process.ExitCode }
}
Build-Project (Join-Path $PSScriptRoot 'wd.vcxproj')
if ($Test) {
    Build-Project (Join-Path $PSScriptRoot 'tests\tests.vcxproj')
    Push-Location $PSScriptRoot
    try {
        & (Join-Path $PSScriptRoot 'build\wd-tests.exe')
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    } finally { Pop-Location }
}
Get-Item (Join-Path $PSScriptRoot 'build\wd.dll') | Select-Object Name, Length
