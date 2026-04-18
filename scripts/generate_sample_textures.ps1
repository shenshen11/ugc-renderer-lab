[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $projectRoot "assets\\textures"
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

function Save-Texture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [scriptblock]$ColorFunc
    )

    $bitmap = New-Object System.Drawing.Bitmap 128, 128
    try {
        for ($y = 0; $y -lt 128; $y++) {
            for ($x = 0; $x -lt 128; $x++) {
                $color = & $ColorFunc $x $y
                $bitmap.SetPixel($x, $y, $color)
            }
        }

        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $bitmap.Dispose()
    }
}

$checkerboardPath = Join-Path $outputDirectory "checkerboard.png"
Save-Texture -Path $checkerboardPath -ColorFunc {
    param($x, $y)

    $cell = (([int]($x / 16)) + ([int]($y / 16))) % 2
    if ($cell -eq 0) {
        return [System.Drawing.Color]::FromArgb(255, 245, 245, 245)
    }

    return [System.Drawing.Color]::FromArgb(255, 35, 35, 42)
}

$gradientPath = Join-Path $outputDirectory "gradient_stripes.png"
Save-Texture -Path $gradientPath -ColorFunc {
    param($x, $y)

    $red = [int](80 + ($x * 175 / 127))
    $green = [int](60 + ($y * 155 / 127))
    $isStripe = (([int](($x + $y) / 12)) % 2) -eq 0
    $blue = if ($isStripe) { 245 } else { 120 }

    return [System.Drawing.Color]::FromArgb(255, $red, $green, $blue)
}

Write-Host "Generated sample textures in $outputDirectory"
