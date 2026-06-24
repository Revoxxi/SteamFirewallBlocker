param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$PreviousTag,
    [Parameter(Mandatory = $true)][string]$Bump,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-BumpReason {
    param([Parameter(Mandatory = $true)][string]$Subject)

    $text = $Subject.ToLowerInvariant()
    if ($text.Contains('#major') -or $text.Contains('version:major') -or $text.Contains('[major]')) {
        return 'major'
    }
    if ($text.Contains('#minor') -or $text.Contains('version:minor') -or $text.Contains('[minor]')) {
        return 'minor'
    }
    if ($text.Contains('#fix') -or $text.Contains('version:fix') -or $text.Contains('[fix]') -or
        $text.Contains('#patch') -or $text.Contains('version:patch') -or $text.Contains('[patch]')) {
        return 'fix'
    }
    return ''
}

$commitRange = if ($PreviousTag -and $PreviousTag -ne 'v0.0.0') { "$PreviousTag..HEAD" } else { 'HEAD' }
$commits = @(git log $commitRange --pretty=format:'%h|%s|%an')

$triggerCommits = @()
foreach ($entry in $commits) {
    $parts = $entry.Split('|', 3)
    if ($parts.Count -lt 2) {
        continue
    }

    $reason = Get-BumpReason -Subject $parts[1]
    if ($reason -eq $Bump) {
        $triggerCommits += "- ``$($parts[0])`` $($parts[1]) ($($parts[2]))"
    }
}

$allCommitLines = @()
foreach ($entry in $commits) {
    $parts = $entry.Split('|', 3)
    if ($parts.Count -lt 2) {
        continue
    }

    $keyword = Get-BumpReason -Subject $parts[1]
    $suffix = if ($keyword) { " ``[$keyword]``" } else { '' }
    $author = if ($parts.Count -ge 3) { " ($($parts[2]))" } else { '' }
    $allCommitLines += "- ``$($parts[0])`` $($parts[1])$suffix$author"
}

$lines = @(
    "# Steam Firewall Blocker v$Version",
    '',
    'Supplied by **Revoxxi**.',
    '',
    "## Version",
    "- Previous release: ``$PreviousTag``",
    "- New release: ``v$Version``",
    "- Bump type: **$Bump**",
    ''
)

if ($triggerCommits.Count -gt 0) {
    $lines += '### Commits that triggered this bump'
    $lines += $triggerCommits
    $lines += ''
}

$lines += '### All changes since last release'
if ($allCommitLines.Count -eq 0) {
    $lines += '- No commit messages found.'
} else {
    $lines += $allCommitLines
}

$lines += ''
$lines += '### Install'
$lines += '1. Download ``SteamFirewallBlocker-Windows-x64-v' + $Version + '.zip``.'
$lines += '2. Extract ``SteamFirewallBlocker.exe``.'
$lines += '3. Run as Administrator.'

$lines -join "`n" | Set-Content -Path $OutputPath -Encoding utf8

Write-Host "Release notes written to $OutputPath"
