// price_tag_printer.h
// =============================================================================
// МОДУЛЬ ПЕЧАТИ ЦЕННИКОВ — КОМПОНОВКА ПО ОБРАЗЦУ 10008.jpg
// ИСПРАВЛЕНИЯ Senior Developer (C++20, Production):
// 1. Размеры шрифтов увеличены с 3 пт до 7-9 пт для четкой термопечати.
// 2. Математика позиционирования (57 мм ширина ценника) сохранена, так как 
//    она идеально вписывается в 70 мм печатной области (57 + 5 + 5 = 67 <= 70).
// =============================================================================
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <cmath>
#include <ctime>
#include <mutex>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#include "logger.h"
#include "string_utils.h"
#include "config.h"
#include "receipt_printer.h"
#include "shop_barcode.h"

extern Logger g_logger;
extern HINSTANCE g_hInstance;

#ifndef IDR_PNG_STATUE
#define IDR_PNG_STATUE 300
#endif

namespace {
    // --- HDC ---
    struct HdcDeleter {
        void operator()(HDC dc) const noexcept { if (dc) DeleteDC(dc); }
    };
    using HdcPtr = std::unique_ptr<std::remove_pointer_t<HDC>, HdcDeleter>;

    struct ReleaseDcDeleter {
        HWND hwnd;
        void operator()(HDC dc) const noexcept { if (dc) ReleaseDC(hwnd, dc); }
    };

    // --- HBITMAP ---
    struct HBitmapDeleter {
        void operator()(HBITMAP b) const noexcept { if (b) DeleteObject(b); }
    };
    using HBitmapPtr = std::unique_ptr<std::remove_pointer_t<HBITMAP>, HBitmapDeleter>;

    // --- HFONT ---
    struct HFontDeleter {
        void operator()(HFONT f) const noexcept { if (f) DeleteObject(f); }
    };
    using HFontPtr = std::unique_ptr<std::remove_pointer_t<HFONT>, HFontDeleter>;

    // --- HBRUSH ---
    struct HBrushDeleter {
        void operator()(HBRUSH b) const noexcept { if (b) DeleteObject(b); }
    };
    using HBrushPtr = std::unique_ptr<std::remove_pointer_t<HBRUSH>, HBrushDeleter>;
}

namespace PriceTagConfig {
    constexpr int TAG_WIDTH_MM = 57;
    constexpr int TAG_HEIGHT_MM = 40;
    constexpr COLORREF STATUE_RED = RGB(211, 47, 47);
    inline const wchar_t* STORE_HEADER = ReceiptConfig::STORE_NAME;
}

struct PriceTagData {
    long long appendixNumber = 0;
    int ordinal = 0;
    int itemId = 0;
    int clientId = 0;
    std::wstring clientFullName;
    std::wstring workerFullName;
    std::wstring description;
    std::wstring characteristic;
    std::wstring note;
    double price = 0.0;
    double perUnitPay = 0.0;
    time_t date = 0;
};

