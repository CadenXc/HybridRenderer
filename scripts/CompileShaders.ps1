$SourceDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\Chimera\shaders"))
$OutputDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\build\Sandbox\Debug\shaders"))
$GlslcPath = "D:\Software\Vulkan\Bin\glslc.exe"

# Ensure output directory exists
if (!(Test-Path -Path $OutputDir)) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}

$Shaders = Get-ChildItem -Path $SourceDir -Recurse -Include *.vert, *.frag, *.comp, *.rgen, *.rchit, *.rmiss, *.rahit

Write-Host "Found $($Shaders.Count) shaders. Compiling into $OutputDir" -ForegroundColor Cyan

foreach ($Shader in $Shaders) {
    $InputFile = $Shader.FullName
    $RelativePath = ""
    if ($Shader.DirectoryName.Length -gt $SourceDir.Length) {
        $RelativePath = $Shader.DirectoryName.Substring($SourceDir.Length).TrimStart("\").TrimStart("/")
    }
    
    $FileName = $Shader.Name
    $TargetDir = if ($RelativePath) { Join-Path $OutputDir $RelativePath } else { $OutputDir }
    if (!(Test-Path -Path $TargetDir)) {
        New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null
    }

    $OutputFile = Join-Path -Path $TargetDir -ChildPath "$FileName.spv"
    $BackendDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\Chimera\src\Renderer\Backend"))

    Write-Host "Compiling: $FileName..."
    
    # CRITICAL: Use full path and explicit target environment
    & $GlslcPath --target-env=vulkan1.3 -I "$SourceDir\common" -I "$SourceDir" -I "$BackendDir" $InputFile -o $OutputFile

    if ($LASTEXITCODE -ne 0) {
        Write-Host "FATAL ERROR compiling $FileName" -ForegroundColor Red
        exit 1
    }
}

Write-Host "Shader compilation successful!" -ForegroundColor Green
