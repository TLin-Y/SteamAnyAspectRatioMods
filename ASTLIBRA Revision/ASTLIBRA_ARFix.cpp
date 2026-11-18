#include "stdafx.h"
#include "helper.hpp"
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <d3d9.h>
#include <cmath>

namespace
{
    constexpr char setGraphModePattern[] =
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 30 45 85 C0 BF 10 00 00 00";
    constexpr char makeScreenPattern[] =
        "48 63 05 ?? ?? ?? ?? 45 33 C0 41 8B 94 85 ?? ?? ?? ?? 41 8B 8C 85 ?? ?? ?? ?? E8";
    constexpr size_t makeScreenCallOffset = 26;
    constexpr DWORD rhwFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

    HMODULE ownModule;
    std::shared_ptr<spdlog::logger> lg;
    int targetW = 1280, targetH = 800;
    float targetAR = 1.6f;
    bool borderless = true;

    struct Canvas { int ew, eh, cw, ch; float ox, oy; };
    Canvas canvases[4];
    int nCanvas = 0;
    SafetyHookMid makeScreenHook, setGraphModeHook;

    Canvas Extend(int w, int h)
    {
        Canvas c{ w, h, w, h, 0, 0 };
        const float ar = (float)w / h;
        if (targetAR > ar + 0.001f)
        {
            c.ew = (int)std::lround(h * targetAR);
            c.ox = std::floor((c.ew - w) / 2.0f);
        }
        else if (targetAR < ar - 0.001f)
        {
            c.eh = (int)std::lround(w / targetAR);
            c.oy = (float)(c.eh - h);
        }
        return c;
    }

    void Remember(const Canvas& c)
    {
        for (int i = 0; i < nCanvas; ++i)
            if (canvases[i].ew == c.ew && canvases[i].eh == c.eh) { canvases[i] = c; return; }
        if (nCanvas < 4) canvases[nCanvas++] = c;
        lg->info("Canvas {}x{} placed in {}x{} at {},{}", c.cw, c.ch, c.ew, c.eh, c.ox, c.oy);
    }

    const Canvas* FindCanvas(UINT w, UINT h)
    {
        for (int i = 0; i < nCanvas; ++i)
            if (canvases[i].ew == (int)w && canvases[i].eh == (int)h && (canvases[i].ox || canvases[i].oy)) return &canvases[i];
        return nullptr;
    }

    bool LowEdge(float v) { return v >= -0.75f && v <= 0.75f; }
    bool HighEdge(float v, float e) { return v >= e - 1.75f && v <= e + 0.75f; }

    UINT VertCount(D3DPRIMITIVETYPE t, UINT prims)
    {
        switch (t)
        {
        case D3DPT_TRIANGLELIST: return prims * 3;
        case D3DPT_TRIANGLESTRIP: case D3DPT_TRIANGLEFAN: return prims + 2;
        case D3DPT_LINELIST: return prims * 2;
        case D3DPT_LINESTRIP: return prims + 1;
        default: return prims;
        }
    }

    void* Patch(void** vt, int idx, void* fn)
    {
        void* old = vt[idx];
        if (old == fn) return nullptr;
        DWORD p;
        VirtualProtect(&vt[idx], sizeof(void*), PAGE_EXECUTE_READWRITE, &p);
        vt[idx] = fn;
        VirtualProtect(&vt[idx], sizeof(void*), p, &p);
        return old;
    }

