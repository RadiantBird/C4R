param(
    [string]$mode = "build",
    [string]$exe  = "C4R"
)

function _init {
    $vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"

    $envOutput = & $env:ComSpec /d /c "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && set"

    if ($LASTEXITCODE -ne 0) {
        throw "Failed to initialize MSVC environment"
    }

    foreach ($line in $envOutput) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable(
                $matches[1],
                $matches[2],
                "Process"
            )
        }
    }
}

function _build {
    cmake -B build -S .
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed"
    }

    cmake --build build --config Debug
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }
}

function _run {
    & ".\build\Debug\$($exe).exe"
}

if ($mode -in @("build", "exec")) {
    _init
}

if ($mode -eq "build") {
    _build
}
elseif ($mode -eq "run") {
    _run
}
elseif ($mode -eq "exec") {
    _build
    _run
}
else {
    Write-Host "Unknown command: $($mode)"
    Write-Host @"
Available commands:
- build
- run
- exec ; (build and run)
"@
}