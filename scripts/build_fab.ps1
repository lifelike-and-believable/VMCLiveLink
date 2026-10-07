<#
.SYNOPSIS
  Build and package a UE code plugin for Fab across multiple UE versions (Windows PowerShell).

.DESCRIPTION
  Uses Unreal AutomationTool (RunUAT.bat BuildPlugin) to compile the plugin for each engine version,
  and produces zips (one per engine) with EngineVersion set in the .uplugin. BuildPlugin's package
  includes Binaries and Intermediate, and they aren't removed.

.PARAMETER PluginDir
  Path to the plugin root folder (contains *.uplugin).

.PARAMETER OutputDir
  Where to place packaged zips.

.PARAMETER EngineRoots
  One or more Unreal Engine root folders (each must contain Engine/Build/BatchFiles/RunUAT.bat).

.PARAMETER EngineVersions
  Corresponding engine version strings to place in the .uplugin (e.g., 5.4.0 5.5.0 5.6.0).
#>

param(
  [Parameter(Mandatory=$true)][string]$PluginDir,
  [Parameter(Mandatory=$true)][string]$OutputDir,
  [Parameter(Mandatory=$true)][string[]]$EngineRoots,
  [Parameter(Mandatory=$true)][string[]]$EngineVersions
)

function Fail($msg) { Write-Error $msg; exit 1 }

if ($EngineRoots.Count -ne $EngineVersions.Count) {
  Fail "EngineRoots and EngineVersions must have same length."
}

$PluginDir = (Resolve-Path -Path $PluginDir -ErrorAction Stop).Path

$OutputDirRaw = $OutputDir
$resolved = Resolve-Path -LiteralPath $OutputDirRaw -ErrorAction SilentlyContinue
if ($null -eq $resolved) {
  New-Item -ItemType Directory -Path $OutputDirRaw -Force | Out-Null
  $resolved = Resolve-Path -LiteralPath $OutputDirRaw -ErrorAction Stop
}
$OutputDir = $resolved.Path

$uplugin = Get-ChildItem -Path $PluginDir -Filter *.uplugin -Recurse -File | Select-Object -First 1
if (-not $uplugin) { Fail "No .uplugin found under $PluginDir" }
$upluginPath = $uplugin.FullName
$pluginName = Split-Path $uplugin.DirectoryName -Leaf

Write-Host "Plugin: $pluginName"
Write-Host "UPlugin: $upluginPath"
Write-Host "Output: $OutputDir"

# Remove JSON comments using a simple state machine so we don't remove sequences inside string literals.
function Remove-JsonComments([string]$txt) {
  $sb = New-Object System.Text.StringBuilder
  $inString = $false
  $inSingleLineComment = $false
  $inMultiLineComment = $false
  $escape = $false

  for ($i=0; $i -lt $txt.Length; $i++) {
    $ch = $txt[$i]

    if ($inSingleLineComment) {
      if ($ch -eq "`n") {
        $inSingleLineComment = $false
        $sb.Append($ch) | Out-Null
      } else {
        # skip
      }
      continue
    }

    if ($inMultiLineComment) {
      if ($ch -eq '*' -and ($i+1 -lt $txt.Length) -and $txt[$i+1] -eq '/') {
        $inMultiLineComment = $false
        $i++ # skip '/'
      }
      continue
    }

    if ($inString) {
      if (-not $escape -and $ch -eq '"') {
        $inString = $false
        $sb.Append($ch) | Out-Null
        $escape = $false
        continue
      }
      if (-not $escape -and $ch -eq '\') {
        $escape = $true
        $sb.Append($ch) | Out-Null
        continue
      }
      if ($escape) {
        $escape = $false
      }
      $sb.Append($ch) | Out-Null
      continue
    }

    # Not in string or comment
    if ($ch -eq '"') {
      $inString = $true
      $sb.Append($ch) | Out-Null
      continue
    }

    # Check for // (single line)
    if ($ch -eq '/' -and ($i+1 -lt $txt.Length) -and $txt[$i+1] -eq '/') {
      $inSingleLineComment = $true
      $i++ # skip second '/'
      continue
    }

    # Check for /* */ (multi-line)
    if ($ch -eq '/' -and ($i+1 -lt $txt.Length) -and $txt[$i+1] -eq '*') {
      $inMultiLineComment = $true
      $i++ # skip '*'
      continue
    }

    $sb.Append($ch) | Out-Null
  }

  return $sb.ToString()
}

