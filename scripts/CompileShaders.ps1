$SourceDir = Join-Path $PSScriptRoot "..\Chimera\shaders"
$OutputDir = Join-Path $PSScriptRoot "..\build\Sandbox\Debug\shaders"

# Ensure output directory exists
if (!(Test-Path -Path $OutputDir)) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    Write-Host "Created output directory: $OutputDir" -ForegroundColor Green
}

# Find all shader files
$Shaders = Get-ChildItem -Path $SourceDir -Recurse -Include *.vert, *.frag, *.comp, *.rgen, *.rchit, *.rmiss

if ($Shaders.Count -eq 0) {
    Write-Host "No shaders found in $SourceDir" -ForegroundColor Yellow
    exit
}

Write-Host "Found $($Shaders.Count) shaders. Compiling..." -ForegroundColor Cyan

foreach ($Shader in $Shaders) {
    $InputFile = $Shader.FullName
    $FileName = $Shader.Name
    $OutputFile = Join-Path -Path $OutputDir -ChildPath "$FileName.spv"

    # Compile with Vulkan 1.3 target
    Write-Host "Compiling: $FileName -> $OutputFile"
    glslc --target-env=vulkan1.3 $InputFile -o $OutputFile

    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error compiling $FileName" -ForegroundColor Red
        exit 1
    }
}

Write-Host "Shader compilation complete!" -ForegroundColor Green
