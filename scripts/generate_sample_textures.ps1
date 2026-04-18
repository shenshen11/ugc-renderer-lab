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

$metallicRoughnessPath = Join-Path $outputDirectory "metallic_roughness_mask.png"
Save-Texture -Path $metallicRoughnessPath -ColorFunc {
    param($x, $y)

    $roughness = [int](40 + ($x * 215 / 127))
    $metallic = [int](30 + ($y * 225 / 127))
    return [System.Drawing.Color]::FromArgb(255, 0, $roughness, $metallic)
}

$occlusionPath = Join-Path $outputDirectory "occlusion_mask.png"
Save-Texture -Path $occlusionPath -ColorFunc {
    param($x, $y)

    $dx = ($x - 63.5) / 63.5
    $dy = ($y - 63.5) / 63.5
    $distance = [Math]::Sqrt($dx * $dx + $dy * $dy)
    $occlusion = [int]([Math]::Max(55, 255 - ($distance * 180)))
    return [System.Drawing.Color]::FromArgb(255, $occlusion, 0, 0)
}

$emissivePath = Join-Path $outputDirectory "emissive_mask.png"
Save-Texture -Path $emissivePath -ColorFunc {
    param($x, $y)

    $distanceToDiagonal = [Math]::Abs($x - $y)
    $intensity = [int]([Math]::Max(0, 255 - ($distanceToDiagonal * 8)))
    $warm = [int]([Math]::Min(255, 80 + ($intensity * 0.7)))
    return [System.Drawing.Color]::FromArgb(255, $intensity, $warm, 40)
}

$normalPath = Join-Path $outputDirectory "normal_detail.png"
Save-Texture -Path $normalPath -ColorFunc {
    param($x, $y)

    $u = $x / 127.0
    $v = $y / 127.0
    $heightDx = [Math]::Cos($u * [Math]::PI * 6.0) * [Math]::Sin($v * [Math]::PI * 4.0)
    $heightDy = [Math]::Sin($u * [Math]::PI * 6.0) * [Math]::Cos($v * [Math]::PI * 4.0)
    $nx = -$heightDx * 0.55
    $ny = -$heightDy * 0.55
    $nz = 1.0
    $length = [Math]::Sqrt($nx * $nx + $ny * $ny + $nz * $nz)
    $nx /= $length
    $ny /= $length
    $nz /= $length

    $red = [int](($nx * 0.5 + 0.5) * 255.0)
    $green = [int](($ny * 0.5 + 0.5) * 255.0)
    $blue = [int](($nz * 0.5 + 0.5) * 255.0)
    return [System.Drawing.Color]::FromArgb(255, $red, $green, $blue)
}

Write-Host "Generated sample textures in $outputDirectory"
