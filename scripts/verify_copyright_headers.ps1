<#
.SYNOPSIS
  Verify every .h/.hpp/.cpp/.cxx file under a plugin's Source/ directory starts
  with an up-to-date copyright header. Check only - never modifies files.

.DESCRIPTION
  PowerShell port of add_copyright_headers.py's --verify mode, kept in sync by
  hand. Exists so CI doesn't depend on Python being available on the runner
  (self-hosted Windows runners here don't reliably have it, and installing it
  via actions/setup-python isn't reliable on locked-down runners either).
  For fixing headers locally, use add_copyright_headers.py instead - this
  script only checks.

.PARAMETER PluginDir
  Path to the plugin root folder (contains Source/).

.PARAMETER Holder
  Copyright holder text to enforce.

.EXAMPLE
  .\verify_copyright_headers.ps1 -PluginDir "Plugins\VMCLiveLink" -Holder "Lifelike & Believable Animation Design, Inc. | Athomas Goldberg"
#>

param(
  [Parameter(Mandatory=$true)][string]$PluginDir,
  [Parameter(Mandatory=$true)][string]$Holder
)

$ErrorActionPreference = "Stop"
$CurrentYear = (Get-Date).Year

$SourceRoot = Join-Path $PluginDir "Source"
if (-not (Test-Path $SourceRoot)) {
  Write-Error "No Source folder under $PluginDir"
  exit 1
}

$HeaderLineRegex = '^//\s*Copyright\s*\(c\)\s*(\d{4}(?:[–-]\d{4})?)\s+.+'

function Get-DesiredHeader([string]$Years) {
  return "// Copyright (c) $Years $Holder. All Rights Reserved."
}

function Get-NormalizedYears([string]$YearsStr) {
  if ($YearsStr -match '^(\d{4})[–-](\d{4})$') {
    $startYear = [int]$Matches[1]
    return "$startYear-$CurrentYear"
  }
  $y = [int]$YearsStr
  if ($y -ne $CurrentYear) { return "$y-$CurrentYear" }
  return "$CurrentYear"
}

$Violations = New-Object System.Collections.Generic.List[string]
$Files = Get-ChildItem -Path $SourceRoot -Recurse -File -Include *.h,*.hpp,*.cpp,*.cxx

foreach ($File in $Files) {
  $FirstLine = Get-Content -LiteralPath $File.FullName -TotalCount 1 -Encoding UTF8
  if ($null -eq $FirstLine) { continue }

  if ($FirstLine -match $HeaderLineRegex) {
    $Years = Get-NormalizedYears $Matches[1]
    $Desired = Get-DesiredHeader $Years
    if ($FirstLine.TrimEnd() -ne $Desired) {
      $Violations.Add($File.FullName)
    }
  } else {
    $Violations.Add($File.FullName)
  }
}

if ($Violations.Count -gt 0) {
  Write-Host "Missing or outdated copyright header:"
  foreach ($V in $Violations) { Write-Host "  $V" }
  exit 1
}

Write-Host "All copyright headers OK."
