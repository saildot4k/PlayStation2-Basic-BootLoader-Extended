param(
    [Parameter(Mandatory = $true)][string]$FontPath,
    [Parameter(Mandatory = $true)][int]$Size,
    [Parameter(Mandatory = $true)][string]$OutC,
    [Parameter(Mandatory = $true)][string]$OutH,
    [int]$Style = 0,
    [int]$AtlasWidth = 512,
    [string]$Stamp = ""
)

Add-Type -AssemblyName System.Drawing

$signature = "$FontPath|$Size|$Style|$AtlasWidth"
if ($Stamp -ne '') {
    if ((Test-Path -LiteralPath $Stamp) -and (Test-Path -LiteralPath $OutC) -and (Test-Path -LiteralPath $OutH)) {
        $prev = (Get-Content -LiteralPath $Stamp -Raw).Trim()
        if ($prev -eq $signature) {
            $fontTime = (Get-Item -LiteralPath $FontPath).LastWriteTimeUtc
            $outTime = (Get-Item -LiteralPath $OutC).LastWriteTimeUtc
            if ($outTime -ge $fontTime) {
                return
            }
        }
    }
}

$fonts = New-Object System.Drawing.Text.PrivateFontCollection
$fonts.AddFontFile($FontPath)
if ($fonts.Families.Count -lt 1) {
    throw "No font families found in $FontPath"
}

$fontStyle = if ($Style -ne 0) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular }
$family = $fonts.Families[0]
$font = New-Object System.Drawing.Font($family, $Size, $fontStyle, [System.Drawing.GraphicsUnit]::Pixel)

$em = $family.GetEmHeight($fontStyle)
$ascent = $family.GetCellAscent($fontStyle)
$descent = $family.GetCellDescent($fontStyle)
$lineSpacing = $family.GetLineSpacing($fontStyle)
$baseline = [math]::Ceiling($Size * $ascent / $em)
$lineHeight = [math]::Ceiling($Size * $lineSpacing / $em)
if ($lineHeight -lt ($baseline + [math]::Ceiling($Size * $descent / $em))) {
    $lineHeight = $baseline + [math]::Ceiling($Size * $descent / $em)
}

$tmpBmp = New-Object System.Drawing.Bitmap(1, 1)
$gfx = [System.Drawing.Graphics]::FromImage($tmpBmp)
$gfx.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$fmt = New-Object System.Drawing.StringFormat([System.Drawing.StringFormat]::GenericTypographic)
$fmt.FormatFlags = $fmt.FormatFlags -bor [System.Drawing.StringFormatFlags]::MeasureTrailingSpaces

$first = 32
$last = 126
$glyphs = @()

