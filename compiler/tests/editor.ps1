param([string]$compiler = "$PSScriptRoot/../build/Debug/C4R.exe")
$ErrorActionPreference = 'Stop'
$compiler = (Resolve-Path $compiler).Path
$dir = Join-Path $PSScriptRoot '../build/editor-tests'
New-Item -ItemType Directory -Force $dir | Out-Null
$source = 'int32 add(int32 a, bool b) { int32 local = 1; { int32 inner = 2; } local }'
$file = Join-Path $dir 'symbols.c4r'
[IO.File]::WriteAllText($file, $source, [Text.UTF8Encoding]::new($false))
$offset = $source.LastIndexOf('local')
$symbols = & $compiler --symbols $offset $file | Out-String
if ($LASTEXITCODE -ne 0 -or !$symbols.Contains("C4R_SYMBOL`tfunction`tadd`tint32 add(int32 a, bool b)") -or !$symbols.Contains("C4R_SYMBOL`tvariable`ta`tint32") -or !$symbols.Contains("C4R_SYMBOL`tvariable`tlocal`tint32")) { throw "Missing symbol: $symbols" }
if ($symbols.Contains("C4R_SYMBOL`tvariable`tinner`t")) { throw "Out-of-scope symbol: $symbols" }
$bad = Join-Path $dir 'bad.c4r'
[IO.File]::WriteAllText($bad, 'int32 main() { int32 x = true; return x; }')
$ErrorActionPreference = 'Continue'
$output = & $compiler --check $bad 2>&1 | Out-String
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -eq 0 -or !$output.Contains('Initializer type mismatch')) { throw "Expected type error: $output" }
$good = Join-Path $dir 'good.c4r'
[IO.File]::WriteAllText($good, 'int32 main() { return 0; }')
& $compiler --check $good | Out-Null
if ($LASTEXITCODE -ne 0 -or (Test-Path ([IO.Path]::ChangeExtension($good, '.obj')))) { throw 'Check should succeed without emitting an object file' }
Write-Host 'Editor protocol tests passed.'