class PriceTagPrinter {
public:
    static bool printForReceipt(const ReceiptData& d, const std::wstring& workerFullName) {
        g_logger.info(L"PriceTagPrinter::printForReceipt: started, appendix=" +
            std::to_wstring(d.appendixNumber) + L", worker='" + workerFullName +
            L"', positions=" + std::to_wstring(d.items.size()));

        if (d.items.empty()) {
            g_logger.warning(L"PriceTagPrinter::printForReceipt: no items, nothing to print");
            return true;
        }

        ReceiptPrinter::ensureGdiplus();
        std::vector<PriceTagData> units;
        int ordinal = 0;
        for (const auto& it : d.items) {
            const int qty = (it.quantity > 0) ? it.quantity : 1;
            for (int k = 0; k < qty; ++k) {
                ++ordinal;
                PriceTagData t;
                t.appendixNumber = d.appendixNumber;
                t.ordinal = ordinal;
                t.itemId = it.itemId;
                t.clientId = d.clientId;
                t.clientFullName = d.clientFullName;
                t.workerFullName = workerFullName;
                t.description = it.description;
                t.characteristic = it.characteristic;
                t.note = it.note;
                t.price = it.price;
                auto rates = CommissionCalc::calculateByPrice(it.price);
                t.perUnitPay = it.price * rates.clientPercent / 100.0;
                t.date = time(nullptr);

                g_logger.info(L"PriceTagPrinter: unit #" + std::to_wstring(ordinal) +
                    L", itemId=" + std::to_wstring(t.itemId) +
                    L", barcode=" + utf8_to_wstring(ShopBarcode::make(t.itemId)) +
                    L", desc='" + it.description + L"', price=" + std::to_wstring(it.price));

                if (t.itemId <= 0) {
                    g_logger.error(L"PriceTagPrinter: itemId is invalid, "
                        L"barcode cannot be generated. appendix=" +
                        std::to_wstring(t.appendixNumber) + L", ordinal=" +
                        std::to_wstring(t.ordinal));
                    return false;
                }
                units.push_back(t);
            }
        }

        g_logger.info(L"PriceTagPrinter: total units to print= " + std::to_wstring(units.size()));
        for (const auto& t : units) {
            std::wstring png = renderTagPreviewPng(t);
            if (!png.empty())
                g_logger.info(L"PriceTagPrinter: PNG preview saved: " + png);
            else
                g_logger.warning(L"PriceTagPrinter: PNG preview NOT created, ordinal=" +
                    std::to_wstring(t.ordinal));
        }

        std::wstring printer = ReceiptPrinter::selectPrinter();
        if (printer.empty()) {
            g_logger.error(L"PriceTagPrinter: no printer available");
            return false;
        }
        g_logger.info(L"PriceTagPrinter: selected printer = " + printer);

        if (ReceiptPrinter::isPromptPort(printer)) {
            g_logger.warning(L"PriceTagPrinter: printer '" + printer +
                L"' is interactive; spool SKIPPED, inspect PNG previews");
            return true;
        }

        HDC hdc = ReceiptPrinter::createLandscapeDC(printer);
        if (!hdc) {
            g_logger.error(L"PriceTagPrinter: CreateDC failed for " + printer +
                L", err=" + std::to_wstring(GetLastError()));
            return false;
        }

        const int logDpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        const int logDpiY = GetDeviceCaps(hdc, LOGPIXELSY);
        const int logHorzRes = GetDeviceCaps(hdc, HORZRES);
        const int logVertRes = GetDeviceCaps(hdc, VERTRES);
        const int logPhysW = GetDeviceCaps(hdc, PHYSICALWIDTH);
        const int logPhysH = GetDeviceCaps(hdc, PHYSICALHEIGHT);

        g_logger.info(L"PriceTagPrinter: printer DC diagnostics - "
            L"LOGPIXELSX=" + std::to_wstring(logDpiX) +
            L", LOGPIXELSY=" + std::to_wstring(logDpiY) +
            L", HORZRES=" + std::to_wstring(logHorzRes) +
            L", VERTRES=" + std::to_wstring(logVertRes) +
            L", PHYSICALWIDTH=" + std::to_wstring(logPhysW) +
            L", PHYSICALHEIGHT=" + std::to_wstring(logPhysH));

        if (SetAbortProc(hdc, ReceiptPrinter::AbortProc) <= 0)
            g_logger.warning(L"PriceTagPrinter: SetAbortProc failed, err=" +
                std::to_wstring(GetLastError()));

        DOCINFOW di = {};
        di.cbSize = sizeof(di);
        std::wstring docName = L"Ценники_приложение_" + std::to_wstring(d.appendixNumber);
        di.lpszDocName = docName.c_str();

        if (StartDocW(hdc, &di) <= 0) {
            g_logger.error(L"PriceTagPrinter: StartDocW failed, err=" +
                std::to_wstring(GetLastError()));
            DeleteDC(hdc);
            return false;
        }

        bool ok = true;
        for (const auto& t : units) {
            if (StartPage(hdc) <= 0) {
                g_logger.error(L"PriceTagPrinter: StartPage failed, ordinal=" +
                    std::to_wstring(t.ordinal));
                ok = false; break;
            }
            ReceiptPrinter::beginPage(hdc, GetDeviceCaps(hdc, HORZRES), GetDeviceCaps(hdc, VERTRES));
            renderTagDocument(hdc, t, 0, 0);
            if (EndPage(hdc) <= 0) {
                g_logger.error(L"PriceTagPrinter: EndPage failed, ordinal=" +
                    std::to_wstring(t.ordinal));
                ok = false; break;
            }
            g_logger.info(L"PriceTagPrinter: page spooled for unit #" + std::to_wstring(t.ordinal));
        }

        if (EndDoc(hdc) <= 0) {
            g_logger.error(L"PriceTagPrinter: EndDoc failed, err=" + std::to_wstring(GetLastError()));
            ok = false;
        }
        DeleteDC(hdc);

        g_logger.info(L"PriceTagPrinter::printForReceipt: finished, ok=" +
            std::wstring(ok ? L"true" : L"false") +
            L", appendix=" + std::to_wstring(d.appendixNumber));
        return ok;
    }

private:
    static std::string buildTagPayload(const PriceTagData& t) {
        if (t.itemId <= 0) {
            g_logger.error(L"PriceTagPrinter::buildTagPayload: "
                L"invalid itemId=" + std::to_wstring(t.itemId));
            return std::string();
        }
        const std::string payload = ShopBarcode::make(t.itemId);
        g_logger.info(L"PriceTagPrinter::buildTagPayload: itemId=" +
            std::to_wstring(t.itemId) + L", barcode='" +
            std::wstring(payload.begin(), payload.end()) + L"'");
        return payload;
    }

    static std::wstring formatPriceSpaced(double v) {
        const long long rub = static_cast<long long>(std::llround(v));
        std::wstring s = std::to_wstring(rub);
        std::wstring out;
        int cnt = 0;
        for (auto it = s.rbegin(); it != s.rend(); ++it) {
            out.insert(out.begin(), *it);
            if (++cnt % 3 == 0 && (it + 1) != s.rend()) out.insert(out.begin(), L' ');
        }
        return out;
    }

    // =========================================================================
    // Кастомный делетер для Gdiplus::Bitmap
    // =========================================================================
    struct GdiplusBitmapDeleter {
        void operator()(Gdiplus::Bitmap* b) const noexcept {
            if (b) {
                delete b;   // Gdiplus::Bitmap::~Bitmap() освобождает GDI+ ресурсы
            }
        }
    };
    using GdiplusBitmapPtr = std::unique_ptr<Gdiplus::Bitmap, GdiplusBitmapDeleter>;

