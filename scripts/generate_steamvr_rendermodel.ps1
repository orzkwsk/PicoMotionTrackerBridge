param(
    [Parameter(Mandatory = $true)]
    [string]$DriverPackageDir
)

$ErrorActionPreference = "Stop"

function Write-TextureAtlas {
    param([string]$Path)
    # 48x8 RGBA PNG. Six 8-pixel-wide swatches:
    # body white, button light grey, LED blue, USB dark, trim grey, logo grey.
    $atlasBase64 = "iVBORw0KGgoAAAANSUhEUgAAADAAAAAICAYAAAC/K3xHAAAAO0lEQVR42mN89+7lfwY84NXrN/ikGeKmaeGVf7tVA698d287XnkJCQm88kwMQxyMemDUA6MeGPXAwAIALUALZBA7GDIAAAAASUVORK5CYII="
    [IO.File]::WriteAllBytes($Path, [Convert]::FromBase64String($atlasBase64))
}

$ModelName = "pico_motion_tracker"
$ModelDir = Join-Path $DriverPackageDir "rendermodels\$ModelName"
New-Item -ItemType Directory -Force -Path $ModelDir | Out-Null

# Approximate PICO Motion Tracker core dimensions.
# Geometry units are meters. Local frame:
#   -Z = logo/front face
#   +Y = button + LED / up
#   -X = USB-C / left
$segments = 40
$frontCenterZ = -0.00825
$rearCenterZ = 0.00745
$profile = @(
    @(0.00000, $frontCenterZ),
    @(0.01000, -0.00800),
    @(0.01630, -0.00710),
    @(0.01840, -0.00550),
    @(0.01925, -0.00300),
    @(0.01925,  0.00300),
    @(0.01840,  0.00540),
    @(0.01600,  0.00680),
    @(0.00900,  0.00730),
    @(0.00000, $rearCenterZ)
)

$vertices = New-Object System.Collections.Generic.List[object]
$normals = New-Object System.Collections.Generic.List[object]
$faces = New-Object System.Collections.Generic.List[object]

function Add-Vertex {
    param([double]$X, [double]$Y, [double]$Z,
          [double]$Nx, [double]$Ny, [double]$Nz)
    $script:vertices.Add(@($X, $Y, $Z))
    $script:normals.Add(@($Nx, $Ny, $Nz))
    return $script:vertices.Count
}

function Add-Face {
    param([int]$A, [int]$B, [int]$C, [string]$Material)
    $script:faces.Add([pscustomobject]@{ A=$A; B=$B; C=$C; Material=$Material })
}

function Add-Quad {
    param(
        [double[]]$P0,
        [double[]]$P1,
        [double[]]$P2,
        [double[]]$P3,
        [double[]]$Normal,
        [string]$Material
    )
    $i0 = Add-Vertex $P0[0] $P0[1] $P0[2] $Normal[0] $Normal[1] $Normal[2]
    $i1 = Add-Vertex $P1[0] $P1[1] $P1[2] $Normal[0] $Normal[1] $Normal[2]
    $i2 = Add-Vertex $P2[0] $P2[1] $P2[2] $Normal[0] $Normal[1] $Normal[2]
    $i3 = Add-Vertex $P3[0] $P3[1] $P3[2] $Normal[0] $Normal[1] $Normal[2]
    Add-Face $i0 $i1 $i2 $Material
    Add-Face $i0 $i2 $i3 $Material
}

$frontCenter = Add-Vertex 0.0 0.0 $frontCenterZ 0.0 0.0 -1.0
$rings = @()
for ($k = 1; $k -lt ($profile.Count - 1); $k++) {
    $r = [double]$profile[$k][0]
    $z = [double]$profile[$k][1]
    $prevR = [double]$profile[$k - 1][0]
    $prevZ = [double]$profile[$k - 1][1]
    $nextR = [double]$profile[$k + 1][0]
    $nextZ = [double]$profile[$k + 1][1]
    $dr = $nextR - $prevR
    $dz = $nextZ - $prevZ

    $ring = @()
    for ($i = 0; $i -lt $segments; $i++) {
        $theta = 2.0 * [Math]::PI * $i / $segments
        $c = [Math]::Cos($theta)
        $s = [Math]::Sin($theta)
        $nx = $dz * $c
        $ny = $dz * $s
        $nz = -$dr
        $len = [Math]::Sqrt($nx*$nx + $ny*$ny + $nz*$nz)
        $ring += Add-Vertex ($r*$c) ($r*$s) $z ($nx/$len) ($ny/$len) ($nz/$len)
    }
    $rings += ,$ring
}
$rearCenter = Add-Vertex 0.0 0.0 $rearCenterZ 0.0 0.0 1.0

