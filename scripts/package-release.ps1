param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("win-x64")]
    [string]$Runtime = "win-x64",

    [switch]$SkipBuild,

    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$controlUiProject = Join-Path $repoRoot "control-ui\PurpleReaction.ControlUI\PurpleReaction.ControlUI.csproj"
$nativeProject = Join-Path $repoRoot "vs\PurpleReaction.Native\PurpleReaction.Native.vcxproj"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "artifacts\release\$Configuration"
}

$publishDir = Join-Path $repoRoot "artifacts\publish-controlui\$Configuration"
$nativeExePrimary = Join-Path $repoRoot "build-vs18\$Configuration\PurpleReaction.exe"
$nativeExeFallback = Join-Path $repoRoot "vs\PurpleReaction.Native\build-vs18\$Configuration\PurpleReaction.exe"
$zipPath = Join-Path $repoRoot "artifacts\PurpleReaction-$Configuration-$Runtime.zip"

if (-not $SkipBuild) {
    Write-Host "Publishing Control UI..." -ForegroundColor Cyan
    dotnet publish $controlUiProject -c $Configuration -r $Runtime -p:Platform=x64 -p:SelfContained=true -o $publishDir
    if ($LASTEXITCODE -ne 0) {
        throw "Control UI publish failed."
    }

    $msbuild = Get-Command msbuild -ErrorAction SilentlyContinue
    if (-not $msbuild) {
        throw "msbuild was not found in PATH. Run this script from a Visual Studio Developer PowerShell."
    }

    Write-Host "Building native runner..." -ForegroundColor Cyan
    & $msbuild.Path $nativeProject /m /p:Configuration=$Configuration /p:Platform=x64 /p:SolutionDir="$repoRoot\"
    if ($LASTEXITCODE -ne 0) {
        throw "Native runner build failed."
    }
}

if (-not (Test-Path $publishDir)) {
    throw "Control UI publish output missing: $publishDir"
}

$nativeExe = $null
if (Test-Path $nativeExePrimary) {
    $nativeExe = $nativeExePrimary
} elseif (Test-Path $nativeExeFallback) {
    $nativeExe = $nativeExeFallback
}

if (-not $nativeExe) {
    throw "Native runner missing. Checked: $nativeExePrimary and $nativeExeFallback"
}

if (Test-Path $OutputDir) {
    Remove-Item -Recurse -Force $OutputDir
}
New-Item -ItemType Directory -Path $OutputDir | Out-Null

Write-Host "Assembling package folder..." -ForegroundColor Cyan
Copy-Item (Join-Path $publishDir "*") $OutputDir -Recurse -Force
Copy-Item $nativeExe (Join-Path $OutputDir "PurpleReaction.exe") -Force

foreach ($doc in @("README.md", "LICENSE", "appicon.jpg", "appicon.ico")) {
    $source = Join-Path $repoRoot $doc
    if (Test-Path $source) {
        Copy-Item $source (Join-Path $OutputDir $doc) -Force
    }
}

$zipDir = Split-Path -Parent $zipPath
if (-not (Test-Path $zipDir)) {
    New-Item -ItemType Directory -Path $zipDir | Out-Null
}
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}

Write-Host "Creating zip archive..." -ForegroundColor Cyan
Compress-Archive -Path (Join-Path $OutputDir "*") -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Package folder: $OutputDir" -ForegroundColor Green
Write-Host "Package zip:    $zipPath" -ForegroundColor Green
