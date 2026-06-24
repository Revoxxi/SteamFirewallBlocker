param(
    [string]$OutputFile = $env:GITHUB_OUTPUT
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Set-ActionOutput {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Value
    )

    if ($OutputFile) {
        Add-Content -Path $OutputFile -Encoding utf8 -Value "${Name}=${Value}"
    }

    Write-Host "${Name}=${Value}"
}

function Get-LastVersionTag {
    $tag = git tag --list 'v*' --sort=-v:refname | Select-Object -First 1
    if (-not $tag) {
        return 'v0.0.0'
    }
    return $tag
}

function Parse-VersionTag {
    param([Parameter(Mandatory = $true)][string]$Tag)

    if ($Tag -notmatch '^v(\d+)\.(\d+)\.(\d+)$') {
        throw "Latest tag '$Tag' is not in vMajor.Minor.Fix format."
    }

    return [pscustomobject]@{
        Major = [int]$Matches[1]
        Minor = [int]$Matches[2]
        Fix   = [int]$Matches[3]
    }
}

function Get-BumpLevel {
    param([Parameter(Mandatory = $true)][string]$Subject)

    $text = $Subject.ToLowerInvariant()

    $majorKeywords = @('#major', 'version:major', '[major]')
    $minorKeywords = @('#minor', 'version:minor', '[minor]')
    $fixKeywords = @('#fix', 'version:fix', '[fix]', '#patch', 'version:patch', '[patch]')

    foreach ($keyword in $majorKeywords) {
        if ($text.Contains($keyword)) {
            return 2
        }
    }

    foreach ($keyword in $minorKeywords) {
        if ($text.Contains($keyword)) {
            return 1
        }
    }

    foreach ($keyword in $fixKeywords) {
        if ($text.Contains($keyword)) {
            return 0
        }
    }

    return 0
}

function Get-BumpLabel {
    param([Parameter(Mandatory = $true)][int]$Level)

    switch ($Level) {
        2 { return 'major' }
        1 { return 'minor' }
        default { return 'fix' }
    }
}

function Test-GitTagExists {
    param([Parameter(Mandatory = $true)][string]$Tag)

    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    git rev-parse "refs/tags/$Tag" 1>$null 2>$null
    $exists = $LASTEXITCODE -eq 0
    $ErrorActionPreference = $previousPreference
    return $exists
}

$lastTag = Get-LastVersionTag
$previousVersion = Parse-VersionTag -Tag $lastTag

$headSha = (git rev-parse HEAD).Trim()
$lastTagSha = ''
if (Test-GitTagExists -Tag $lastTag) {
    $lastTagSha = (git rev-list -n 1 $lastTag).Trim()
}

if ($lastTagSha -and ($headSha -eq $lastTagSha.Trim())) {
    Set-ActionOutput -Name 'should_release' -Value 'false'
    Set-ActionOutput -Name 'version' -Value "$($previousVersion.Major).$($previousVersion.Minor).$($previousVersion.Fix)"
    Set-ActionOutput -Name 'tag' -Value $lastTag
    Set-ActionOutput -Name 'previous_tag' -Value $lastTag
    Set-ActionOutput -Name 'bump' -Value 'none'
    Write-Host 'No new commits since the latest tag. Skipping release.'
    exit 0
}

$commitRange = if ($lastTagSha) { "$lastTag..HEAD" } else { 'HEAD' }
$commitSubjects = @(git log $commitRange --pretty=format:%s)
if ($commitSubjects.Count -eq 0) {
    $commitSubjects = @((git log -1 --pretty=format:%s).Trim())
}

$bumpLevel = 0
foreach ($subject in $commitSubjects) {
    $level = Get-BumpLevel -Subject $subject
    if ($level -gt $bumpLevel) {
        $bumpLevel = $level
    }
}

$newMajor = $previousVersion.Major
$newMinor = $previousVersion.Minor
$newFix = $previousVersion.Fix

switch ($bumpLevel) {
    2 {
        $newMajor += 1
        $newMinor = 0
        $newFix = 0
    }
    1 {
        $newMinor += 1
        $newFix = 0
    }
    default {
        $newFix += 1
    }
}

$newVersion = "$newMajor.$newMinor.$newFix"
$newTag = "v$newVersion"
$bumpLabel = Get-BumpLabel -Level $bumpLevel

Set-ActionOutput -Name 'should_release' -Value 'true'
Set-ActionOutput -Name 'version' -Value $newVersion
Set-ActionOutput -Name 'tag' -Value $newTag
Set-ActionOutput -Name 'previous_tag' -Value $lastTag
Set-ActionOutput -Name 'bump' -Value $bumpLabel
Set-ActionOutput -Name 'commit_count' -Value "$($commitSubjects.Count)"

Write-Host "Previous tag: $lastTag"
Write-Host "New tag: $newTag ($bumpLabel bump from $($commitSubjects.Count) commit(s))"
