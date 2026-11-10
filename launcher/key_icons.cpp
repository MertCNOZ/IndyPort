// The "keyboard_icons" texture pack: the game's controller button icons drawn as the keys and mouse buttons bound to
// them (Controls page), so the game's prompts show what to press on a keyboard. It takes the place of the same game
// textures as packs/modern_buttons (tools/make_button_icons.ps1 draws that one; the hashes and the atlas boxes are
// the same): the 32x32 A, B, X, Y of the menus and prompts, and the 128x256 icon atlas. Drawn with GDI+ at 8 times
// the game's size whenever the bindings or the icon choice change.
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>

#include "key_icons.h"

namespace gp = Gdiplus;
namespace fs = std::filesystem;

namespace {

constexpr int kWheelUp = 0x100, kWheelDown = 0x101;
constexpr float kScale = 8;

const gp::Color kBody(255, 30, 31, 34), kBodyLight(255, 70, 72, 78), kRim(255, 128, 130, 136),
    kLabel(255, 240, 240, 242), kGold(255, 246, 182, 50), kGoldDark(255, 150, 92, 18), kDepth(255, 14, 14, 16);

void Rounded(gp::GraphicsPath& p, const gp::RectF& r, float rad) {
    float d = std::min(rad * 2, std::min(r.Width, r.Height));
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

// text centred in r, made smaller until it fits, as a filled path so it is smooth at any size
void Text(gp::Graphics& g, const std::wstring& s, const gp::RectF& r, float size, const gp::Color& c) {
    if (s.empty()) return;
    gp::FontFamily fam(L"Segoe UI");
    for (int i = 0; i < 16; i++, size *= 0.88f) {
        gp::GraphicsPath p;
        p.AddString(s.c_str(), -1, &fam, gp::FontStyleBold, size, gp::PointF(0, 0), gp::StringFormat::GenericTypographic());
        gp::RectF b;
        p.GetBounds(&b);
        if (i < 15 && (b.Width > r.Width * 0.80f || b.Height > r.Height * 0.62f)) continue;
        gp::Matrix m;
        m.Translate(r.X + (r.Width - b.Width) / 2 - b.X, r.Y + (r.Height - b.Height) / 2 - b.Y);
        p.Transform(&m);
        gp::SolidBrush br(c);
        g.FillPath(&br, &p);
        return;
    }
}

// a key: a dark rounded cap on a darker base (its depth), the label on top
void Keycap(gp::Graphics& g, gp::RectF r, const std::wstring& label) {
    float s = std::min(r.Width, r.Height), rim = s * 0.045f;
    gp::RectF base(r.X + rim, r.Y + rim, r.Width - rim * 2, r.Height - rim * 2);
    {
        gp::GraphicsPath p;
        Rounded(p, base, s * 0.2f);
        gp::SolidBrush br(kDepth);
        g.FillPath(&br, &p);
    }
    gp::RectF cap(base.X + s * 0.05f, base.Y + s * 0.03f, base.Width - s * 0.1f, base.Height - s * 0.14f);
    gp::GraphicsPath p;
    Rounded(p, cap, s * 0.16f);
    gp::LinearGradientBrush br(gp::PointF(cap.X, cap.Y), gp::PointF(cap.X, cap.Y + cap.Height), kBodyLight, kBody);
    g.FillPath(&br, &p);
    gp::Pen pen(kRim, rim);
    g.DrawPath(&pen, &p);
    Text(g, label, cap, s * 0.5f, kLabel);
}

// a mouse, its lit part in gold: 0 the mouse itself (moving it), 1 left button, 2 right, 3 the wheel / middle
void Mouse(gp::Graphics& g, gp::RectF r, int part) {
    float s = std::min(r.Width, r.Height), w = s * 0.62f, h = s * 0.92f, rim = s * 0.045f;
    gp::RectF b(r.X + (r.Width - w) / 2, r.Y + (r.Height - h) / 2, w, h);
    gp::GraphicsPath body;
    Rounded(body, b, w * 0.48f);
    gp::LinearGradientBrush br(gp::PointF(b.X, b.Y), gp::PointF(b.X, b.Y + b.Height), kBodyLight, kBody);
    g.FillPath(&br, &body);
    float split = b.Y + h * 0.42f, cx = b.X + w / 2;
    gp::SolidBrush gold(kGold);
    if (part == 1 || part == 2) {
        g.SetClip(&body);
        gp::RectF half(part == 1 ? b.X : cx, b.Y, w / 2, split - b.Y);
        g.FillRectangle(&gold, half);
        g.ResetClip();
    }
    gp::Pen pen(kRim, rim);
    g.DrawPath(&pen, &body);
    g.DrawLine(&pen, b.X, split, b.X + w, split);
    g.DrawLine(&pen, cx, b.Y, cx, split);
    gp::RectF wheel(cx - w * 0.09f, b.Y + h * 0.1f, w * 0.18f, h * 0.2f);
    gp::GraphicsPath wp;
    Rounded(wp, wheel, w * 0.09f);
    if (part == 3) g.FillPath(&gold, &wp);
    else {
        gp::SolidBrush dark(kDepth);
        g.FillPath(&dark, &wp);
    }
    g.DrawPath(&pen, &wp);
    if (part == 0) {  // arrows out to the sides: moving it
        gp::Pen ap(kGold, s * 0.06f);
        ap.SetEndCap(gp::LineCapArrowAnchor);
        float y = b.Y + h * 0.7f;
        g.DrawLine(&ap, b.X + w * 0.38f, y, r.X + s * 0.02f, y);
        g.DrawLine(&ap, b.X + w * 0.62f, y, r.X + r.Width - s * 0.02f, y);
    }
}

// four keys as arrow keys sit: up on top, left, down and right below
void KeyCross(gp::Graphics& g, gp::RectF r, const std::wstring& up, const std::wstring& down, const std::wstring& left,
              const std::wstring& right) {
    float s = std::min(r.Width / 3, r.Height / 2);
    float x = r.X + (r.Width - s * 3) / 2, y = r.Y + (r.Height - s * 2) / 2;
    Keycap(g, gp::RectF(x + s, y, s, s), up);
    Keycap(g, gp::RectF(x, y + s, s, s), left);
    Keycap(g, gp::RectF(x + s, y + s, s, s), down);
    Keycap(g, gp::RectF(x + s * 2, y + s, s, s), right);
}

// a gold arrow pointing at angle degrees (0 right, 90 down), as the modern pack's
void Arrow(gp::Graphics& g, gp::RectF r, float angle) {
    float s = std::min(r.Width, r.Height);
    gp::GraphicsState st = g.Save();
    g.TranslateTransform(r.X + r.Width / 2, r.Y + r.Height / 2);
    g.RotateTransform(angle);
    float L = s * 0.46f, w = s * 0.13f, head = s * 0.30f;
    gp::PointF pts[] = {{-L, -w}, {L - head, -w}, {L - head, -head}, {L, 0}, {L - head, head}, {L - head, w}, {-L, w}};
    gp::GraphicsPath p;
    p.AddPolygon(pts, 7);
    gp::LinearGradientBrush br(gp::PointF(0, -head), gp::PointF(0, head), kGold, kGoldDark);
    g.FillPath(&br, &p);
    gp::Pen pen(gp::Color(255, 90, 50, 8), s * 0.04f);
    g.DrawPath(&pen, &p);
    g.Restore(st);
}

// turn around: a gold ring with an arrow head
void Turn(gp::Graphics& g, gp::RectF r) {
    float s = std::min(r.Width, r.Height);
    gp::RectF c(r.X + (r.Width - s) / 2 + s * 0.16f, r.Y + (r.Height - s) / 2 + s * 0.16f, s * 0.68f, s * 0.68f);
    gp::Pen pen(kGold, s * 0.13f);
    pen.SetStartCap(gp::LineCapRound);
    g.DrawArc(&pen, c, 120, 290);
    float a = 120 * 3.14159265f / 180, rad = c.Width / 2;
    float ex = c.X + rad + rad * std::cos(a), ey = c.Y + rad + rad * std::sin(a), hs = s * 0.2f;
    gp::PointF pts[] = {{ex - hs, ey - hs * 0.2f}, {ex + hs * 0.9f, ey - hs * 0.4f}, {ex + hs * 0.1f, ey + hs}};
    gp::SolidBrush br(kGold);
    g.FillPolygon(&br, pts, 3);
}

bool IsMouse(int vk) {
    return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2 ||
           vk == kWheelUp || vk == kWheelDown;
}

// a key's short label for a cap
std::wstring Label(int vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::wstring(1, wchar_t(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return L"NUM" + std::to_wstring(vk - VK_NUMPAD0);
    switch (vk) {
        case 0: return L"";
        case VK_SPACE: return L"SPACE";
        case VK_RETURN: return L"ENTER";
        case VK_ESCAPE: return L"ESC";
        case VK_TAB: return L"TAB";
        case VK_BACK: return L"BKSP";
        case VK_MENU: case VK_LMENU: case VK_RMENU: return L"ALT";
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return L"SHIFT";
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return L"CTRL";
        case VK_CAPITAL: return L"CAPS";
        case VK_UP: return L"↑";
        case VK_DOWN: return L"↓";
        case VK_LEFT: return L"←";
        case VK_RIGHT: return L"→";
        case VK_INSERT: return L"INS";
        case VK_DELETE: return L"DEL";
        case VK_HOME: return L"HOME";
        case VK_END: return L"END";
        case VK_PRIOR: return L"PGUP";
        case VK_NEXT: return L"PGDN";
        default: break;
    }
    UINT sc = MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC);
    wchar_t name[32] = {};
    if (sc && GetKeyNameTextW(LONG(sc << 16), name, 32) > 0) {
        std::wstring n = name;
        for (auto& ch : n) ch = wchar_t(towupper(ch));
        return n.substr(0, 5);
    }
    return L"?";
}

// one binding: a mouse part or a key cap
void Binding(gp::Graphics& g, gp::RectF r, int vk) {
    switch (vk) {
        case VK_LBUTTON: Mouse(g, r, 1); return;
        case VK_RBUTTON: Mouse(g, r, 2); return;
        case VK_MBUTTON: case VK_XBUTTON1: case VK_XBUTTON2: case kWheelUp: case kWheelDown: Mouse(g, r, 3); return;
        default: Keycap(g, r, Label(vk)); return;
    }
}

bool SavePng(gp::Bitmap& b, const fs::path& file) {
    UINT n = 0, size = 0;
    gp::GetImageEncodersSize(&n, &size);
    if (!size) return false;
    std::string buf(size, '\0');
    auto* enc = reinterpret_cast<gp::ImageCodecInfo*>(buf.data());
    gp::GetImageEncoders(n, size, enc);
    for (UINT i = 0; i < n; i++)
        if (std::wstring(enc[i].MimeType) == L"image/png") return b.Save(file.c_str(), &enc[i].Clsid) == gp::Ok;
    return false;
}

void Smooth(gp::Graphics& g) {
    g.SetSmoothingMode(gp::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(gp::PixelOffsetModeHighQuality);
    g.SetCompositingQuality(gp::CompositingQualityHighQuality);
    g.SetTextRenderingHint(gp::TextRenderingHintAntiAliasGridFit);
}

}  // namespace

bool MakeKeyIcons(const fs::path& dir, const KeyIconBindings& keys, bool mouse_look) {
    static ULONG_PTR token = 0;
    if (!token) {
        gp::GdiplusStartupInput in;
        if (gp::GdiplusStartup(&token, &in, nullptr) != gp::Ok) return false;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    auto primary = [&](int action) { return keys[size_t(action)][0] ? keys[size_t(action)][0] : keys[size_t(action)][1]; };
    // the menus are played with keys: there, a key bound to the button rather than a mouse button
    auto for_menus = [&](int action) {
        for (int k : keys[size_t(action)])
            if (k && !IsMouse(k)) return k;
        return primary(action);
    };
    bool ok = true;

    // the single A B X Y (menus and prompts)
    const char* singles[4] = {"be24b0363a4db709", "46079a11e789a4d5", "46095a0cd4438b43", "2978e289885083bb"};
    for (int i = 0; i < 4; i++) {
        gp::Bitmap b(int(32 * kScale), int(32 * kScale), PixelFormat32bppARGB);
        gp::Graphics g(&b);
        Smooth(g);
        Binding(g, gp::RectF(0, 0, 23 * kScale, 23 * kScale), for_menus(i));
        ok &= SavePng(b, dir / (std::string("32x32_") + singles[i] + "_DXT5.png"));
    }

    // the atlas: the game's boxes (x, y, w, h), the icon each was and what takes its place
    struct Box { int x, y, w, h; int what; float arg; };
    enum { kButton, kArrow, kMoveKeys, kLookKeys, kTurn, kPadKeys };
    const Box atlas[] = {
        {0, 0, 23, 23, kButton, 0},   {25, 0, 23, 23, kButton, 1},  {50, 0, 23, 23, kButton, 2},   // A B X
        {0, 33, 23, 23, kButton, 3},  {31, 33, 20, 23, kButton, 6}, {53, 33, 20, 23, kButton, 7},  // Y LT RT
        {75, 34, 22, 21, kArrow, 0},  {1, 68, 19, 19, kArrow, 45},  {22, 66, 21, 22, kArrow, 90},
        {45, 69, 19, 19, kArrow, 135}, {68, 67, 22, 21, kArrow, 180}, {0, 101, 19, 19, kArrow, 225},
        {22, 100, 21, 22, kArrow, 270}, {46, 100, 19, 19, kArrow, 315},
        {67, 99, 22, 22, kMoveKeys, 0}, {0, 132, 23, 24, kTurn, 0},  {25, 132, 22, 22, kLookKeys, 0},  // LS turn RS
        {50, 134, 19, 19, kPadKeys, 0}, {72, 133, 18, 21, kButton, 5}, {1, 167, 18, 20, kButton, 4}};  // d-pad LB RB
    gp::Bitmap b(int(128 * kScale), int(256 * kScale), PixelFormat32bppARGB);
    gp::Graphics g(&b);
    Smooth(g);
    for (const Box& x : atlas) {
        gp::RectF r(x.x * kScale, x.y * kScale, x.w * kScale, x.h * kScale);
        switch (x.what) {
            case kButton: Binding(g, r, primary(int(x.arg))); break;
            case kArrow: Arrow(g, r, x.arg); break;
            case kTurn: Turn(g, r); break;
            case kMoveKeys:
                KeyCross(g, r, Label(primary(16)), Label(primary(17)), Label(primary(18)), Label(primary(19)));
                break;
            case kLookKeys:
                if (mouse_look) Mouse(g, r, 0);
                else KeyCross(g, r, Label(primary(20)), Label(primary(21)), Label(primary(22)), Label(primary(23)));
                break;
            case kPadKeys:
                KeyCross(g, r, Label(primary(8)), Label(primary(9)), Label(primary(10)), Label(primary(11)));
                break;
        }
    }
    ok &= SavePng(b, dir / "128x256_ae7c8bc40d1623af_DXT5.png");
    return ok;
}