function Set-EngineVersionInUPlugin($jsonPath, $engineVersion) {
  $txt = Get-Content -Raw -Path $jsonPath -ErrorAction Stop

  # Remove comments safely
  $txt2 = Remove-JsonComments $txt

  $data = $null
  try {
    $data = $txt2 | ConvertFrom-Json -Depth 100 -ErrorAction Stop
  } catch {
    $data = $null
  }

  if ($null -ne $data) {
    # Robust approach: build new ordered hashtable(s) from the parsed JSON (object or array),
    # set/ensure EngineVersion and SupportedTargetPlatforms, then write JSON out.
    try {
      if ($data -is [System.Array]) {
        $newArr = @()
        foreach ($el in $data) {
          if ($el -and $el.PSObject -and $el.PSObject.Properties.Count -gt 0) {
            $h = [ordered]@{}
            foreach ($p in $el.PSObject.Properties) { $h[$p.Name] = $p.Value }
            # Ensure EngineVersion is set/updated
            $h['EngineVersion'] = $engineVersion
            # Ensure SupportedTargetPlatforms exists and is non-empty
            if (-not $h.ContainsKey('SupportedTargetPlatforms') -or -not $h['SupportedTargetPlatforms']) {
              $h['SupportedTargetPlatforms'] = @("Win64")
            }
            $newArr += [PSCustomObject]$h
          } else {
            # element is not an object with properties; keep as-is
            $newArr += $el
          }
        }
        $jsonOut = $newArr | ConvertTo-Json -Depth 100 -Compress:$false
        Set-Content -Path $jsonPath -Value $jsonOut -Encoding UTF8
        return
      } else {
        # single object
        $h = [ordered]@{}
        foreach ($p in $data.PSObject.Properties) { $h[$p.Name] = $p.Value }
        # Update EngineVersion
        $h['EngineVersion'] = $engineVersion
        # Ensure SupportedTargetPlatforms exists and is non-empty
        if (-not $h.ContainsKey('SupportedTargetPlatforms') -or -not $h['SupportedTargetPlatforms']) {
          $h['SupportedTargetPlatforms'] = @("Win64")
        }
        $jsonOut = $h | ConvertTo-Json -Depth 100 -Compress:$false
        Set-Content -Path $jsonPath -Value $jsonOut -Encoding UTF8
        return
      }
    } catch {
      # If anything goes wrong updating via object model, fall back to the text-patch below.
      Write-Host "JSON manipulation failed; falling back to text patch. Error: $($_.Exception.Message)"
    }
  }

  # Text patch fallback (careful to only change the EngineVersion property)
  if ($txt -match '"EngineVersion"\s*:') {
    # Replace EngineVersion using a MatchEvaluator to avoid accidental literal $1/$2 in the output
    $pattern = '("EngineVersion"\s*:\s*")([^"]*)(")'
    $evaluator = [System.Text.RegularExpressions.MatchEvaluator]{
      param($m)
      return $m.Groups[1].Value + $engineVersion + $m.Groups[3].Value
    }
    $txt = [System.Text.RegularExpressions.Regex]::Replace($txt, $pattern, $evaluator, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)

    # Ensure SupportedTargetPlatforms exists in text fallback: if not present, insert it after the EngineVersion line.
    if ($txt -notmatch '"SupportedTargetPlatforms"\s*:') {
      $pattern2 = '("EngineVersion"\s*:\s*"[^\"]*"\s*,?)'
      $evaluator2 = [System.Text.RegularExpressions.MatchEvaluator]{
        param($m)
        return $m.Groups[1].Value + "`n  `"SupportedTargetPlatforms`": [ `"Win64`" ],"
      }
      $txt = [System.Text.RegularExpressions.Regex]::Replace($txt, $pattern2, $evaluator2, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
    }
  } else {
    # The source descriptors have no EngineVersion (a 5.6.0 there makes newer editors refuse to load
    # the plugin from source), so this is the usual path: insert it after the opening brace, with
    # SupportedTargetPlatforms unless the descriptor already lists them.
    $insert = "{`n  `"EngineVersion`": `"$engineVersion`","
    if ($txt -notmatch '"SupportedTargetPlatforms"\s*:') {
      $insert += "`n  `"SupportedTargetPlatforms`": [ `"Win64`" ],"
    }
    $txt = $txt -replace '^\s*\{', $insert
  }
  # Write out the patched file
  Set-Content -Value $txt -Path $jsonPath -Encoding UTF8
}

