param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [string]$OutputDir,

    [string]$Publisher = "CN=Team Arion"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$project = Join-Path $repoRoot "control-ui\PurpleReaction.ControlUI\PurpleReaction.ControlUI.csproj"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "artifacts\msix\$Configuration\"
}
if (-not $OutputDir.EndsWith("\")) {
    $OutputDir = "$OutputDir\"
}

$msbuild = Get-Command msbuild -ErrorAction SilentlyContinue
if (-not $msbuild) {
    throw "msbuild was not found in PATH. Run this script from a Visual Studio Developer PowerShell."
}

Write-Host "Building MSIX package..." -ForegroundColor Cyan
& $msbuild.Path $project /restore /m `
    /p:Configuration=$Configuration `
    /p:Platform=x64 `
    /p:RuntimeIdentifier=win-x64 `
    /p:WindowsPackageType=MSIX `
    /p:WindowsAppSDKSelfContained=false `
    /p:WindowsAppSdkBootstrapInitialize=false `
    /p:GenerateAppxPackageOnBuild=true `
    /p:UapAppxPackageBuildMode=SideloadOnly `
    /p:AppxBundle=Never `
    /p:AppxPackageDir="$OutputDir" `
    /p:AppxPackageSigningEnabled=false `
    /p:Publisher="$Publisher"

if ($LASTEXITCODE -ne 0) {
    throw "MSIX build failed."
}

Write-Host "MSIX output: $OutputDir" -ForegroundColor Green
Write-Host "Note: unsigned MSIX is for local/testing sideload workflows." -ForegroundColor Yellow
