# Shared date version rules for local builds, resources and Actions.
function Read-VqfVersionTag([string]$Name) {
    $match = [regex]::Match($Name, '^(?<date>[0-9]{4}\.[0-9]{2}\.[0-9]{2})(?:p(?<patch>[1-9][0-9]*))?$')
    if (-not $match.Success) { return }
    $date = [datetime]::MinValue
    if (-not [datetime]::TryParseExact($match.Groups['date'].Value, 'yyyy.MM.dd',
        [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::None, [ref]$date)) { return }
    $patch = 0L
    if ($match.Groups['patch'].Success -and -not [long]::TryParse($match.Groups['patch'].Value, [ref]$patch)) {
        throw 'Version patch number is too large.'
    }
    [pscustomobject]@{ Name = $Name; Date = $date; Patch = $patch }
}

function Get-VqfBuildVersion([string]$Version, [string]$SourceDirectory = (Split-Path $PSScriptRoot -Parent)) {
    if (-not $Version) {
        $saved = Join-Path $SourceDirectory 'BUILD_VERSION'
        $Version = if (Test-Path -LiteralPath $saved -PathType Leaf) {
            (Get-Content -LiteralPath $saved -Encoding UTF8 -Raw).Trim()
        } else {
            [DateTimeOffset]::UtcNow.ToOffset([TimeSpan]::FromHours(8)).ToString(
                'yyyy.MM.dd', [Globalization.CultureInfo]::InvariantCulture)
        }
    }
    $parsed = Read-VqfVersionTag $Version
    if (-not $parsed) { throw 'Build version must be a valid yyyy.MM.dd or yyyy.MM.ddpN date.' }
    # Each component of the Windows fixed file version is an unsigned WORD.
    if ($parsed.Patch -gt 65535) { throw 'Build version patch must not exceed 65535.' }
    return $parsed
}

function Assert-VqfFileVersion([string]$Path, [string]$Version) {
    $parsed = Get-VqfBuildVersion $Version
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo([IO.Path]::GetFullPath($Path))
    $expected = '{0}.{1}.{2}.{3}' -f $parsed.Date.Year,$parsed.Date.Month,$parsed.Date.Day,$parsed.Patch
    $file = '{0}.{1}.{2}.{3}' -f $info.FileMajorPart,$info.FileMinorPart,$info.FileBuildPart,$info.FilePrivatePart
    $product = '{0}.{1}.{2}.{3}' -f $info.ProductMajorPart,$info.ProductMinorPart,$info.ProductBuildPart,$info.ProductPrivatePart
    if ($info.FileVersion -cne $Version -or $info.ProductVersion -cne $Version -or
        $file -cne $expected -or $product -cne $expected) {
        throw "DLL version does not match package version $Version (file: $($info.FileVersion), product: $($info.ProductVersion))."
    }
}
