# Draws the "modern_buttons" texture pack: the game's controller button icons as a modern Xbox controller's, drawn
# here from shapes and text (no game art), at 8 times the game's size. The game's textures are named by the hash the
# runtime gives their bytes (textures\dumps names them so); each image here takes the place of one:
#   32x32 A, B, X, Y (pause menu and prompts), and the 128x256 icon atlas (buttons, triggers, sticks, arrows).
# The boxes are where the game's atlas has each icon (found from its alpha), times 8.
# Usage: powershell -ExecutionPolicy Bypass -File tools\make_button_icons.ps1 [out folder]
param([string]$Out = (Join-Path (Split-Path $PSScriptRoot) "packs\modern_buttons"))
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;

public static class Icons {
    static readonly Color Body = Color.FromArgb(255, 30, 31, 34);
    static readonly Color BodyLight = Color.FromArgb(255, 64, 66, 72);
    static readonly Color Rim = Color.FromArgb(255, 120, 122, 128);
    static readonly Color Label = Color.FromArgb(255, 238, 238, 240);
    static readonly Color Gold = Color.FromArgb(255, 246, 182, 50);
    static readonly Color GoldDark = Color.FromArgb(255, 150, 92, 18);

    public static Graphics Begin(Bitmap b) {
        Graphics g = Graphics.FromImage(b);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.PixelOffsetMode = PixelOffsetMode.HighQuality;
        g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
        g.CompositingQuality = CompositingQuality.HighQuality;
        return g;
    }

    static GraphicsPath Rounded(RectangleF r, float rad) {
        var p = new GraphicsPath();
        float d = rad * 2;
        p.AddArc(r.X, r.Y, d, d, 180, 90);
        p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
        p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
        p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
        p.CloseFigure();
        return p;
    }