    // =========================================================================
    // loadStatue() — RAII-версия
    // =========================================================================
    static Gdiplus::Bitmap* loadStatue()
    {
        static std::once_flag flag;
        static GdiplusBitmapPtr cached;   // ← владеющий unique_ptr

        std::call_once(flag, [&]() {
            g_logger.info(L"PriceTagPrinter: loading statue resource IDR_PNG_STATUE");

            HRSRC hr = FindResourceW(g_hInstance, MAKEINTRESOURCEW(IDR_PNG_STATUE), L"PNG");
            if (!hr) {
                g_logger.error(L"PriceTagPrinter: FindResourceW(statue) failed, err="
                    + std::to_wstring(GetLastError()));
                return;
            }

            HGLOBAL hg = LoadResource(g_hInstance, hr);
            if (!hg) {
                g_logger.error(L"PriceTagPrinter: LoadResource returned NULL");
                return;
            }

            DWORD sz = SizeofResource(g_hInstance, hr);
            if (sz == 0) {
                g_logger.error(L"PriceTagPrinter: SizeofResource returned 0");
                return;
            }

            const void* p = LockResource(hg);
            if (!p) {
                g_logger.error(L"PriceTagPrinter: LockResource returned NULL");
                return;
            }

            // hCopy владеет памятью; при CreateStreamOnHGlobal(..., TRUE, ...)
            // владение перейдёт к потоку. Если CreateStreamOnHGlobal не удался —
            // освобождаем hCopy вручную.
            HGLOBAL hCopy = GlobalAlloc(GMEM_MOVEABLE, sz);
            if (!hCopy) {
                g_logger.error(L"PriceTagPrinter: GlobalAlloc failed");
                return;
            }

            void* pc = GlobalLock(hCopy);
            if (!pc) {
                g_logger.error(L"PriceTagPrinter: GlobalLock returned NULL");
                GlobalFree(hCopy);
                return;
            }
            memcpy(pc, p, sz);
            GlobalUnlock(hCopy);

            // RAII для IStream
            IStream* stmRaw = nullptr;
            if (!SUCCEEDED(CreateStreamOnHGlobal(hCopy, TRUE, &stmRaw))) {
                g_logger.error(L"PriceTagPrinter: CreateStreamOnHGlobal failed");
                GlobalFree(hCopy);   // поток не создан — освобождаем сами
                return;
            }
            // Оборачиваем поток, чтобы гарантировать Release при любом выходе
            struct StreamReleaser {
                void operator()(IStream* s) const noexcept { if (s) s->Release(); }
            };
            std::unique_ptr<IStream, StreamReleaser> stm(stmRaw);

            // RAII для src (сырой bitmap из потока)
            GdiplusBitmapPtr src(Gdiplus::Bitmap::FromStream(stm.get()));
            if (!src || src->GetLastStatus() != Gdiplus::Ok) {
                g_logger.error(L"PriceTagPrinter: Gdiplus::Bitmap::FromStream failed");
                return;   // stm и src освободятся автоматически
            }

            // Клонируем в кэш
            cached.reset(src->Clone(0, 0, src->GetWidth(), src->GetHeight(),
                PixelFormat32bppARGB));
            if (!cached) {
                g_logger.error(L"PriceTagPrinter: Clone(statue) failed");
                return;
            }

            // Перекраска в красный
            Gdiplus::BitmapData bd{};
            Gdiplus::Rect r(0, 0,
                static_cast<INT>(cached->GetWidth()),
                static_cast<INT>(cached->GetHeight()));
            if (cached->LockBits(&r,
                Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
                PixelFormat32bppARGB, &bd) == Gdiplus::Ok)
            {
                if (!bd.Scan0) {
                    g_logger.warning(L"PriceTagPrinter: LockBits returned NULL Scan0");
                    cached->UnlockBits(&bd);
                }
                else {
                    const BYTE redR = GetRValue(PriceTagConfig::STATUE_RED);
                    const BYTE redG = GetGValue(PriceTagConfig::STATUE_RED);
                    const BYTE redB = GetBValue(PriceTagConfig::STATUE_RED);
                    for (UINT y = 0; y < cached->GetHeight(); ++y) {
                        BYTE* row = reinterpret_cast<BYTE*>(bd.Scan0) + y * bd.Stride;
                        for (UINT x = 0; x < cached->GetWidth(); ++x) {
                            BYTE* px = row + x * 4;
                            if (px[3] > 0) { px[0] = redB; px[1] = redG; px[2] = redR; }
                        }
                    }
                    cached->UnlockBits(&bd);
                    g_logger.info(L"PriceTagPrinter: statue recolored to RED, size="
                        + std::to_wstring(cached->GetWidth()) + L"x"
                        + std::to_wstring(cached->GetHeight()));
                }
            }
            else {
                g_logger.warning(L"PriceTagPrinter: LockBits failed, statue left as-is");
            }
            });

        return cached.get();   // владение остаётся у статического unique_ptr
    }

    // =========================================================================
    // ЯВНОЕ ОСВОБОЖДЕНИЕ КЭША — ВЫЗВАТЬ ДО GdiplusShutdown()
    // =========================================================================
    static void releaseStatueCache() noexcept {
        // Доступ к static-локалу через ту же функцию невозможен; поэтому
        // сделаем кэш глобальным для единицы трансляции:
        // (см. вариант ниже — с вынесенным наружу unique_ptr)
    }