# Keep UnrealBuildTool off IncrediBuild (XGE), which on the build machine intermittently refuses
# with "Maximum number of concurrent builds reached". BuildPlugin passes no -NoXGE or -ubtargs to
# UnrealBuildTool, but UnrealBuildTool reads UnrealBuildTool_<Category>__<Field> environment
# variables as BuildConfiguration.xml settings (XmlConfig.cs, UE 5.6 to 5.8).
# Restored at the end, so a developer's shell isn't left building without XGE.
$PreviousAllowXGE = $env:UnrealBuildTool_BuildConfiguration__bAllowXGE
$env:UnrealBuildTool_BuildConfiguration__bAllowXGE = 'false'

try {
for ($i = 0; $i -lt $EngineRoots.Count; $i++) {
  $root = (Resolve-Path -Path $EngineRoots[$i] -ErrorAction Stop).Path
  $ver = $EngineVersions[$i]
  $uat = Join-Path $root "Engine\Build\BatchFiles\RunUAT.bat"
  if (-not (Test-Path -Path $uat)) { Fail "RunUAT not found at $uat" }
  
  $stage = Join-Path $OutputDir "$pluginName-UE$($ver.Replace('.','_'))-Stage"
  if (Test-Path -Path $stage) { Remove-Item -Recurse -Force -Path $stage }
  New-Item -ItemType Directory -Path $stage -Force | Out-Null

  # Copy plugin tree to stage and set EngineVersion
  Copy-Item -Recurse -Force -Path $PluginDir -Destination $stage
  $stagePluginDir = Join-Path $stage $pluginName
  $stageUPlugin = Get-ChildItem -Path $stagePluginDir -Filter *.uplugin -Recurse -File | Select-Object -First 1
  if (-not $stageUPlugin) { Fail "Stage .uplugin missing in $stagePluginDir" }
  Set-EngineVersionInUPlugin -jsonPath $stageUPlugin.FullName -engineVersion $ver

  # BuildPlugin with UAT (the packaged folder includes Binaries and Intermediate)
  $packageDir = Join-Path $OutputDir "$pluginName-UE$($ver.Replace('.','_'))-Packaged"
  if (Test-Path -Path $packageDir) { Remove-Item -Recurse -Force -Path $packageDir }
  & $uat BuildPlugin -Plugin="$($stageUPlugin.FullName)" -Package="$packageDir" -Rocket -VeryVerbose
  if ($LASTEXITCODE -ne 0) { Fail "BuildPlugin failed for UE $ver" }

  # Zip
  $verParts = $ver.Split('.')
  $zipPath = Join-Path $OutputDir ("$pluginName" + "_UE" + $verParts[0] + "_" + $verParts[1] + "_Fab.zip")
  if (Test-Path -Path $zipPath) { Remove-Item -Force -Path $zipPath }
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  [System.IO.Compression.ZipFile]::CreateFromDirectory($packageDir, $zipPath)

  Write-Host "Packaged: $zipPath"
}

} finally {
  if ($null -eq $PreviousAllowXGE) { Remove-Item Env:\UnrealBuildTool_BuildConfiguration__bAllowXGE -ErrorAction SilentlyContinue }
  else { $env:UnrealBuildTool_BuildConfiguration__bAllowXGE = $PreviousAllowXGE }
}

Write-Host "Done."