    // text centred in r, as a filled path so it is smooth at any size
    static void Text(Graphics g, string s, RectangleF r, float size, Color c) {
        using (var fam = new FontFamily("Segoe UI"))
        using (var p = new GraphicsPath())
        using (var sf = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center }) {
            p.AddString(s, fam, (int)FontStyle.Bold, size, new PointF(0, 0), StringFormat.GenericTypographic);
            RectangleF tb = p.GetBounds();
            using (var m = new Matrix()) {
                m.Translate(r.X + (r.Width - tb.Width) / 2 - tb.X, r.Y + (r.Height - tb.Height) / 2 - tb.Y);
                p.Transform(m);
            }
            using (var br = new SolidBrush(c)) g.FillPath(br, p);
        }
    }

    static void Body3D(Graphics g, GraphicsPath shape, RectangleF r, float rim) {
        using (var br = new LinearGradientBrush(new PointF(r.X, r.Y), new PointF(r.X, r.Bottom), BodyLight, Body))
            g.FillPath(br, shape);
        using (var pen = new Pen(Rim, rim)) g.DrawPath(pen, shape);
    }

    // A B X Y: a dark round button with the letter in its colour
    public static void Face(Graphics g, RectangleF r, string letter, Color colour) {
        float s = Math.Min(r.Width, r.Height), rim = s * 0.05f;
        var c = new RectangleF(r.X + (r.Width - s) / 2 + rim, r.Y + (r.Height - s) / 2 + rim, s - rim * 2, s - rim * 2);
        using (var p = new GraphicsPath()) { p.AddEllipse(c); Body3D(g, p, c, rim); }
        Text(g, letter, c, s * 0.62f, colour);
    }

    // LB RB: a bumper, wide with a rounded top
    public static void Bumper(Graphics g, RectangleF r, string text) {
        float rim = Math.Min(r.Width, r.Height) * 0.05f;
        float h = r.Height * 0.62f;
        var b = new RectangleF(r.X + rim, r.Y + (r.Height - h) / 2, r.Width - rim * 2, h);
        using (var p = Rounded(b, h * 0.42f)) Body3D(g, p, b, rim);
        Text(g, text, b, h * 0.62f, Label);
    }

    // LT RT: a trigger, taller with a round top
    public static void Trigger(Graphics g, RectangleF r, string text) {
        float rim = Math.Min(r.Width, r.Height) * 0.05f;
        var b = new RectangleF(r.X + rim, r.Y + rim, r.Width - rim * 2, r.Height - rim * 2);
        using (var p = new GraphicsPath()) {
            float rad = b.Width * 0.5f;
            p.AddArc(b.X, b.Y, b.Width, rad * 2, 180, 180);
            p.AddLine(b.Right, b.Y + rad, b.Right, b.Bottom - b.Width * 0.18f);
            p.AddArc(b.Right - b.Width * 0.36f, b.Bottom - b.Width * 0.36f, b.Width * 0.36f, b.Width * 0.36f, 0, 90);
            p.AddArc(b.X, b.Bottom - b.Width * 0.36f, b.Width * 0.36f, b.Width * 0.36f, 90, 90);
            p.CloseFigure();
            Body3D(g, p, b, rim);
        }
        Text(g, text, new RectangleF(b.X, b.Y + b.Height * 0.12f, b.Width, b.Height * 0.88f), b.Width * 0.48f, Label);
    }

    // LS RS: a stick, a ring around a dark cap with the letters
    public static void Stick(Graphics g, RectangleF r, string text) {
        float s = Math.Min(r.Width, r.Height), rim = s * 0.05f;
        var c = new RectangleF(r.X + (r.Width - s) / 2 + rim, r.Y + (r.Height - s) / 2 + rim, s - rim * 2, s - rim * 2);
        using (var p = new GraphicsPath()) { p.AddEllipse(c); Body3D(g, p, c, rim); }
        var inner = RectangleF.Inflate(c, -s * 0.13f, -s * 0.13f);
        using (var pen = new Pen(Rim, rim * 0.8f)) g.DrawEllipse(pen, inner);
        Text(g, text, c, s * 0.40f, Label);
    }

    // the d-pad: a dark cross
    public static void Dpad(Graphics g, RectangleF r) {
        float s = Math.Min(r.Width, r.Height), rim = s * 0.05f, arm = s * 0.36f;
        float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2, h = s / 2 - rim;
        using (var p = new GraphicsPath()) {
            p.AddPolygon(new[] {
                new PointF(cx - arm / 2, cy - h), new PointF(cx + arm / 2, cy - h), new PointF(cx + arm / 2, cy - arm / 2),
                new PointF(cx + h, cy - arm / 2), new PointF(cx + h, cy + arm / 2), new PointF(cx + arm / 2, cy + arm / 2),
                new PointF(cx + arm / 2, cy + h), new PointF(cx - arm / 2, cy + h), new PointF(cx - arm / 2, cy + arm / 2),
                new PointF(cx - h, cy + arm / 2), new PointF(cx - h, cy - arm / 2), new PointF(cx - arm / 2, cy - arm / 2) });
            Body3D(g, p, r, rim);
        }
    }

    // a gold arrow pointing at angle degrees (0 right, 90 down)
    public static void Arrow(Graphics g, RectangleF r, float angle) {
        float s = Math.Min(r.Width, r.Height);
        var st = g.Save();
        g.TranslateTransform(r.X + r.Width / 2, r.Y + r.Height / 2);
        g.RotateTransform(angle);
        float L = s * 0.46f, w = s * 0.13f, head = s * 0.30f;
        using (var p = new GraphicsPath()) {
            p.AddPolygon(new[] {
                new PointF(-L, -w), new PointF(L - head, -w), new PointF(L - head, -head), new PointF(L, 0),
                new PointF(L - head, head), new PointF(L - head, w), new PointF(-L, w) });
            using (var br = new LinearGradientBrush(new PointF(0, -head), new PointF(0, head), Gold, GoldDark)) g.FillPath(br, p);
            using (var pen = new Pen(Color.FromArgb(255, 90, 50, 8), s * 0.04f)) g.DrawPath(pen, p);
        }
        g.Restore(st);
    }

    // turn around: a gold ring with an arrow head
    public static void Turn(Graphics g, RectangleF r) {
        float s = Math.Min(r.Width, r.Height);
        var c = RectangleF.Inflate(new RectangleF(r.X + (r.Width - s) / 2, r.Y + (r.Height - s) / 2, s, s), -s * 0.16f, -s * 0.16f);
        using (var pen = new Pen(Gold, s * 0.13f)) { pen.StartCap = LineCap.Round; g.DrawArc(pen, c, 120, 290); }
        float a = (float)(120 * Math.PI / 180), rad = c.Width / 2;
        float ex = c.X + rad + rad * (float)Math.Cos(a), ey = c.Y + rad + rad * (float)Math.Sin(a);
        float hs = s * 0.2f;
        using (var p = new GraphicsPath()) {
            p.AddPolygon(new[] { new PointF(ex - hs, ey - hs * 0.2f), new PointF(ex + hs * 0.9f, ey - hs * 0.4f), new PointF(ex + hs * 0.1f, ey + hs) });
            using (var br = new SolidBrush(Gold)) g.FillPath(br, p);
        }
    }
}
"@

