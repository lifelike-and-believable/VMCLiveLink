# Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
<#
.SYNOPSIS
  Fails if a tracked .uasset or .umap was saved by an engine newer than the oldest one supported.

.DESCRIPTION
  An engine can't load a package saved by a newer engine, so the content is saved from the oldest
  supported engine (see CONTRIBUTING). This reads the UE5 package file version from the header of
  every tracked .uasset and .umap (git ls-files) and lists each one newer than -MaxFileVersionUE5.
  Run it from the repository root, after git lfs pull (an LFS pointer isn't a package and fails).

.PARAMETER MaxFileVersionUE5
  The newest UE5 package file version allowed: the one the oldest supported engine writes
  (EUnrealEngineObjectUE5Version in Core/Public/UObject/ObjectVersion.h). UE 5.6 writes 1017;
  5.7 and 5.8 write 1018.
#>
param(
  [int]$MaxFileVersionUE5 = 1017
)

# Paths as UTF-8, unquoted (VRM imports can give assets Japanese names).
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$Files = @(git -c core.quotepath=off ls-files -- '*.uasset' '*.umap')
if ($LASTEXITCODE -ne 0) { Write-Error "git ls-files failed"; exit 1 }

$Bad = @()
foreach ($File in $Files) {
  $Stream = [System.IO.File]::OpenRead((Join-Path (Get-Location) $File))
  try {
    $Header = New-Object byte[] 20
    $Read = $Stream.Read($Header, 0, 20)
  } finally {
    $Stream.Dispose()
  }
  # The package tag 0x9E2A83C1, little-endian.
  $IsPackage = $Read -ge 20 -and $Header[0] -eq 0xC1 -and $Header[1] -eq 0x83 -and $Header[2] -eq 0x2A -and $Header[3] -eq 0x9E
  if (-not $IsPackage) {
    $Bad += "$File`: not a package (an LFS pointer? run git lfs pull)"
    continue
  }
  # FPackageFileSummary: Tag, LegacyFileVersion, LegacyUE3Version (unless -4), FileVersionUE4,
  # FileVersionUE5 (when LegacyFileVersion <= -8).
  $Legacy = [BitConverter]::ToInt32($Header, 4)
  if ($Legacy -lt -9) {
    # A newer header layout than UE 5.6 to 5.8 write (-9); 5.6 refuses to load it anyway.
    $Bad += "$File`: unknown package header layout ($Legacy)"
    continue
  }
  $Offset = if ($Legacy -ne -4) { 12 } else { 8 }
  $UE5 = if ($Legacy -le -8) { [BitConverter]::ToInt32($Header, $Offset + 4) } else { 0 }
  if ($UE5 -gt $MaxFileVersionUE5) {
    $Bad += "$File`: saved at UE5 package version $UE5 (newest allowed $MaxFileVersionUE5)"
  }
}

Write-Host "Checked $($Files.Count) packages against UE5 package version $MaxFileVersionUE5."
if ($Bad.Count -gt 0) {
  $Bad | ForEach-Object { Write-Host "  $_" }
  Write-Error "$($Bad.Count) package(s) can't be loaded by the oldest supported engine. Re-save them from that engine, or restore them from main."
  exit 1
}