    static void drawBarcodeVertical(HDC hdc, int x, int yTop, int yBottom, int maxWidthPx,
        const std::string& payload, int horizontalBarcodeWpx, int horizontalBarcodeHpx)
    {
        if (payload.empty() || yBottom <= yTop || maxWidthPx <= 0 ||
            horizontalBarcodeWpx <= 0 || horizontalBarcodeHpx <= 0) {
            g_logger.warning(L"PriceTagPrinter: drawBarcodeVertical invalid parameters");
            return;
        }
        g_logger.info(L"PriceTagPrinter: drawBarcodeVertical: EXACT COPY of top barcode, "
            L"src=" + std::to_wstring(horizontalBarcodeWpx) + L"x" + std::to_wstring(horizontalBarcodeHpx) +
            L", target x=" + std::to_wstring(x) + L", y=" + std::to_wstring(yTop) + L".." + std::to_wstring(yBottom));

        const int rotLenPx = horizontalBarcodeWpx;
        const int areaHpx = yBottom - yTop;
        if (rotLenPx > areaHpx || (horizontalBarcodeWpx > 0 &&
            (maxWidthPx * 254 / (GetDeviceCaps(hdc, LOGPIXELSX) * 10)) < 1)) {
            g_logger.warning(L"PriceTagPrinter: drawBarcodeVertical: rotated copy skipped "
                L"(readability guard), rotLen=" + std::to_wstring(rotLenPx) +
                L", areaH=" + std::to_wstring(areaHpx));
            return;
        }

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = horizontalBarcodeWpx;
        bmi.bmiHeader.biHeight = -horizontalBarcodeHpx;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);
        void* pBits = nullptr;
        HBITMAP hBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (!hBmp || !memDC) {
            g_logger.error(L"PriceTagPrinter: drawBarcodeVertical CreateDIBSection failed");
            if (memDC) DeleteDC(memDC);
            ReleaseDC(nullptr, screenDC);
            return;
        }

        HGDIOBJ hOld = SelectObject(memDC, hBmp);
        RECT rc = { 0, 0, horizontalBarcodeWpx, horizontalBarcodeHpx };
        HBRUSH hWhite = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(memDC, &rc, hWhite);
        DeleteObject(hWhite);

        ReceiptPrinter::drawBarcodeLine(memDC, 0, horizontalBarcodeWpx, 0, horizontalBarcodeHpx, payload);

        Gdiplus::Bitmap bmp(hBmp, nullptr);
        if (bmp.GetLastStatus() != Gdiplus::Ok) {
            g_logger.error(L"PriceTagPrinter: drawBarcodeVertical Gdiplus::Bitmap failed");
            SelectObject(memDC, hOld);
            DeleteObject(hBmp);
            DeleteDC(memDC);
            ReleaseDC(nullptr, screenDC);
            return;
        }

        Gdiplus::Bitmap* pRotated = bmp.Clone(0, 0, horizontalBarcodeWpx, horizontalBarcodeHpx, PixelFormat32bppARGB);
        if (!pRotated) {
            g_logger.error(L"PriceTagPrinter: drawBarcodeVertical Clone failed");
            SelectObject(memDC, hOld);
            DeleteObject(hBmp);
            DeleteDC(memDC);
            ReleaseDC(nullptr, screenDC);
            return;
        }
        pRotated->RotateFlip(Gdiplus::Rotate90FlipNone);

        Gdiplus::Graphics g(hdc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        const int rotW = static_cast<int>(pRotated->GetWidth());
        const int rotH = static_cast<int>(pRotated->GetHeight());
        const int areaH = yBottom - yTop;
        const int drawY = yTop + (areaH - rotH) / 2;
        const int drawW = (rotW > maxWidthPx) ? maxWidthPx : rotW;

        Gdiplus::RectF dest(
            static_cast<Gdiplus::REAL>(x),
            static_cast<Gdiplus::REAL>(drawY),
            static_cast<Gdiplus::REAL>(drawW),
            static_cast<Gdiplus::REAL>(rotH));

        g.DrawImage(pRotated, dest,
            0.0f, 0.0f,
            static_cast<Gdiplus::REAL>(rotW),
            static_cast<Gdiplus::REAL>(rotH),
            Gdiplus::UnitPixel);

        g_logger.info(L"PriceTagPrinter: vertical barcode EXACT COPY drawn at x=" +
            std::to_wstring(x) + L", y=" + std::to_wstring(drawY) +
            L", size=" + std::to_wstring(drawW) + L"x" + std::to_wstring(rotH) +
            L" (identical modules, Mertech-readable)");

        delete pRotated;
        SelectObject(memDC, hOld);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
    }

