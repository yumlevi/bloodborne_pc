param()

$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath($PSScriptRoot)
$go = Get-Command go -ErrorAction SilentlyContinue
if ($go) {
    Push-Location $source
    try {
        & $go.Source test ./...
        if ($LASTEXITCODE -ne 0) { throw 'Go tests failed' }
        $oldGoos = $env:GOOS
        $oldGoarch = $env:GOARCH
        $oldCgo = $env:CGO_ENABLED
        try {
            $env:GOOS = 'windows'; $env:GOARCH = 'amd64'; $env:CGO_ENABLED = '0'
            & $go.Source build -o bbport-net.exe .
            if ($LASTEXITCODE -ne 0) { throw 'Go build failed' }
        } finally {
            $env:GOOS = $oldGoos; $env:GOARCH = $oldGoarch; $env:CGO_ENABLED = $oldCgo
        }
    } finally { Pop-Location }
} else {
    $docker = Get-Command docker -ErrorAction SilentlyContinue
    if (-not $docker) { throw 'Install Go 1.27.1 or Docker to build the embedded network helper.' }
    & $docker.Source run --rm -v "${source}:/src" -v 'bbport-embedded-go-cache:/go' -w /src golang:1.27.1 sh -c 'go test ./... && GOOS=windows GOARCH=amd64 CGO_ENABLED=0 go build -o bbport-net.exe .'
    if ($LASTEXITCODE -ne 0) { throw 'Docker Go build failed' }
}
Write-Host "Built $source\bbport-net.exe"
