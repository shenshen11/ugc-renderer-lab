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
        [Parameter()]
        [int]$Width = 128,
        [Parameter()]
        [int]$Height = 128,
        [Parameter(Mandatory = $true)]
        [scriptblock]$ColorFunc
    )

    $bitmap = New-Object System.Drawing.Bitmap $Width, $Height
    try {
        for ($y = 0; $y -lt $Height; $y++) {
            for ($x = 0; $x -lt $Width; $x++) {
                $color = & $ColorFunc $x $y $Width $Height
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
    param($x, $y, $width, $height)

    $cell = (([int]($x / 16)) + ([int]($y / 16))) % 2
    if ($cell -eq 0) {
        return [System.Drawing.Color]::FromArgb(255, 245, 245, 245)
    }

    return [System.Drawing.Color]::FromArgb(255, 35, 35, 42)
}

$gradientPath = Join-Path $outputDirectory "gradient_stripes.png"
Save-Texture -Path $gradientPath -ColorFunc {
    param($x, $y, $width, $height)

    $red = [int](80 + ($x * 175 / 127))
    $green = [int](60 + ($y * 155 / 127))
    $isStripe = (([int](($x + $y) / 12)) % 2) -eq 0
    $blue = if ($isStripe) { 245 } else { 120 }

    return [System.Drawing.Color]::FromArgb(255, $red, $green, $blue)
}

$alphaCutoutPath = Join-Path $outputDirectory "alpha_cutout_stripes.png"
Save-Texture -Path $alphaCutoutPath -ColorFunc {
    param($x, $y, $width, $height)

    $red = [int](95 + ($x * 150 / 127))
    $green = [int](90 + ($y * 125 / 127))
    $isStripe = (([int](($x + $y) / 10)) % 2) -eq 0
    $blue = if ($isStripe) { 255 } else { 145 }
    $cellX = $x % 32
    $cellY = $y % 32
    $dx = $cellX - 15.5
    $dy = $cellY - 15.5
    $isHole = (($dx * $dx) + ($dy * $dy)) -lt 70.0
    $alpha = if ($isHole) { 0 } else { 255 }

    return [System.Drawing.Color]::FromArgb($alpha, $red, $green, $blue)
}

$alphaBlendPath = Join-Path $outputDirectory "alpha_blend_soft_panel.png"
Save-Texture -Path $alphaBlendPath -ColorFunc {
    param($x, $y, $width, $height)

    $u = $x / ([Math]::Max(1, $width - 1))
    $v = $y / ([Math]::Max(1, $height - 1))
    $centeredU = ($u - 0.5) * 2.0
    $centeredV = ($v - 0.5) * 2.0
    $radius = [Math]::Sqrt($centeredU * $centeredU + $centeredV * $centeredV)
    $softEdge = [Math]::Max(0.0, 1.0 - ($radius * 0.82))
    $verticalBand = 0.35 + 0.65 * [Math]::Pow([Math]::Sin($u * [Math]::PI), 2.0)
    $diagonalRibbon = 0.45 + 0.55 * [Math]::Pow([Math]::Cos(($u + $v) * [Math]::PI * 1.5), 2.0)
    $alphaValue = [Math]::Min(1.0, [Math]::Max(0.08, $softEdge * 0.7 + $verticalBand * 0.18 + $diagonalRibbon * 0.12))
    $alpha = [int]($alphaValue * 255.0)
    $luminance = [int](210 + 35 * [Math]::Sin($v * [Math]::PI))

    return [System.Drawing.Color]::FromArgb($alpha, $luminance, $luminance, $luminance)
}

$metallicRoughnessPath = Join-Path $outputDirectory "metallic_roughness_mask.png"
Save-Texture -Path $metallicRoughnessPath -ColorFunc {
    param($x, $y, $width, $height)

    $roughness = [int](40 + ($x * 215 / 127))
    $metallic = [int](30 + ($y * 225 / 127))
    return [System.Drawing.Color]::FromArgb(255, 0, $roughness, $metallic)
}

$occlusionPath = Join-Path $outputDirectory "occlusion_mask.png"
Save-Texture -Path $occlusionPath -ColorFunc {
    param($x, $y, $width, $height)

    $dx = ($x - 63.5) / 63.5
    $dy = ($y - 63.5) / 63.5
    $distance = [Math]::Sqrt($dx * $dx + $dy * $dy)
    $occlusion = [int]([Math]::Max(55, 255 - ($distance * 180)))
    return [System.Drawing.Color]::FromArgb(255, $occlusion, 0, 0)
}

$emissivePath = Join-Path $outputDirectory "emissive_mask.png"
Save-Texture -Path $emissivePath -ColorFunc {
    param($x, $y, $width, $height)

    $distanceToDiagonal = [Math]::Abs($x - $y)
    $intensity = [int]([Math]::Max(0, 255 - ($distanceToDiagonal * 8)))
    $warm = [int]([Math]::Min(255, 80 + ($intensity * 0.7)))
    return [System.Drawing.Color]::FromArgb(255, $intensity, $warm, 40)
}

$normalPath = Join-Path $outputDirectory "normal_detail.png"
Save-Texture -Path $normalPath -ColorFunc {
    param($x, $y, $width, $height)

    $u = $x / ([Math]::Max(1, $width - 1))
    $v = $y / ([Math]::Max(1, $height - 1))
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

$environmentPath = Join-Path $outputDirectory "environment_panorama.png"
Save-Texture -Path $environmentPath -Width 1024 -Height 512 -ColorFunc {
    param($x, $y, $width, $height)

    $u = $x / ([Math]::Max(1, $width - 1))
    $v = $y / ([Math]::Max(1, $height - 1))
    $elevation = 1.0 - $v
    $skyLerp = [Math]::Pow([Math]::Max(0.0, [Math]::Min(1.0, ($elevation - 0.28) / 0.72)), 0.8)
    $groundLerp = [Math]::Pow([Math]::Max(0.0, [Math]::Min(1.0, (0.34 - $elevation) / 0.34)), 0.9)
    $horizonWeight = [Math]::Pow([Math]::Max(0.0, 1.0 - [Math]::Abs($elevation - 0.36) / 0.16), 1.5)

    $skyR = 38 + 32 * $skyLerp
    $skyG = 64 + 74 * $skyLerp
    $skyB = 112 + 128 * $skyLerp

    $groundR = 44 + 36 * $groundLerp
    $groundG = 30 + 32 * $groundLerp
    $groundB = 18 + 20 * $groundLerp

    $blend = [Math]::Max(0.0, [Math]::Min(1.0, ($elevation - 0.18) / 0.32))
    $red = $groundR * (1.0 - $blend) + $skyR * $blend
    $green = $groundG * (1.0 - $blend) + $skyG * $blend
    $blue = $groundB * (1.0 - $blend) + $skyB * $blend

    $red += 70 * $horizonWeight
    $green += 48 * $horizonWeight
    $blue += 18 * $horizonWeight

    $sunU = 0.18
    $sunV = 0.24
    $dx = [Math]::Min([Math]::Abs($u - $sunU), 1.0 - [Math]::Abs($u - $sunU))
    $dy = $v - $sunV
    $sunDistance = [Math]::Sqrt($dx * $dx * 14.0 + $dy * $dy * 38.0)
    $sunGlow = [Math]::Pow([Math]::Max(0.0, 1.0 - $sunDistance), 3.6)
    $red += 255 * $sunGlow
    $green += 214 * $sunGlow
    $blue += 132 * $sunGlow

    $cloudBand = 0.5 + 0.5 * [Math]::Sin($u * [Math]::PI * 7.0 + $v * [Math]::PI * 1.6)
    $clouds = [Math]::Pow($cloudBand, 3.0) * [Math]::Pow([Math]::Max(0.0, $elevation - 0.34), 0.45)
    $red += 38 * $clouds
    $green += 42 * $clouds
    $blue += 46 * $clouds

    $red = [int][Math]::Max(0, [Math]::Min(255, $red))
    $green = [int][Math]::Max(0, [Math]::Min(255, $green))
    $blue = [int][Math]::Max(0, [Math]::Min(255, $blue))
    return [System.Drawing.Color]::FromArgb(255, $red, $green, $blue)
}

Write-Host "Generated sample textures in $outputDirectory"
