[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$PreviousVersion,
    [Parameter(Mandatory = $true)][string]$Repository,
    [Parameter(Mandatory = $true)][string]$Commit,
    [string]$ArtifactDirectory = 'artifact',
    [string]$ServerUrl = 'https://github.com'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'version.ps1')
$version = (Get-VqfBuildVersion $Version).Name
if ($PreviousVersion -and -not (Read-VqfVersionTag $PreviousVersion)) { throw 'Invalid previous release version.' }
if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $Commit -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Invalid repository or commit.'
}
$archiveName = "ttp_vqf-$version.zip"
$sourceName = "ttp_vqf-$version-source.zip"
$hash = (Get-FileHash -LiteralPath (Join-Path $ArtifactDirectory $archiveName) -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceHash = (Get-FileHash -LiteralPath (Join-Path $ArtifactDirectory $sourceName) -Algorithm SHA256).Hash.ToLowerInvariant()
$expected = (Get-Content -LiteralPath (Join-Path $ArtifactDirectory 'SHA256SUMS.txt') -Encoding UTF8 -Raw).Trim().Replace("`r`n","`n")
if ($expected -cne "$hash  $archiveName`n$sourceHash  $sourceName") { throw 'VQF package SHA-256 verification failed.' }
$repoUrl = "$ServerUrl/$Repository"
$logUrl = if ($PreviousVersion) { "$repoUrl/compare/$PreviousVersion...$version" } else { "$repoUrl/commits/$version" }
$notes = @"
**更新记录**: $logUrl

解压 ttp_vqf-$version.zip，关闭播放器后，将 AddIn/ttp_vqf.dll 放入安装目录。
支持 TwinVQ/VQF 浮点解码、定位、原两帧回调预热和只读文字标签；无需 tvqdec.dll。
同一 x86 DLL 面向 XP SP3、Win7 和新系统，CPU 需要 SSE2。
运行包仅包含 AddIn/ttp_vqf.dll 和 SHA256SUMS.txt。对应源码附件含完整 FFmpeg 源码、修改和构建说明，许可证保留在源码中，不嵌入 DLL。

[源码与验证说明]($repoUrl/tree/$Commit)
[第三方版本、来源和修改记录]($repoUrl/blob/$Commit/docs/DEPENDENCIES.md)
[完整许可证]($repoUrl/tree/$Commit/LICENSES)
"@
$notes | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'release-notes.md') -Encoding UTF8