for ($i = 0; $i -lt $segments; $i++) {
    $j = ($i + 1) % $segments
    Add-Face $frontCenter $rings[0][$j] $rings[0][$i] "Body"
}
for ($k = 0; $k -lt ($rings.Count - 1); $k++) {
    $a = $rings[$k]
    $b = $rings[$k + 1]
    for ($i = 0; $i -lt $segments; $i++) {
        $j = ($i + 1) % $segments
        Add-Face $a[$i] $a[$j] $b[$j] "Body"
        Add-Face $a[$i] $b[$j] $b[$i] "Body"
    }
}
for ($i = 0; $i -lt $segments; $i++) {
    $j = ($i + 1) % $segments
    Add-Face $rings[$rings.Count - 1][$i] $rings[$rings.Count - 1][$j] $rearCenter "Body"
}

# Button / LED on +Y and USB-C opening on -X.
Add-Quad @(-0.0042,0.01940,-0.0018) @(0.0042,0.01940,-0.0018) @(0.0042,0.01940,0.0018) @(-0.0042,0.01940,0.0018) @(0.0,1.0,0.0) "Button"
Add-Quad @(0.0061,0.01945,-0.00065) @(0.0075,0.01945,-0.00065) @(0.0075,0.01945,0.00065) @(0.0061,0.01945,0.00065) @(0.0,1.0,0.0) "Led"
Add-Quad @(-0.01945,-0.0048,-0.00145) @(-0.01945,-0.0048,0.00145) @(-0.01945,0.0048,0.00145) @(-0.01945,0.0048,-0.00145) @(-1.0,0.0,0.0) "Port"

# Lightweight PICO-like front marking, intentionally not copied vendor artwork.
$logoZ = -0.00831
function Add-LogoRect {
    param([double]$X0, [double]$Y0, [double]$X1, [double]$Y1)
    Add-Quad @($X0,$Y1,$logoZ) @($X1,$Y1,$logoZ) @($X1,$Y0,$logoZ) @($X0,$Y0,$logoZ) @(0.0,0.0,-1.0) "Logo"
}
$h = 0.0032
$sw = 0.00048
$y0 = -$h/2.0
$y1 =  $h/2.0
$x = -0.0064
Add-LogoRect $x $y0 ($x+$sw) $y1; Add-LogoRect $x ($y1-$sw) ($x+0.0022) $y1; Add-LogoRect ($x+0.00175) 0.0 ($x+0.0022) $y1; Add-LogoRect $x (-$sw/2.0) ($x+0.0022) ($sw/2.0)
$x = -0.0027
Add-LogoRect $x $y0 ($x+$sw) $y1
$x = -0.0008
Add-LogoRect $x $y0 ($x+$sw) $y1; Add-LogoRect $x ($y1-$sw) ($x+0.0022) $y1; Add-LogoRect $x $y0 ($x+0.0022) ($y0+$sw)
$x = 0.0027
Add-LogoRect $x $y0 ($x+$sw) $y1; Add-LogoRect ($x+0.00175) $y0 ($x+0.0022) $y1; Add-LogoRect $x ($y1-$sw) ($x+0.0022) $y1; Add-LogoRect $x $y0 ($x+0.0022) ($y0+$sw)

