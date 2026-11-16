param([string]$compiler = "$PSScriptRoot/../build/Debug/c4rc.exe")
$ErrorActionPreference = 'Continue'
$compiler = (Resolve-Path $compiler).Path
function Test-Cli([string[]]$arguments, [bool]$success, [string]$expected) {
    $result = & $compiler @arguments 2>&1 | Out-String -Width 10000
    if (($LASTEXITCODE -eq 0) -ne $success -or !$result.Contains($expected)) {
        throw "Unexpected result for $arguments`: $result"
    }
}
Test-Cli @('--help') $true 'Usage: c4rc'
Test-Cli @('-h') $true 'Usage: c4rc'
Test-Cli @('--help') $true '--version'
Test-Cli @('--help') $true '--input'
Test-Cli @('--version') $true 'c4rc '
Test-Cli @('-v') $true 'c4rc '
$version = & $compiler --version | Out-String -Width 10000
if ($version -notmatch 'c4rc\s+[0-9]+\.[0-9]+\.[0-9]+') { throw "Missing compiler version: $version" }
Test-Cli @('--unknown') $false 'Unknown argument'
Test-Cli @('-i') $false 'Input option requires'
Test-Cli @('-o') $false 'Output option requires'
Test-Cli @('-i', '__missing_c4r_input__.c4r') $false 'Failed to open source file'
Test-Cli @('-i', 'a.c4r', '-o', 'a.exe', '-o', 'b.exe') $false 'more than once'
$dir = Join-Path $PSScriptRoot '../build/cli-tests'
New-Item -ItemType Directory -Force $dir | Out-Null
$source = Join-Path $dir 'link-error.c4r'
$stdout = Join-Path $dir 'stdout.txt'
$stderr = Join-Path $dir 'stderr.txt'
[IO.File]::WriteAllText($source, 'int main() { return 0; }')
$missingOutput = Join-Path $dir (([Guid]::NewGuid().ToString()) + '/out.exe')
$arguments = '-i "' + $source + '" -o "' + $missingOutput + '"'
$process = Start-Process -FilePath $compiler -ArgumentList $arguments -RedirectStandardOutput $stdout -RedirectStandardError $stderr -NoNewWindow -PassThru -Wait
$outText = [IO.File]::ReadAllText($stdout)
$errText = [IO.File]::ReadAllText($stderr)
if ($process.ExitCode -eq 0 -or $outText.Contains('LNK1104') -or !$errText.Contains('LNK1104')) {
    throw "Linker diagnostics should go to stderr. stdout: $outText stderr: $errText"
}
foreach ($case in @(
    @{ Arguments = '--unknown'; Message = 'Unknown argument' },
    @{ Arguments = '-i "__missing_c4r_input__.c4r"'; Message = 'Failed to open source file' }
)) {
    $process = Start-Process -FilePath $compiler -ArgumentList $case.Arguments -RedirectStandardOutput $stdout -RedirectStandardError $stderr -NoNewWindow -PassThru -Wait
    $outText = [IO.File]::ReadAllText($stdout)
    $errText = [IO.File]::ReadAllText($stderr)
    if ($process.ExitCode -eq 0 -or $outText.Contains($case.Message) -or !$errText.Contains($case.Message)) {
        throw "CLI diagnostics should go to stderr. stdout: $outText stderr: $errText"
    }
}
Write-Host 'CLI tests passed.'