    using GetProcAddress_t = FARPROC(WINAPI*)(HMODULE, LPCSTR);
    using Create9Ex_t = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
    using CreateDeviceEx_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);
    using DPUP_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
    using SRT_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
    using STex_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
    using SR_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
    using SCPresent_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);

    GetProcAddress_t realGetProcAddress;
    Create9Ex_t realCreate9Ex;
    CreateDeviceEx_t realCreateDeviceEx;
    DPUP_t realDPUP;
    SRT_t realSRT;
    STex_t realSTex;
    SR_t realSR;
    SCPresent_t realSCPresent;

    UINT rtW = 0, rtH = 0, texW = 0, texH = 0;
    bool rtTexture = false;

    void UpdateRt(IDirect3DDevice9* d)
    {
        rtW = rtH = 0;
        IDirect3DSurface9* s = nullptr;
        if (SUCCEEDED(d->GetRenderTarget(0, &s)) && s)
        {
            D3DSURFACE_DESC desc{};
            s->GetDesc(&desc);
            s->Release();
            rtW = desc.Width; rtH = desc.Height;
        }
    }

    HRESULT STDMETHODCALLTYPE HkSRT(IDirect3DDevice9* d, DWORD i, IDirect3DSurface9* s)
    {
        const HRESULT hr = realSRT(d, i, s);
        if (i == 0) UpdateRt(d);
        return hr;
    }

    HRESULT STDMETHODCALLTYPE HkSTex(IDirect3DDevice9* d, DWORD stage, IDirect3DBaseTexture9* t)
    {
        if (stage == 0)
        {
            rtTexture = false;
            texW = texH = 0;
            D3DSURFACE_DESC desc{};
            if (t && t->GetType() == D3DRTYPE_TEXTURE && SUCCEEDED(static_cast<IDirect3DTexture9*>(t)->GetLevelDesc(0, &desc)))
            {
                rtTexture = (desc.Usage & D3DUSAGE_RENDERTARGET) != 0;
                texW = desc.Width; texH = desc.Height;
            }
        }
        return realSTex(d, stage, t);
    }

    HRESULT DrawOnCanvas(IDirect3DDevice9* d, const Canvas* c, D3DPRIMITIVETYPE t, UINT n, const void* v, UINT stride, DWORD fvf)
    {
        const UINT count = VertCount(t, n);
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (UINT i = 0; i < count; ++i)
        {
            const float* p = (const float*)((const uint8_t*)v + i * stride);
            x0 = (std::min)(x0, p[0]); x1 = (std::max)(x1, p[0]);
            y0 = (std::min)(y0, p[1]); y1 = (std::max)(y1, p[1]);
        }
        if (LowEdge(x0) && LowEdge(y0) && HighEdge(x1, (float)c->ew) && HighEdge(y1, (float)c->eh))
            return realDPUP(d, t, n, v, stride);

        thread_local std::vector<uint8_t> buf;
        thread_local std::vector<int8_t> bar;
        buf.assign((const uint8_t*)v, (const uint8_t*)v + count * stride);
        const bool textured = (fvf & D3DFVF_TEXCOUNT_MASK) != 0;
        const bool quadList = t == D3DPT_TRIANGLELIST && n % 2 == 0 && n >= 2;
        const bool snapshot = rtTexture && n == 2 && t == D3DPT_TRIANGLELIST && fvf == rhwFvf && stride >= 28 && FindCanvas(texW, texH);
        float orig[6][2]{};
        if (snapshot)
            for (UINT i = 0; i < 6; ++i)
            {
                const float* p = (const float*)(buf.data() + i * stride);
                orig[i][0] = p[0]; orig[i][1] = p[1];
            }

        UINT prims = n;
        const float lo[2] = { x0, y0 }, hi[2] = { x1, y1 };
        const float len[2] = { (float)c->cw, (float)c->ch };
        const float ext[2] = { (float)c->ew, (float)c->eh };
        const float before[2] = { c->ox, c->oy };
        for (int a = 0; a < 2; ++a)
        {
            const float eb = before[a], ea = ext[a] - len[a] - eb, L = len[a];
            if ((!eb && !ea) || (LowEdge(lo[a]) && HighEdge(hi[a], ext[a]))) continue;
            auto at = [&](UINT i) -> float& { return ((float*)(buf.data() + i * stride))[a]; };
            auto quadRange = [&](UINT k, float& qa, float& qb) {
                qa = 1e9f; qb = -1e9f;
                for (UINT j = 0; j < 6; ++j) { const float vv = at(k * 6 + j); qa = (std::min)(qa, vv); qb = (std::max)(qb, vv); }
            };

            enum { Shift, Snap, Pin, Stretch, Tile } mode = Shift;
            if (!textured) mode = Snap;
            else if (n == 2) mode = (a == 0 && y0 >= len[1] * 0.75f && HighEdge(y1, len[1])) ? Shift : Pin;
            else if (quadList)
            {
                bool big = n / 2 <= 4;
                for (UINT k = 0; big && k < n / 2; ++k) { float qa, qb; quadRange(k, qa, qb); big = qb - qa >= L / 6; }
                mode = big ? Stretch : Tile;
            }

            if (mode == Tile)
            {
                const UINT q = 6 * stride, sf = stride / 4;
                const bool mirror = stride >= 28;
                for (UINT k = 0; k < n / 2; ++k)
                {
                    float qa, qb;
                    quadRange(k, qa, qb);
                    const float wq = qb - qa;
                    if (wq < 4.0f) continue;
                    float dir = 0, need = 0;
                    if (eb && qa <= 0.75f && qa <= lo[a] + 1.0f) { dir = -1; need = eb + qa; }
                    else if (ea && qb >= L - 1.75f && qb >= hi[a] - 1.0f) { dir = 1; need = ea + L - qb; }
                    if (!dir) continue;
                    int copy = 0;
                    for (float s = wq; s - wq < need && prims < n + 600; s += wq, ++copy)
                    {
                        const size_t start = buf.size();
                        buf.insert(buf.end(), buf.begin() + k * q, buf.begin() + (k + 1) * q);
                        float* qv = (float*)(buf.data() + start);
                        float ua = 1e9f, ub = -1e9f;
                        for (UINT j = 0; j < 6; ++j) { const float u = qv[j * sf + 5 + a]; ua = (std::min)(ua, u); ub = (std::max)(ub, u); }
                        for (UINT j = 0; j < 6; ++j)
                        {
                            qv[j * sf + a] += dir * s;
                            if (mirror && copy % 2 == 0) qv[j * sf + 5 + a] = ua + ub - qv[j * sf + 5 + a];
                        }
                        prims += 2;
                    }
                }
            }

            const UINT all = VertCount(t, prims);
            const bool touchLo = LowEdge(lo[a]), touchHi = HighEdge(hi[a], L);
            bar.assign(all, 0);
            if (mode == Snap)
            {
                const UINT group = quadList ? 6 : all;
                for (UINT g0 = 0; g0 < all; g0 += group)
                {
                    const UINT g1 = (std::min)(all, g0 + group);
                    float ga = 1e9f, gb = -1e9f;
                    for (UINT i = g0; i < g1; ++i) { ga = (std::min)(ga, at(i)); gb = (std::max)(gb, at(i)); }
                    const bool thin = gb - ga < L / 4;
                    const int8_t f = !thin ? 0 : (ga <= 0.75f && gb < L - 1.75f) ? 1 : (gb >= L - 1.75f && ga > 0.75f) ? 2 : 0;
                    for (UINT i = g0; i < g1; ++i) bar[i] = f;
                }
            }
            for (UINT i = 0; i < all; ++i)
            {
                float& vv = at(i);
                float r = vv;
                switch (mode)
                {
                case Snap:
                    if (bar[i] == 1) r = vv - eb;
                    else if (bar[i] == 2) r = vv + ea;
                    else if (vv <= 0.75f) r = -eb;
                    else if (vv >= L - 1.75f) r = L + ea;
                    break;
                case Pin:
                    if (eb && LowEdge(vv)) r = vv - eb;
                    else if (ea && HighEdge(vv, L)) r = vv + ea;
                    break;
                case Stretch:
                    if (touchLo && touchHi) r = (vv - lo[a]) * (L + eb + ea) / (hi[a] - lo[a]) + lo[a] - eb;
                    else if (touchLo && eb) r = hi[a] - (hi[a] - vv) * (hi[a] - lo[a] + eb) / (hi[a] - lo[a]);
                    else if (touchHi && ea) r = lo[a] + (vv - lo[a]) * (hi[a] - lo[a] + ea) / (hi[a] - lo[a]);
                    break;
                default:
                    break;
                }
                vv = r + eb;
            }
        }
        if (snapshot)
            for (UINT i = 0; i < 6; ++i)
            {
                float* p = (float*)(buf.data() + i * stride);
                p[5] += (p[0] - orig[i][0]) / texW;
                p[6] += (p[1] - orig[i][1]) / texH;
            }
        return realDPUP(d, t, prims, buf.data(), stride);
    }

    HRESULT STDMETHODCALLTYPE HkDPUP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT n, const void* v, UINT stride)
    {
        DWORD fvf = 0;
        if (!v || stride < 16 || FAILED(d->GetFVF(&fvf)) || (fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW)
            return realDPUP(d, t, n, v, stride);
        if (const Canvas* c = FindCanvas(rtW, rtH))
            return DrawOnCanvas(d, c, t, n, v, stride, fvf);
        if (rtTexture && fvf == rhwFvf && stride >= 28)
            if (const Canvas* src = FindCanvas(texW, texH))
            {
                const UINT count = VertCount(t, n);
                thread_local std::vector<uint8_t> buf;
                buf.assign((const uint8_t*)v, (const uint8_t*)v + count * stride);
                for (UINT i = 0; i < count; ++i)
                {
                    float* p = (float*)(buf.data() + i * stride);
                    p[5] += src->ox / texW;
                    p[6] += src->oy / texH;
                }
                return realDPUP(d, t, n, buf.data(), stride);
            }
        return realDPUP(d, t, n, v, stride);
    }

    HRESULT STDMETHODCALLTYPE HkSR(IDirect3DDevice9* d, IDirect3DSurface9* s, const RECT* sr, IDirect3DSurface9* ds, const RECT* dr, D3DTEXTUREFILTERTYPE f)
    {
        if (s && sr)
        {
            D3DSURFACE_DESC sd{};
            s->GetDesc(&sd);
            const Canvas* c = FindCanvas(sd.Width, sd.Height);
            if (c && !(sr->left == 0 && sr->top == 0 && sr->right == c->ew && sr->bottom == c->eh))
            {
                auto map = [](LONG v, int len, int ext, float o) -> LONG {
                    if (!o && ext == len) return v;
                    if (v <= 0) return 0;
                    if (v >= len - 1) return ext;
                    return v + (LONG)o;
                };
                const RECT r{ map(sr->left, c->cw, c->ew, c->ox), map(sr->top, c->ch, c->eh, c->oy),
                              map(sr->right, c->cw, c->ew, c->ox), map(sr->bottom, c->ch, c->eh, c->oy) };
                return realSR(d, s, &r, ds, dr, D3DTEXF_LINEAR);
            }
        }
        return realSR(d, s, sr, ds, dr, f);
    }

    HWND gameWindow;
    WNDPROC realWndProc;
    constexpr LONG_PTR frameBits = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_DLGFRAME;
    constexpr LONG_PTR frameExBits = WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_STATICEDGE;

    RECT TargetRect(HWND w)
    {
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi);
        const int mw = mi.rcMonitor.right - mi.rcMonitor.left, mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
        const int x = mi.rcMonitor.left + (std::max)(0, (mw - targetW) / 2);
        const int y = mi.rcMonitor.top + (std::max)(0, (mh - targetH) / 2);
        return { x, y, x + targetW, y + targetH };
    }

    LRESULT CALLBACK HkWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_WINDOWPOSCHANGING)
        {
            auto* pos = reinterpret_cast<WINDOWPOS*>(lp);
            if (!IsIconic(w) && !((pos->flags & SWP_NOSIZE) && (pos->flags & SWP_NOMOVE)))
            {
                const RECT r = TargetRect(w);
                pos->x = r.left; pos->y = r.top;
                pos->cx = r.right - r.left; pos->cy = r.bottom - r.top;
                pos->flags &= ~(SWP_NOSIZE | SWP_NOMOVE);
            }
        }
        else if (msg == WM_STYLECHANGING)
        {
            auto* st = reinterpret_cast<STYLESTRUCT*>(lp);
            if (wp == GWL_STYLE) st->styleNew &= ~frameBits;
            else if (wp == GWL_EXSTYLE) st->styleNew &= ~frameExBits;
        }
        return CallWindowProcW(realWndProc, w, msg, wp, lp);
    }

    void FixWindow()
    {
        if (!gameWindow || IsIconic(gameWindow)) return;
        if (!realWndProc)
        {
            realWndProc = (WNDPROC)SetWindowLongPtrW(gameWindow, GWLP_WNDPROC, (LONG_PTR)HkWndProc);
            lg->info("Borderless window {}x{}", targetW, targetH);
        }
        const LONG_PTR style = GetWindowLongPtrW(gameWindow, GWL_STYLE);
        const RECT r = TargetRect(gameWindow);
        RECT cur{};
        GetWindowRect(gameWindow, &cur);
        if (!(style & frameBits) && EqualRect(&cur, &r)) return;
        if (style & frameBits) SetWindowLongPtrW(gameWindow, GWL_STYLE, style & ~frameBits);
        SetWindowPos(gameWindow, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    HRESULT STDMETHODCALLTYPE HkSCPresent(IDirect3DSwapChain9* s, const RECT* a, const RECT* b, HWND w, const RGNDATA* r, DWORD f)
    {
        if (borderless)
        {
            static int frame = 0;
            if (++frame % 30 == 1)
            {
                if (!gameWindow)
                {
                    D3DPRESENT_PARAMETERS pp{};
                    s->GetPresentParameters(&pp);
                    gameWindow = pp.hDeviceWindow;
                }
                FixWindow();
            }
            if (realWndProc) b = nullptr;
        }
        return realSCPresent(s, a, b, w, r, f);
    }

    void HookDevice(IDirect3DDevice9* dev)
    {
        void** vt = *(void***)dev;
        if (auto o = Patch(vt, 34, (void*)HkSR)) realSR = (SR_t)o;
        if (auto o = Patch(vt, 37, (void*)HkSRT)) realSRT = (SRT_t)o;
        if (auto o = Patch(vt, 65, (void*)HkSTex)) realSTex = (STex_t)o;
        if (auto o = Patch(vt, 83, (void*)HkDPUP)) realDPUP = (DPUP_t)o;
        IDirect3DSwapChain9* sc = nullptr;
        if (SUCCEEDED(dev->GetSwapChain(0, &sc)) && sc)
        {
            if (auto o = Patch(*(void***)sc, 3, (void*)HkSCPresent)) realSCPresent = (SCPresent_t)o;
            sc->Release();
        }
        UpdateRt(dev);
        lg->info("Direct3D 9 device hooked, back buffer {}x{}", rtW, rtH);
    }

    HRESULT STDMETHODCALLTYPE HkCreateDeviceEx(IDirect3D9Ex* self, UINT a, D3DDEVTYPE t, HWND w, DWORD f, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* m, IDirect3DDevice9Ex** out)
    {
        if (pp && !pp->Windowed) lg->warn("Fullscreen mode detected; set Screen Mode to WINDOW in the game options");
        gameWindow = pp && pp->hDeviceWindow ? pp->hDeviceWindow : w;
        const HRESULT hr = realCreateDeviceEx(self, a, t, w, f, pp, m, out);
        if (SUCCEEDED(hr) && out && *out) HookDevice(*out);
        return hr;
    }

    HRESULT WINAPI HkCreate9Ex(UINT v, IDirect3D9Ex** out)
    {
        const HRESULT hr = realCreate9Ex(v, out);
        if (SUCCEEDED(hr) && out && *out)
            if (auto o = Patch(*(void***)*out, 20, (void*)HkCreateDeviceEx)) realCreateDeviceEx = (CreateDeviceEx_t)o;
        return hr;
    }

    FARPROC WINAPI HkGetProcAddress(HMODULE m, LPCSTR name)
    {
        FARPROC p = realGetProcAddress(m, name);
        if (p && (uintptr_t)name > 0xFFFF)
        {
            if (!strcmp(name, "Direct3DCreate9Ex"))
            {
                realCreate9Ex = (Create9Ex_t)p;
                return (FARPROC)HkCreate9Ex;
            }
            if (!strcmp(name, "D3D11CreateDevice") || !strcmp(name, "D3D11CreateDeviceAndSwapChain"))
                lg->warn("Direct3D 11 selected; set Direct 3D to 9.0 in the game options");
        }
        return p;
    }

    uint8_t* FindOne(const char* name, const char* pattern)
    {
        const auto hits = Memory::PatternScanAll(GetModuleHandleW(nullptr), pattern);
        if (hits.size() == 1) return hits[0];
        lg->error("{} pattern: {} matches", name, hits.size());
        return nullptr;
    }

    void Init()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(ownModule, path, MAX_PATH);
        const auto folder = std::filesystem::path(path).parent_path();
        lg = spdlog::basic_logger_mt("ASTLIBRA_ARFix", (folder / L"ASTLIBRA_ARFix.log").string(), true);
        lg->flush_on(spdlog::level::info);
        const auto ini = (folder / L"ASTLIBRA_ARFix.ini").wstring();
        targetW = GetPrivateProfileIntW(L"Display", L"Width", 1280, ini.c_str());
        targetH = GetPrivateProfileIntW(L"Display", L"Height", 800, ini.c_str());
        wchar_t flag[16]{};
        GetPrivateProfileStringW(L"Display", L"Borderless", L"true", flag, 16, ini.c_str());
        borderless = _wcsicmp(flag, L"false") != 0 && wcscmp(flag, L"0") != 0;
        if (targetW <= 0 || targetH <= 0)
        {
            MONITORINFO mi{ sizeof(mi) };
            GetMonitorInfoW(MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
            targetW = mi.rcMonitor.right - mi.rcMonitor.left;
            targetH = mi.rcMonitor.bottom - mi.rcMonitor.top;
        }
        targetAR = (float)targetW / targetH;
        lg->info("ASTLIBRA_ARFix {}.{}.{}: {}x{} borderless={}", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, targetW, targetH, borderless);

        auto* sgm = FindOne("SetGraphMode", setGraphModePattern);
        auto* ms = FindOne("MakeScreen", makeScreenPattern);
        if (!sgm || !ms) return;

        auto gpa = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetProcAddress");
        realGetProcAddress = (GetProcAddress_t)gpa;
        if (!Memory::HookIAT(GetModuleHandleW(nullptr), "KERNEL32.dll", (void*)gpa, (void*)HkGetProcAddress))
        {
            lg->error("GetProcAddress import not found");
            return;
        }
        auto resize = [](SafetyHookContext& c) {
            const Canvas cv = Extend((int)c.rcx, (int)c.rdx);
            Remember(cv);
            c.rcx = cv.ew;
            c.rdx = cv.eh;
        };
        if (auto h = SafetyHookMid::create(sgm, resize)) setGraphModeHook = std::move(*h);
        if (auto h = SafetyHookMid::create(ms + makeScreenCallOffset, resize)) makeScreenHook = std::move(*h);
        lg->info("Hooks: SetGraphMode={} MakeScreen={}", (bool)setGraphModeHook, (bool)makeScreenHook);
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        ownModule = module;
        DisableThreadLibraryCalls(module);
        Init();
    }
    return TRUE;
}