New-Item -ItemType Directory -Force $Out | Out-Null
$k = 8
$green = [System.Drawing.Color]::FromArgb(255, 110, 205, 70)
$red = [System.Drawing.Color]::FromArgb(255, 235, 72, 62)
$blue = [System.Drawing.Color]::FromArgb(255, 72, 146, 240)
$yellow = [System.Drawing.Color]::FromArgb(255, 245, 205, 50)

# the single 32x32 face buttons
$singles = @{ "be24b0363a4db709" = @("A", $green); "46079a11e789a4d5" = @("B", $red);
              "46095a0cd4438b43" = @("X", $blue); "2978e289885083bb" = @("Y", $yellow) }
foreach ($key in $singles.Keys) {
    $b = New-Object System.Drawing.Bitmap (32 * $k), (32 * $k)
    $g = [Icons]::Begin($b)
    [Icons]::Face($g, (New-Object System.Drawing.RectangleF 0, 0, (23 * $k), (23 * $k)), $singles[$key][0], $singles[$key][1])
    $b.Save((Join-Path $Out "32x32_${key}_DXT5.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $b.Dispose()
}

# the 128x256 atlas: x, y, width, height of each icon in the game's texture, and what it becomes
$atlas = @(
    @(0, 0, 23, 23, "face", "A"), @(25, 0, 23, 23, "face", "B"), @(50, 0, 23, 23, "face", "X"), @(0, 33, 23, 23, "face", "Y"),
    @(31, 33, 20, 23, "trigger", "LT"), @(53, 33, 20, 23, "trigger", "RT"),
    @(75, 34, 22, 21, "arrow", 0), @(1, 68, 19, 19, "arrow", 45), @(22, 66, 21, 22, "arrow", 90), @(45, 69, 19, 19, "arrow", 135),
    @(68, 67, 22, 21, "arrow", 180), @(0, 101, 19, 19, "arrow", 225), @(22, 100, 21, 22, "arrow", 270), @(46, 100, 19, 19, "arrow", 315),
    @(67, 99, 22, 22, "stick", "LS"), @(0, 132, 23, 24, "turn", ""), @(25, 132, 22, 22, "stick", "RS"),
    @(50, 134, 19, 19, "dpad", ""), @(72, 133, 18, 21, "bumper", "LB"), @(1, 167, 18, 20, "bumper", "RB"))
$colours = @{ "A" = $green; "B" = $red; "X" = $blue; "Y" = $yellow }
$b = New-Object System.Drawing.Bitmap (128 * $k), (256 * $k)
$g = [Icons]::Begin($b)
foreach ($i in $atlas) {
    $r = New-Object System.Drawing.RectangleF ($i[0] * $k), ($i[1] * $k), ($i[2] * $k), ($i[3] * $k)
    switch ($i[4]) {
        "face" { [Icons]::Face($g, $r, $i[5], $colours[$i[5]]) }
        "trigger" { [Icons]::Trigger($g, $r, $i[5]) }
        "bumper" { [Icons]::Bumper($g, $r, $i[5]) }
        "stick" { [Icons]::Stick($g, $r, $i[5]) }
        "dpad" { [Icons]::Dpad($g, $r) }
        "arrow" { [Icons]::Arrow($g, $r, [float]$i[5]) }
        "turn" { [Icons]::Turn($g, $r) }
    }
}
$b.Save((Join-Path $Out "128x256_ae7c8bc40d1623af_DXT5.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $b.Dispose()
Get-ChildItem $Out | ForEach-Object { "{0} ({1} KB)" -f $_.Name, [int]($_.Length / 1024) }
