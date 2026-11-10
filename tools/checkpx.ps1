# The share of pixels in a region of a screenshot that are white (R, G, B > 200) or red (R > 120, G and B < 90),
# sampled every third pixel, printed with a dot. tools/ap_load_slot1.sh uses it to see which menu line is lit.
# Usage: powershell -ExecutionPolicy Bypass -File tools\checkpx.ps1 -Src shot.png -X 0 -Y 0 -W 100 -H 50 -Mode white
param([string]$Src, [int]$X, [int]$Y, [int]$W, [int]$H, [string]$Mode = "white")
Add-Type -AssemblyName System.Drawing
if (-not (Test-Path $Src)) { "missing"; exit 1 }
$b = [System.Drawing.Bitmap]::FromFile($Src)
$n = 0; $hit = 0
for ($yy = $Y; $yy -lt $Y + $H; $yy += 3) {
    for ($xx = $X; $xx -lt $X + $W; $xx += 3) {
        $c = $b.GetPixel($xx, $yy); $n++
        if ($Mode -eq "white" -and $c.R -gt 200 -and $c.G -gt 200 -and $c.B -gt 200) { $hit++ }
        if ($Mode -eq "red" -and $c.R -gt 120 -and $c.G -lt 90 -and $c.B -lt 90) { $hit++ }
    }
}
$b.Dispose()
($hit / $n).ToString("F4", [Globalization.CultureInfo]::InvariantCulture)