$materialUv = @{ Body=0.0833333; Button=0.2500000; Led=0.4166667; Port=0.5833333; Trim=0.7500000; Logo=0.9166667 }
$materialUvIndex = @{ Body=1; Button=2; Led=3; Port=4; Trim=5; Logo=6 }
$ci = [Globalization.CultureInfo]::InvariantCulture
$obj = New-Object Text.StringBuilder
[void]$obj.AppendLine("# Approximate PICO Motion Tracker core render model")
[void]$obj.AppendLine("# Units: meters. -Z front/logo, +Y button/up, -X USB-C/left.")
[void]$obj.AppendLine("mtllib pico_motion_tracker.mtl")
[void]$obj.AppendLine("o pico_motion_tracker")
foreach ($v in $vertices) { [void]$obj.AppendLine([string]::Format($ci,"v {0:F7} {1:F7} {2:F7}",$v[0],$v[1],$v[2])) }
foreach ($name in @("Body","Button","Led","Port","Trim","Logo")) { [void]$obj.AppendLine([string]::Format($ci,"vt {0:F7} 0.5000000",$materialUv[$name])) }
foreach ($n in $normals) { [void]$obj.AppendLine([string]::Format($ci,"vn {0:F7} {1:F7} {2:F7}",$n[0],$n[1],$n[2])) }
$currentMaterial = ""
foreach ($f in $faces) {
    if ($f.Material -ne $currentMaterial) { [void]$obj.AppendLine("usemtl $($f.Material)"); $currentMaterial = $f.Material }
    $t = $materialUvIndex[$f.Material]
    [void]$obj.AppendLine("f $($f.A)/$t/$($f.A) $($f.B)/$t/$($f.B) $($f.C)/$t/$($f.C)")
}

$mtl = @"
newmtl Body
Ka 1 1 1
Kd 1 1 1
Ks 0.12 0.12 0.12
Ns 32
illum 2
map_Kd pico_motion_tracker.png
newmtl Button
Ka 1 1 1
Kd 1 1 1
illum 2
map_Kd pico_motion_tracker.png
newmtl Led
Ka 1 1 1
Kd 1 1 1
illum 2
map_Kd pico_motion_tracker.png
newmtl Port
Ka 1 1 1
Kd 1 1 1
illum 2
map_Kd pico_motion_tracker.png
newmtl Trim
Ka 1 1 1
Kd 1 1 1
illum 2
map_Kd pico_motion_tracker.png
newmtl Logo
Ka 1 1 1
Kd 1 1 1
illum 2
map_Kd pico_motion_tracker.png
"@

$ObjPath = Join-Path $ModelDir "$ModelName.obj"
$MtlPath = Join-Path $ModelDir "$ModelName.mtl"
$PngPath = Join-Path $ModelDir "$ModelName.png"
[IO.File]::WriteAllText($ObjPath, $obj.ToString(), [Text.Encoding]::ASCII)
[IO.File]::WriteAllText($MtlPath, $mtl, [Text.Encoding]::ASCII)
Write-TextureAtlas $PngPath

# Hidden state uses a microscopic valid render model instead of removing the
# render-model property. This avoids SteamVR falling back to generic_tracker.
$HiddenName = "pico_motion_tracker_hidden"
$HiddenDir = Join-Path $DriverPackageDir "rendermodels\$HiddenName"
New-Item -ItemType Directory -Force -Path $HiddenDir | Out-Null
$hiddenObj = @"
mtllib pico_motion_tracker_hidden.mtl
o pico_motion_tracker_hidden
v 0.0000000 0.0000000 0.0000000
v 0.0000010 0.0000000 0.0000000
v 0.0000000 0.0000010 0.0000000
vt 0.5000000 0.5000000
vn 0.0000000 0.0000000 1.0000000
usemtl Hidden
f 1/1/1 2/1/1 3/1/1
"@
$hiddenMtl = @"
newmtl Hidden
Ka 0 0 0
Kd 0 0 0
Ks 0 0 0
Ns 1
illum 1
map_Kd pico_motion_tracker_hidden.png
"@
[IO.File]::WriteAllText((Join-Path $HiddenDir "$HiddenName.obj"), $hiddenObj, [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $HiddenDir "$HiddenName.mtl"), $hiddenMtl, [Text.Encoding]::ASCII)
Write-TextureAtlas (Join-Path $HiddenDir "$HiddenName.png")

Write-Host "Generated SteamVR render models:"
Write-Host "  Visible: $ModelDir"
Write-Host "  Hidden : $HiddenDir"
