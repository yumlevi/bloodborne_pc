param(
    [string] $ControlUrl = 'http://127.0.0.1:18081',
    [string] $PortalUrl = 'http://127.0.0.1:18080',
    [string] $Executable = '',
    [string] $StateDir = '',
    [string] $LauncherState = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Security
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Executable) { $Executable = Join-Path $root 'tools\embedded_net\bbport-net.exe' }
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Missing bbport-net executable: $Executable"
}

$control = $null
if (-not [Uri]::TryCreate($ControlUrl.TrimEnd('/'), [UriKind]::Absolute, [ref]$control) -or
    $control.UserInfo -or $control.PathAndQuery -ne '/' -or $control.Fragment) {
    throw 'ControlUrl must be an HTTPS root URL, or a loopback HTTP root URL.'
}
if ($control.Scheme -ne 'https') {
    $loopback = $null
    if ($control.Scheme -ne 'http' -or
        (-not [Net.IPAddress]::TryParse($control.Host, [ref]$loopback)) -or
        (-not [Net.IPAddress]::IsLoopback($loopback))) {
        throw 'Remote ControlUrl must use HTTPS.'
    }
}

$portal = $null
if (-not [Uri]::TryCreate($PortalUrl.TrimEnd('/'), [UriKind]::Absolute, [ref]$portal) -or
    $portal.UserInfo -or $portal.PathAndQuery -ne '/' -or $portal.Fragment) {
    throw 'PortalUrl must be an HTTPS root URL, or a loopback HTTP root URL.'
}
if ($portal.Scheme -ne 'https') {
    $portalLoopback = $null
    if ($portal.Scheme -ne 'http' -or
        (-not [Net.IPAddress]::TryParse($portal.Host, [ref]$portalLoopback)) -or
        (-not [Net.IPAddress]::IsLoopback($portalLoopback))) {
        throw 'Remote PortalUrl must use HTTPS.'
    }
}

if (-not $StateDir) {
    $StateDir = Join-Path $env:LOCALAPPDATA 'BloodbornePC\seamless-server-net\identity'
}
if (-not $LauncherState) {
    $LauncherState = Join-Path $env:LOCALAPPDATA 'BloodbornePC\launcher.ini'
}
$StateDir = [IO.Path]::GetFullPath($StateDir)
New-Item -ItemType Directory -Force -Path $StateDir | Out-Null
$configPath = Join-Path (Split-Path $StateDir -Parent) 'server.json'
$config = [ordered]@{
    control_url = $control.AbsoluteUri.TrimEnd('/')
    state_dir = $StateDir
    hostname = 'bbport-seamless-server-' + $env:COMPUTERNAME.ToLowerInvariant()
    tcp_publishes = @(
        @{ tailnet_port = 31313; local = '127.0.0.1:32313' }
        @{ tailnet_port = 31315; local = '127.0.0.1:32315' }
        @{ tailnet_port = 18080; local = '127.0.0.1:32443' }
        @{ tailnet_port = 31316; local = '127.0.0.1:32444' }
    )
    udp_publishes = @(
        @{ tailnet_port = 31314; local = '127.0.0.1:32314'; stun_source = $true }
    )
}
$json = $config | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText($configPath, $json, [Text.UTF8Encoding]::new($false))

Write-Host 'Starting the isolated BBSC tailnet bridge in the foreground.'
Write-Host 'The allowed tailnet port numbers are reused on a separate node identity; the traditional node is not changed.'
Write-Host "Config: $configPath"
if (Test-Path -LiteralPath (Join-Path $StateDir 'tailscaled.state')) {
    & $Executable --config $configPath
    exit $LASTEXITCODE
}

if (-not (Test-Path -LiteralPath $LauncherState -PathType Leaf)) {
    throw 'Launcher account state was not found. Complete online account registration in the launcher first.'
}
$saved = @{}
foreach ($raw in [IO.File]::ReadAllLines([IO.Path]::GetFullPath($LauncherState))) {
    $line = $raw.Trim()
    if (-not $line -or $line.StartsWith('#')) { continue }
    $equals = $line.IndexOf('=')
    if ($equals -gt 0) { $saved[$line.Substring(0, $equals).Trim()] = $line.Substring($equals + 1).Trim() }
}
$applicationId = [string]$saved['online_application_id']
$protectedToken = [string]$saved['online_application_token_dpapi']
if (-not $applicationId -or -not $protectedToken) {
    throw 'No saved online registration credential was found. Complete online account registration in the launcher first.'
}

$tokenBytes = [Security.Cryptography.ProtectedData]::Unprotect(
    [Convert]::FromBase64String($protectedToken), $null,
    [Security.Cryptography.DataProtectionScope]::CurrentUser)
try {
    $applicationToken = [Text.Encoding]::UTF8.GetString($tokenBytes)
    $body = @{ application_id = $applicationId; application_token = $applicationToken } |
        ConvertTo-Json -Compress
    $reply = Invoke-RestMethod -Method Post -Uri ($portal.AbsoluteUri.TrimEnd('/') + '/v1/device-key') `
        -ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes($body))
    $deviceKey = [string]$reply.auth_key
    if (-not $deviceKey.StartsWith('hskey-auth-')) {
        throw 'The account portal did not return a valid one-time device key.'
    }
    Write-Host 'The registered launcher account issued a one-time server-node key.'
    $deviceKey | & $Executable --config $configPath --auth-key-stdin
    exit $LASTEXITCODE
}
finally {
    if ($tokenBytes) { [Array]::Clear($tokenBytes, 0, $tokenBytes.Length) }
    $applicationToken = $null
    $deviceKey = $null
}
