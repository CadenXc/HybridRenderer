$SourceDir = (Get-Item (Join-Path $PSScriptRoot "..\Chimera\shaders")).FullName
$OutputDir = (Get-Item (Join-Path $PSScriptRoot "..\build\Sandbox\Debug\shaders")).FullName

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

Write-Host "Found $($Shaders.Count) shaders in $SourceDir. Compiling..." -ForegroundColor Cyan

foreach ($Shader in $Shaders) {
    $InputFile = $Shader.FullName
    
    # Calculate relative path manually to be safe
    $RelativePath = ""
    if ($Shader.DirectoryName.Length -gt $SourceDir.Length) {
        $RelativePath = $Shader.DirectoryName.Substring($SourceDir.Length).TrimStart("\").TrimStart("/")
    }
    
    $FileName = $Shader.Name
    
    # Ensure subdirectory exists in output
    $TargetDir = if ($RelativePath) { Join-Path $OutputDir $RelativePath } else { $OutputDir }
    if (!(Test-Path -Path $TargetDir)) {
        New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null
    }

    $OutputFile = Join-Path -Path $TargetDir -ChildPath "$FileName.spv"

    # Compile with Vulkan 1.3 target
    $BackendDir = (Get-Item (Join-Path $PSScriptRoot "..\Chimera\src\Renderer\Backend")).FullName
    Write-Host "Compiling: $FileName -> $(Join-Path $RelativePath "$FileName.spv")"
    glslc --target-env=vulkan1.3 -I "$SourceDir\common" -I "$SourceDir" -I "$BackendDir" $InputFile -o $OutputFile

    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error compiling $FileName" -ForegroundColor Red
        exit 1
    }
}

Write-Host "Shader compilation complete!" -ForegroundColor Green
