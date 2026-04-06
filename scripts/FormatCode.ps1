$extensions = @("*.h", "*.cpp", "*.hpp", "*.c")
$directories = @("Chimera/src", "Sandbox/src")

foreach ($dir in $directories) {
    if (Test-Path $dir) {
        Write-Host "Formatting $dir..."
        Get-ChildItem -Path $dir -Include $extensions -Recurse | ForEach-Object {
            Write-Host "Formatting $($_.FullName)"
            clang-format -i $_.FullName
        }
    }
}
