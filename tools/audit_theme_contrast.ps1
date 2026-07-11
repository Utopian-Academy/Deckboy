# SPDX-License-Identifier: GPL-3.0-or-later
# audit_theme_contrast.ps1 â€” Check every theme's ink/fill pairs for WCAG-style
# contrast. Readability is a THEME-DATA contract (see palette.hpp): the UI
# draws these role pairs, and each must clear its minimum ratio. Run after
# editing any theme.txt; fix the theme, not the renderer.
#
# Usage: .\tools\audit_theme_contrast.ps1 [-ThemesDir data\themes]

param([string]$ThemesDir = "$PSScriptRoot\..\data\themes")

function Get-Lum([string]$hex) {
    $r = [Convert]::ToInt32($hex.Substring(0, 2), 16) / 255.0
    $g = [Convert]::ToInt32($hex.Substring(2, 2), 16) / 255.0
    $b = [Convert]::ToInt32($hex.Substring(4, 2), 16) / 255.0
    $lin = { param($v) if ($v -le 0.04045) { $v / 12.92 } else { [Math]::Pow(($v + 0.055) / 1.055, 2.4) } }
    return 0.2126 * (& $lin $r) + 0.7152 * (& $lin $g) + 0.0722 * (& $lin $b)
}

function Get-Contrast([string]$a, [string]$b) {
    $la = Get-Lum $a; $lb = Get-Lum $b
    $hi = [Math]::Max($la, $lb); $lo = [Math]::Min($la, $lb)
    return ($hi + 0.05) / ($lo + 0.05)
}

# (ink role, fill role, minimum ratio) â€” the pairs the UI actually draws.
$pairs = @(
    @("screen_light",    "screen_deep",  4.5, "live-row text / panel text"),
    @("screen_dark",     "screen_light", 3.0, "row subtext on row fill"),
    @("screen_deep",     "screen_light", 4.5, "row text on row fill"),
    @("screen_deep",     "shell_inner",  3.0, "dialog + idle-strip text"),
    @("screen_dark",     "shell_inner",  2.5, "dialog secondary text"),
    @("screen_ink_soft", "shell_inner",  2.0, "dialog hints"),
    @("screen_ink_soft", "screen_deep",  2.5, "panel secondary text"),
    @("screen_deep",     "screen_mid",   2.5, "selected-row text"),
    @("screen_light",    "screen_dark",  2.5, "button text on dark fills")
)

$anyFail = $false
Get-ChildItem $ThemesDir -Directory | Sort-Object Name | ForEach-Object {
    $file = Join-Path $_.FullName "theme.txt"
    if (-not (Test-Path $file)) { return }
    $colors = @{}
    Get-Content $file | ForEach-Object {
        if ($_ -match "^([a-z_]+)\t([0-9A-Fa-f]{8})") { $colors[$Matches[1]] = $Matches[2] }
    }
    $fails = @()
    foreach ($p in $pairs) {
        $ink = $colors[$p[0]]; $fill = $colors[$p[1]]
        if (-not $ink -or -not $fill) { continue }
        $ratio = Get-Contrast $ink $fill
        if ($ratio -lt $p[2]) {
            $fails += ("  {0} on {1}: {2:N2} (need {3})  [{4}]" -f $p[0], $p[1], $ratio, $p[2], $p[3])
        }
    }
    if ($fails.Count -gt 0) {
        $script:anyFail = $true
        Write-Host ("{0}:" -f $_.Name)
        $fails | ForEach-Object { Write-Host $_ }
    }
}
if (-not $anyFail) { Write-Host "all themes pass" }