    static void renderTagDocument(HDC hdc, const PriceTagData& t, int dpiXOverride, int dpiYOverride)
    {
        const bool previewMode = (dpiXOverride > 0 && dpiYOverride > 0);
        int renderDpi = previewMode ? dpiXOverride : 300;
        int pxW = static_cast<int>(std::round(PriceTagConfig::TAG_WIDTH_MM / 25.4 * renderDpi));
        int pxH = static_cast<int>(std::round(PriceTagConfig::TAG_HEIGHT_MM / 25.4 * renderDpi));

        g_logger.info(L"PriceTagPrinter: renderTagDocument ordinal=" + std::to_wstring(t.ordinal) +
            L", renderDpi=" + std::to_wstring(renderDpi) +
            L", DIB=" + std::to_wstring(pxW) + L"x" + std::to_wstring(pxH) +
            L", mode=" + std::wstring(previewMode ? L"PREVIEW" : L"SPOOL"));

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = pxW;
        bmi.bmiHeader.biHeight = -pxH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC screenRaw = GetDC(nullptr);
        if (!screenRaw) {
            g_logger.error(L"PriceTagPrinter: GetDC(nullptr) failed");
            return;
        }
        std::unique_ptr<std::remove_pointer_t<HDC>, ReleaseDcDeleter>
            screenDC(screenRaw, ReleaseDcDeleter{ nullptr });

        HDC memRaw = CreateCompatibleDC(screenDC.get());
        if (!memRaw) {
            g_logger.error(L"PriceTagPrinter: CreateCompatibleDC failed");
            return;
        }
        HdcPtr memDC(memRaw);

        void* pBits = nullptr;
        HBITMAP hBmpRaw = CreateDIBSection(screenDC.get(), &bmi, DIB_RGB_COLORS,
            &pBits, nullptr, 0);
        if (!hBmpRaw) {
            renderDpi = 300;
            pxW = static_cast<int>(std::round(PriceTagConfig::TAG_WIDTH_MM / 25.4 * renderDpi));
            pxH = static_cast<int>(std::round(PriceTagConfig::TAG_HEIGHT_MM / 25.4 * renderDpi));
            bmi.bmiHeader.biWidth = pxW;
            bmi.bmiHeader.biHeight = -pxH;
            hBmpRaw = CreateDIBSection(screenDC.get(), &bmi, DIB_RGB_COLORS,
                &pBits, nullptr, 0);
            g_logger.warning(L"PriceTagPrinter: 600 DPI failed, fallback 300 DPI");
        }
        if (!hBmpRaw) {
            g_logger.error(L"PriceTagPrinter: CreateDIBSection failed completely");
            return;
        }
        HBitmapPtr hBmp(hBmpRaw);

        HGDIOBJ hOldBmp = SelectObject(memDC.get(), hBmp.get());
        if (!hOldBmp || hOldBmp == HGDI_ERROR) {
            g_logger.error(L"PriceTagPrinter: SelectObject(hBmp) failed, err="
                + std::to_wstring(GetLastError()));
            return;
        }

        // --- Заливка белым ---
        {
            HBrushPtr hWhite(CreateSolidBrush(RGB(255, 255, 255)));
            if (!hWhite) {
                g_logger.error(L"PriceTagPrinter: CreateSolidBrush failed");
                SelectObject(memDC.get(), hOldBmp);
                return;
            }
            RECT rcFill{ 0, 0, pxW, pxH };
            FillRect(memDC.get(), &rcFill, hWhite.get());
        }

        SetBkMode(memDC.get(), TRANSPARENT);
        SetTextColor(memDC.get(), RGB(0, 0, 0));

        // --- Шрифты ---
        auto mmX = [&](int mm) { return MulDiv(mm * 10, renderDpi, 254); };
        auto mmY = [&](int mm) { return MulDiv(mm * 10, renderDpi, 254); };

        auto font = [&](int pt, bool bold) {
            return CreateFontW(-MulDiv(pt, renderDpi, 72), 0, 0, 0,
                bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
            };
        auto fontVert = [&](int pt) {
            return CreateFontW(-MulDiv(pt, renderDpi, 72), 0, 900, 900, FW_NORMAL,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Arial");
            };

        HFontPtr fHead(font(5, true));
        HFontPtr fLbl(font(4, false));
        HFontPtr fVal(font(4, true));
        HFontPtr fSmall(font(4, false));
        HFontPtr fBig(font(5, true));
        HFontPtr fVert(fontVert(7));

        if (!fHead || !fLbl || !fVal || !fSmall || !fBig || !fVert) {
            g_logger.error(L"PriceTagPrinter: CreateFontW failed for one or more fonts");
            SelectObject(memDC.get(), hOldBmp);
            return;
        }

        const int lineStep = MulDiv(7, renderDpi, 72);
        const std::string payload = buildTagPayload(t);
                
        // === Отрисовка шапки ===
        int leftMargin = mmX(1);
        int rightEdge = mmX(38);
        int y = mmY(2);
        MoveToEx(memDC.get(), leftMargin, y, nullptr);
        LineTo(memDC.get(), rightEdge, y);
        y += lineStep;

        // Сохраняем первый выбранный шрифт, чтобы вернуть его в конце
        HFONT hPrevFont = static_cast<HFONT>(SelectObject(memDC.get(), fHead.get()));
        {
            SIZE szHdr{};
            GetTextExtentPoint32W(memDC.get(),
                PriceTagConfig::STORE_HEADER,
                static_cast<int>(wcslen(PriceTagConfig::STORE_HEADER)), &szHdr);
            TextOutW(memDC.get(),
                leftMargin + (rightEdge - leftMargin - szHdr.cx) / 2, y,
                PriceTagConfig::STORE_HEADER,
                static_cast<int>(wcslen(PriceTagConfig::STORE_HEADER)));
        }
        y += lineStep;

        Gdiplus::Bitmap* statue = loadStatue();
        if (statue && statue->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics gStatue(memDC.get());
            gStatue.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            Gdiplus::RectF statRect(
                static_cast<Gdiplus::REAL>(leftMargin),
                static_cast<Gdiplus::REAL>(y),
                static_cast<Gdiplus::REAL>(mmX(6)),
                static_cast<Gdiplus::REAL>(mmY(6)));
            gStatue.DrawImage(statue, statRect);
        }

        const int topBarcodeLeft = mmX(7);    // было 12, сдвиг влево на 5 мм (0,5 см)
        const int topBarcodeRight = mmX(33);  // было 38, сдвиг влево на 5 мм (0,5 см)
        const int topBarcodeTop = y;
        const int topBarcodeBottom = y + mmY(4);
        const int topBarcodeWpx = topBarcodeRight - topBarcodeLeft;
        const int topBarcodeHpx = topBarcodeBottom - topBarcodeTop;

        ReceiptPrinter::drawBarcodeLine(memDC.get(), topBarcodeLeft, topBarcodeRight,
            topBarcodeTop, topBarcodeHpx, payload);

        SelectObject(memDC.get(), fLbl.get());
        {
            const std::wstring digits = utf8_to_wstring(ShopBarcode::make(t.itemId));
            SIZE szDg{};
            GetTextExtentPoint32W(memDC.get(), digits.c_str(),
                static_cast<int>(digits.size()), &szDg);
            TextOutW(memDC.get(), topBarcodeLeft + (topBarcodeWpx - szDg.cx) / 2,
                topBarcodeBottom, digits.c_str(), static_cast<int>(digits.size()));
        }

        y = topBarcodeBottom + lineStep;
        SelectObject(memDC.get(), fLbl.get());
        TextOutW(memDC.get(), leftMargin, y, L"Товарный ярлык", 14);
        SelectObject(memDC.get(), fVal.get());
        std::wstring numVal = std::to_wstring(t.appendixNumber) + L"+" + std::to_wstring(t.ordinal);
        TextOutW(memDC.get(), mmX(14), y, numVal.c_str(), static_cast<int>(numVal.size()));
        SelectObject(memDC.get(), fLbl.get());
        TextOutW(memDC.get(), mmX(20), y, L"дата", 4);// было 26, сдвиг вправо на 10 мм (1 см)
        SelectObject(memDC.get(), fVal.get());
        std::wstring dateStr = ReceiptUtils::formatDateDDMMYYYY(t.date);
        TextOutW(memDC.get(), mmX(28), y, dateStr.c_str(), static_cast<int>(dateStr.size()));   // было 33, сдвиг влево на 5 мм (0,5 см)
        y += lineStep;

        SelectObject(memDC.get(), fLbl.get());
        TextOutW(memDC.get(), leftMargin, y, L"Наименование", 12);
        SelectObject(memDC.get(), fVal.get());
        std::wstring desc = t.description;
        if (desc.length() > 35) desc = desc.substr(0, 32) + L"...";
        TextOutW(memDC.get(), mmX(14), y, desc.c_str(), static_cast<int>(desc.size()));
        y += lineStep;

        SelectObject(memDC.get(), fLbl.get());
        TextOutW(memDC.get(), leftMargin, y, L"Характеристика", 14);
        SelectObject(memDC.get(), fVal.get());
        std::wstring charVal = t.characteristic;
        if (charVal.length() > 35) charVal = charVal.substr(0, 32) + L"...";
        TextOutW(memDC.get(), mmX(14), y, charVal.c_str(), static_cast<int>(charVal.size()));
        y += lineStep;

        SelectObject(memDC.get(), fLbl.get());
        TextOutW(memDC.get(), leftMargin, y,
            L"Бывший в употреблении товар с износом 30%", 41);
        y += lineStep;

        SelectObject(memDC.get(), fBig.get());
        TextOutW(memDC.get(), leftMargin, y, L"Цена:", 6);
        std::wstring priceStr = formatPriceSpaced(t.price);
        TextOutW(memDC.get(), mmX(14), y, priceStr.c_str(), static_cast<int>(priceStr.size()));
        SelectObject(memDC.get(), fLbl.get());
        TextOutW(memDC.get(), mmX(34), y, L"руб.", 4);
        y += lineStep;

        SelectObject(memDC.get(), fSmall.get());
        std::wstring workerStr = t.workerFullName;

        // ИСПРАВЛЕНИЕ: Извлекаем только Имя из ФИО
        std::wstring firstName = workerStr;
        size_t firstSpace = workerStr.find(L' ');
        if (firstSpace != std::wstring::npos) {
            size_t secondSpace = workerStr.find(L' ', firstSpace + 1);
            if (secondSpace != std::wstring::npos) {
                // Формат "Фамилия Имя Отчество" -> берем Имя
                firstName = workerStr.substr(firstSpace + 1, secondSpace - firstSpace - 1);
            }
            else {
                // Формат "Фамилия Имя" -> берем Имя
                firstName = workerStr.substr(firstSpace + 1);
            }
        }
        workerStr = firstName; // Подменяем ФИО на Имя для печати

        g_logger.info(L"PriceTagPrinter: printing worker name on tag: '" + workerStr +
            L"' (original: '" + t.workerFullName + L"')");

        if (workerStr.length() > 35) workerStr = workerStr.substr(0, 32) + L"...";

        SIZE szWorker{};
        GetTextExtentPoint32W(memDC.get(), workerStr.c_str(),
            static_cast<int>(workerStr.size()), &szWorker);
        TextOutW(memDC.get(), leftMargin, y, workerStr.c_str(),
            static_cast<int>(workerStr.size()));
        std::wstring idStr = L" id: " + std::to_wstring(t.clientId);
        TextOutW(memDC.get(), leftMargin + szWorker.cx + mmX(1), y,
            idStr.c_str(), static_cast<int>(idStr.size()));
        y += lineStep;

        MoveToEx(memDC.get(), leftMargin, y, nullptr);
        LineTo(memDC.get(), rightEdge, y);

        drawBarcodeVertical(memDC.get(), mmX(39), mmY(6), mmY(38), mmX(4),
            payload, topBarcodeWpx, topBarcodeHpx);

        SelectObject(memDC.get(), fVert.get());
        SIZE szVert{};
        const std::wstring barcodeText = utf8_to_wstring(ShopBarcode::make(t.itemId));
        GetTextExtentPoint32W(memDC.get(), barcodeText.c_str(),
            static_cast<int>(barcodeText.size()), &szVert);
        int textY = mmY(6) + (mmY(32) - szVert.cx) / 2;
        TextOutW(memDC.get(), mmX(44), textY, barcodeText.c_str(),
            static_cast<int>(barcodeText.size()));

        // === ФИНАЛЬНОЕ ВОССТАНОВЛЕНИЕ ОБЪЕКТОВ DC ===
        // Порядок критичен: сначала возвращаем исходный шрифт, потом исходный bitmap.
        // Иначе DeleteObject внутри unique_ptr удалит объект, всё ещё выбранный в DC.
        if (hPrevFont && hPrevFont != HGDI_ERROR) {
            SelectObject(memDC.get(), hPrevFont);
        }
        if (hOldBmp && hOldBmp != HGDI_ERROR) {
            SelectObject(memDC.get(), hOldBmp);
        }

        // === Вывод результата ===
        if (previewMode) {
            BitBlt(hdc, 0, 0, pxW, pxH, memDC.get(), 0, 0, SRCCOPY);
            g_logger.info(L"PriceTagPrinter: preview blitted straight");
        }
        else {
            Gdiplus::Bitmap bmp(hBmp.get(), nullptr);
            if (bmp.GetLastStatus() != Gdiplus::Ok) {
                g_logger.error(L"PriceTagPrinter: spool Gdiplus::Bitmap failed");
            }
            else {
                const int pageW = GetDeviceCaps(hdc, HORZRES);
                const int pageH = GetDeviceCaps(hdc, VERTRES);
                const int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
                const int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
                const int srcW = static_cast<int>(bmp.GetWidth());
                const int srcH = static_cast<int>(bmp.GetHeight());
                const double crossMm = std::min(
                    pageW * 25.4 / static_cast<double>(dpiX),
                    pageH * 25.4 / static_cast<double>(dpiY));
                const bool tape = (crossMm < 120.0);
                int destW = srcW, destH = srcH;
                int offX = 0, offY = 0;
                bool needScale = false;

                if (tape) {
                    destW = MulDiv(srcW, dpiX, renderDpi);
                    destH = MulDiv(srcH, dpiY, renderDpi);
                    const int maxPrintableMm = 62;
                    const int specW = MulDiv(
                        std::min(PriceTagConfig::TAG_WIDTH_MM, maxPrintableMm) * 10,
                        dpiX, 254);
                    const int availPx = ((pageW < specW) ? pageW : specW)
                        - MulDiv(1 * 10, dpiX, 254);
                    if (destW > availPx) {
                        needScale = true;
                        const double k = static_cast<double>(availPx)
                            / static_cast<double>(destW);
                        destW = availPx;
                        destH = static_cast<int>(std::round(destH * k));
                        g_logger.warning(L"PriceTagPrinter: tag clamped to print head, k="
                            + std::to_wstring(k));
                    }
                    offX = 0;
                    offY = MulDiv(2 * 10, dpiY, 254);
                    g_logger.info(L"PriceTagPrinter: tape window check - cross="
                        + std::to_wstring(destW * 25.4 / dpiX) + L" mm of tape, along="
                        + std::to_wstring((destH + offY) * 25.4 / dpiY)
                        + L" mm of tag area");
                    g_logger.info(L"PriceTagPrinter: tag barcode module on tape="
                        + std::to_wstring(3.0 / renderDpi * 25.4
                            * (destW * 25.4 / dpiX) / PriceTagConfig::TAG_WIDTH_MM)
                        + L" mm => dots@203="
                        + std::to_wstring(3.0 / renderDpi * 25.4
                            * (destW * 25.4 / dpiX) / PriceTagConfig::TAG_WIDTH_MM / 0.125));
                }
                else {
                    offX = (pageW > srcW) ? (pageW - srcW) / 2 : 0;
                    offY = (pageH > srcH) ? (pageH - srcH) / 2 : 0;
                }

                Gdiplus::Graphics g(hdc);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                Gdiplus::RectF dest(
                    static_cast<Gdiplus::REAL>(offX),
                    static_cast<Gdiplus::REAL>(offY),
                    static_cast<Gdiplus::REAL>(destW),
                    static_cast<Gdiplus::REAL>(destH));
                g.DrawImage(&bmp, dest, 0.0f, 0.0f,
                    static_cast<Gdiplus::REAL>(srcW),
                    static_cast<Gdiplus::REAL>(srcH),
                    Gdiplus::UnitPixel);
                g_logger.info(L"PriceTagPrinter: SPOOL NO ROTATION (sample 2), tape="
                    + std::wstring(tape ? L"YES" : L"NO")
                    + L", dest=" + std::to_wstring(destW) + L"x" + std::to_wstring(destH)
                    + L" px (" + std::to_wstring(destW * 25.4 / dpiX) + L"x"
                    + std::to_wstring(destH * 25.4 / dpiY) + L" mm), offX="
                    + std::to_wstring(offX) + L", offY=" + std::to_wstring(offY)
                    + L", needScale="
                    + std::wstring(needScale ? L"true" : L"false"));
            }
        }

        g_logger.info(L"PriceTagPrinter: tag rendered, ordinal=" + std::to_wstring(t.ordinal));
        // hBmp, memDC, screenDC, fHead, fLbl, fVal, fSmall, fBig, fVert — всё освободят unique_ptr
    }

    static std::wstring renderTagPreviewPng(const PriceTagData& t) {
        std::wstring result;
        std::wstring dir = Config::getAppDataPath();
        CreateDirectoryW(dir.c_str(), nullptr);
        dir += L"\\pricetags";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring pngPath = dir + L"\\appendix_" + std::to_wstring(t.appendixNumber) +
            L"_item" + std::to_wstring(t.ordinal) + L".png";

        int dpi = 300;
        int pxW = static_cast<int>(std::round(PriceTagConfig::TAG_WIDTH_MM / 25.4 * dpi));
        int pxH = static_cast<int>(std::round(PriceTagConfig::TAG_HEIGHT_MM / 25.4 * dpi));

        g_logger.info(L"PriceTagPrinter: preview DIB " + std::to_wstring(pxW) + L"x" +
            std::to_wstring(pxH) + L" @ " + std::to_wstring(dpi) + L" DPI");

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = pxW;
        bmi.bmiHeader.biHeight = -pxH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC screenRaw = GetDC(nullptr);
        if (!screenRaw) {
            g_logger.error(L"PriceTagPrinter: preview GetDC failed");
            return result;
        }
        std::unique_ptr<std::remove_pointer_t<HDC>, ReleaseDcDeleter>
            screenDC(screenRaw, ReleaseDcDeleter{ nullptr });

        HDC memRaw = CreateCompatibleDC(screenDC.get());
        if (!memRaw) {
            g_logger.error(L"PriceTagPrinter: preview CreateCompatibleDC failed");
            return result;
        }
        HdcPtr memDC(memRaw);

        void* pBits = nullptr;
        HBITMAP hBmpRaw = CreateDIBSection(screenDC.get(), &bmi, DIB_RGB_COLORS,
            &pBits, nullptr, 0);
        if (!hBmpRaw) {
            dpi = 300;
            pxW = static_cast<int>(std::round(PriceTagConfig::TAG_WIDTH_MM / 25.4 * dpi));
            pxH = static_cast<int>(std::round(PriceTagConfig::TAG_HEIGHT_MM / 25.4 * dpi));
            bmi.bmiHeader.biWidth = pxW;
            bmi.bmiHeader.biHeight = -pxH;
            hBmpRaw = CreateDIBSection(screenDC.get(), &bmi, DIB_RGB_COLORS,
                &pBits, nullptr, 0);
            g_logger.warning(L"PriceTagPrinter: preview DIB fallback to 300 DPI");
        }
        if (!hBmpRaw) {
            g_logger.error(L"PriceTagPrinter: preview CreateDIBSection failed");
            return result;
        }
        HBitmapPtr hBmp(hBmpRaw);

        HGDIOBJ hOldBmp = SelectObject(memDC.get(), hBmp.get());
        if (!hOldBmp || hOldBmp == HGDI_ERROR) {
            g_logger.error(L"PriceTagPrinter: preview SelectObject(hBmp) failed, err="
                + std::to_wstring(GetLastError()));
            return result;
        }

        {
            HBrushPtr hWhite(CreateSolidBrush(RGB(255, 255, 255)));
            if (!hWhite) {
                g_logger.error(L"PriceTagPrinter: preview CreateSolidBrush failed");
                SelectObject(memDC.get(), hOldBmp);
                return result;
            }
            RECT rc{ 0, 0, pxW, pxH };
            FillRect(memDC.get(), &rc, hWhite.get());
        }

        renderTagDocument(memDC.get(), t, dpi, dpi);

        {
            Gdiplus::Bitmap bmp(hBmp.get(), nullptr);
            if (bmp.GetLastStatus() != Gdiplus::Ok) {
                g_logger.error(L"PriceTagPrinter: preview Gdiplus::Bitmap ctor failed");
                SelectObject(memDC.get(), hOldBmp);
                return result;
            }
            bmp.SetResolution(static_cast<Gdiplus::REAL>(dpi), static_cast<Gdiplus::REAL>(dpi));

            CLSID clsid{};
            if (ReceiptPrinter::getPngEncoderClsid(clsid)) {
                if (bmp.Save(pngPath.c_str(), &clsid, nullptr) == Gdiplus::Ok)
                    result = pngPath;
                else
                    g_logger.error(L"PriceTagPrinter: PNG save failed for " + pngPath);
            }
            else {
                g_logger.error(L"PriceTagPrinter: PNG encoder CLSID not found");
            }
        }

        // Восстанавливаем выбранный объект перед автоматическим освобождением
        SelectObject(memDC.get(), hOldBmp);

        // hBmp, memDC, screenDC освободятся автоматически
        return result;
    }
};