for ($code = $first; $code -le $last; $code++) {
    $ch = [char]$code
    $sizeF = $gfx.MeasureString($ch, $font, [System.Drawing.PointF]::Empty, $fmt)
    $adv = [math]::Ceiling($sizeF.Width)
    if ($adv -lt 1) { $adv = 1 }

    $tmpW = $adv + 4
    $tmpH = $lineHeight + 4
    $bmp = New-Object System.Drawing.Bitmap($tmpW, $tmpH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.DrawString($ch, $font, [System.Drawing.Brushes]::White, 0, 0, $fmt)

    $minX = $tmpW
    $minY = $tmpH
    $maxX = -1
    $maxY = -1
    for ($y = 0; $y -lt $tmpH; $y++) {
        for ($x = 0; $x -lt $tmpW; $x++) {
            $a = $bmp.GetPixel($x, $y).A
            if ($a -gt 0) {
                if ($x -lt $minX) { $minX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }

    $w = 0
    $h = 0
    $xoff = 0
    $yoff = 0
    $pixels = New-Object byte[] 0
    if ($maxX -ge $minX -and $maxY -ge $minY) {
        $w = $maxX - $minX + 1
        $h = $maxY - $minY + 1
        $xoff = $minX
        $yoff = $minY - $baseline
        if ($adv -lt ($xoff + $w)) { $adv = $xoff + $w }
        $pixels = New-Object byte[] ($w * $h * 4)
        for ($gy = 0; $gy -lt $h; $gy++) {
            for ($gx = 0; $gx -lt $w; $gx++) {
                $color = $bmp.GetPixel($minX + $gx, $minY + $gy)
                $idx = ($gy * $w + $gx) * 4
                $pixels[$idx] = 255
                $pixels[$idx + 1] = 255
                $pixels[$idx + 2] = 255
                $pixels[$idx + 3] = $color.A
            }
        }
    }

    $glyphs += [pscustomobject]@{
        Code = $code
        Adv = [int]$adv
        XOff = [int]$xoff
        YOff = [int]$yoff
        W = [int]$w
        H = [int]$h
        Pixels = $pixels
        X = 0
        Y = 0
    }

    $g.Dispose()
    $bmp.Dispose()
}

$gfx.Dispose()
$tmpBmp.Dispose()

$padding = 1
$x = 0
$y = 0
$rowH = 0
foreach ($glyph in $glyphs) {
    if ($glyph.W -le 0 -or $glyph.H -le 0) {
        $glyph.X = 0
        $glyph.Y = 0
        continue
    }
    if ($x + $glyph.W + $padding -gt $AtlasWidth) {
        $x = 0
        $y += $rowH + $padding
        $rowH = 0
    }
    $glyph.X = $x
    $glyph.Y = $y
    $x += $glyph.W + $padding
    if ($glyph.H -gt $rowH) { $rowH = $glyph.H }
}
$atlasHeight = $y + $rowH
if ($atlasHeight -lt 1) { $atlasHeight = 1 }

$atlas = New-Object byte[] ($AtlasWidth * $atlasHeight * 4)
foreach ($glyph in $glyphs) {
    if ($glyph.W -le 0 -or $glyph.H -le 0) { continue }
    $rowBytes = $glyph.W * 4
    for ($gy = 0; $gy -lt $glyph.H; $gy++) {
        $srcIndex = $gy * $rowBytes
        $dstIndex = (($glyph.Y + $gy) * $AtlasWidth + $glyph.X) * 4
        [System.Buffer]::BlockCopy($glyph.Pixels, $srcIndex, $atlas, $dstIndex, $rowBytes)
    }
}

$header = New-Object System.IO.StreamWriter($OutH, $false, [System.Text.Encoding]::ASCII)
$header.WriteLine('#ifndef SPLASH_FONT_H')
$header.WriteLine('#define SPLASH_FONT_H')
$header.WriteLine('')
$header.WriteLine('#include <stdint.h>')
$header.WriteLine('')
$header.WriteLine('#define SPLASH_FONT_FIRST 32')
$header.WriteLine('#define SPLASH_FONT_LAST 126')
$header.WriteLine('#define SPLASH_FONT_COUNT 95')
$header.WriteLine('')
$header.WriteLine('typedef struct')
$header.WriteLine('{')
$header.WriteLine('    int16_t x;')
$header.WriteLine('    int16_t y;')
$header.WriteLine('    int16_t w;')
$header.WriteLine('    int16_t h;')
$header.WriteLine('    int16_t xoff;')
$header.WriteLine('    int16_t yoff;')
$header.WriteLine('    int16_t xadv;')
$header.WriteLine('} SplashGlyph;')
$header.WriteLine('')
$header.WriteLine('extern const unsigned char splash_font_rgba[];')
$header.WriteLine('extern const unsigned int splash_font_rgba_size;')
$header.WriteLine('extern const int splash_font_atlas_w;')
$header.WriteLine('extern const int splash_font_atlas_h;')
$header.WriteLine('extern const int splash_font_baseline;')
$header.WriteLine('extern const int splash_font_line_height;')
$header.WriteLine('extern const SplashGlyph splash_font_glyphs[SPLASH_FONT_COUNT];')
$header.WriteLine('')
$header.WriteLine('#endif')
$header.Close()

$source = New-Object System.IO.StreamWriter($OutC, $false, [System.Text.Encoding]::ASCII)
$source.WriteLine('#include "splash_font.h"')
$source.WriteLine('')
$source.WriteLine("const int splash_font_atlas_w = $AtlasWidth;")
$source.WriteLine("const int splash_font_atlas_h = $atlasHeight;")
$source.WriteLine("const int splash_font_baseline = $baseline;")
$source.WriteLine("const int splash_font_line_height = $lineHeight;")
$source.WriteLine('')
$source.WriteLine("const unsigned char splash_font_rgba[] = {")
$bytesPerLine = 16
for ($i = 0; $i -lt $atlas.Length; $i++) {
    if ($i % $bytesPerLine -eq 0) { $source.Write('    ') }
    $source.Write(('0x{0:X2}' -f $atlas[$i]))
    if ($i -lt $atlas.Length - 1) { $source.Write(',') }
    if ($i % $bytesPerLine -eq ($bytesPerLine - 1)) {
        $source.WriteLine()
    } else {
        $source.Write(' ')
    }
}
if ($atlas.Length % $bytesPerLine -ne 0) { $source.WriteLine() }
$source.WriteLine('};')
$source.WriteLine('')
$source.WriteLine("const unsigned int splash_font_rgba_size = $($atlas.Length);")
$source.WriteLine('')
$source.WriteLine('const SplashGlyph splash_font_glyphs[SPLASH_FONT_COUNT] = {')
for ($i = 0; $i -lt $glyphs.Count; $i++) {
    $g = $glyphs[$i]
    $comma = if ($i -lt $glyphs.Count - 1) { ',' } else { '' }
    $source.WriteLine("    { $($g.X), $($g.Y), $($g.W), $($g.H), $($g.XOff), $($g.YOff), $($g.Adv) }$comma")
}
$source.WriteLine('};')
$source.Close()

if ($Stamp -ne '') {
    $dir = Split-Path -Parent $Stamp
    if ($dir -ne '' -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
    Set-Content -LiteralPath $Stamp -Value $signature -Encoding ASCII
}
