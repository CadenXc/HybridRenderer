$SourceDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\Chimera\shaders"))
$OutputDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\build\Sandbox\Debug\shaders"))
$BackendDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\Chimera\src\Renderer\Backend"))
$GlslcPath = "D:\Software\Vulkan\Bin\glslc.exe"

if (-not (Test-Path $SourceDir))
{
    Write-Host "Source directory not found: $SourceDir" -ForegroundColor Red
    exit 1
}

$ShaderExtensions = @("*.vert", "*.frag", "*.comp", "*.rgen", "*.rmiss", "*.rchit", "*.rahit", "*.rcall", "*.rahit", "*.rint")
$ShaderFiles = Get-ChildItem -Path $SourceDir -Recurse -Include $ShaderExtensions

Write-Host "Found $($ShaderFiles.Count) shaders. Compiling into $OutputDir" -ForegroundColor Cyan

foreach ($File in $ShaderFiles)
{
    $RelativePath = $File.FullName.Replace($SourceDir + "\", "")
    $OutputFile = Join-Path $OutputDir ($RelativePath + ".spv")
    $OutputSubDir = Split-Path $OutputFile -Parent

    if (-not (Test-Path $OutputSubDir))
    {
        New-Item -ItemType Directory -Path $OutputSubDir -Force | Out-Null
    }

    Write-Host "Compiling: $RelativePath..."
    
    $Params = @(
        $File.FullName,
        "--target-env=vulkan1.3",
        "-I", "$BackendDir",
        "-I", "$SourceDir/common",
        "-I", "$SourceDir",
        "-o", $OutputFile
    )

    & $GlslcPath @Params

    if ($LASTEXITCODE -ne 0)
    {
        Write-Host "FATAL ERROR compiling $RelativePath" -ForegroundColor Red
        exit 1
    }
}

Write-Host "Shader compilation successful!" -ForegroundColor Green
