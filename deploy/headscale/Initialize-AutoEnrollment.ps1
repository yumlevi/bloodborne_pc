param(
    [ValidateRange(1, 2147483647)][int]$UserId = 1,
    [switch]$Rotate
)

$ErrorActionPreference = 'Stop'
$headscaleCompose = Join-Path $PSScriptRoot 'compose.localtest.yaml'
$shadnetCompose = Join-Path $PSScriptRoot '..\shadnet\compose.localtest.yaml'
$oldUserId = $env:HEADSCALE_USER_ID
try {
    $env:HEADSCALE_USER_ID = [string]$UserId
    & docker compose -f $headscaleCompose up -d
    if ($LASTEXITCODE -ne 0) { throw 'Headscale Docker startup failed.' }
    & docker compose -f $shadnetCompose up -d --build
    if ($LASTEXITCODE -ne 0) { throw 'shadNet/portal Docker startup failed.' }

    & docker compose -f $shadnetCompose exec -T portal test -s /var/lib/portal/headscale_api_key 2>$null
    $hasKey = $LASTEXITCODE -eq 0
    if ($hasKey -and -not $Rotate) {
        Write-Host 'Auto-enrollment is already initialized. Existing API key was preserved.'
        return
    }
    $previousKey = ''
    if ($hasKey) {
        $previousKey = (& docker compose -f $shadnetCompose exec -T portal cat /var/lib/portal/headscale_api_key | Out-String).Trim()
        if ($LASTEXITCODE -ne 0) { throw 'Could not read the existing API key for rotation.' }
    }

    $apiKey = (& docker compose -f $headscaleCompose exec -T headscale headscale apikeys create --expiration 30d | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $apiKey -notmatch '^hskey-api-') {
        throw 'Could not create a Headscale API key.'
    }
    $apiKey | & docker compose -f $shadnetCompose exec -T -u 10001:10001 portal sh -c 'umask 077; cat > /var/lib/portal/headscale_api_key'
    if ($LASTEXITCODE -ne 0) { throw 'Could not store the API key in the portal volume.' }
    if ($previousKey -match '^hskey-api-([^-]+)-') {
        & docker compose -f $headscaleCompose exec -T headscale headscale apikeys expire --prefix $Matches[1]
        if ($LASTEXITCODE -ne 0) { Write-Warning 'New key works, but expiring the previous API key failed.' }
    }
    Write-Host 'Auto-enrollment initialized; admin API key is stored only in the portal Docker volume.'
    Write-Host 'It expires in 30 days. Run this script with -Rotate before expiry.'
} finally {
    $env:HEADSCALE_USER_ID = $oldUserId
}
