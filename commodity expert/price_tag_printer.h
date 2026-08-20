// price_tag_printer.h
// =============================================================================
// МОДУЛЬ ПЕЧАТИ ЦЕННИКОВ 100 x 50 мм (вертикальная ориентация)
// =============================================================================
// ОБНОВЛЕНИЕ: Расчет perUnitPay (выплата на единицу товара) теперь производится
// по новой логике автоматического расчета процентов (CommissionCalc::calculateByPrice).
// Вместо деления clientAmount на количество, используется формула:
//   perUnitPay = price * clientPercent / 100
// где clientPercent определяется по цене единицы товара.
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

extern Logger g_logger;
extern HINSTANCE g_hInstance;

#ifndef IDR_PNG_STATUE
#define IDR_PNG_STATUE 300
#endif

namespace PriceTagConfig {
    constexpr int TAG_WIDTH_MM = 100;
    constexpr int TAG_HEIGHT_MM = 50;
    constexpr COLORREF STATUE_RED = RGB(211, 47, 47);
    inline const wchar_t* STORE_HEADER = ReceiptConfig::STORE_NAME;
}

struct PriceTagData {
    long long appendixNumber = 0;
    int ordinal = 0;
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
                t.clientId = d.clientId;
                t.clientFullName = d.clientFullName;
                t.workerFullName = workerFullName;
                t.description = it.description;
                t.characteristic = it.characteristic;
                t.note = it.note;
                t.price = it.price;

                // =====================================================================
                // НОВОЕ: РАСЧЕТ perUnitPay ПО НОВОЙ ЛОГИКЕ
                // Используем CommissionCalc::calculateByPrice для определения
                // процента комитента по цене единицы товара
                // =====================================================================
                auto rates = CommissionCalc::calculateByPrice(it.price);
                t.perUnitPay = it.price * rates.clientPercent / 100.0;
                g_logger.info(L"PriceTagPrinter: unit #" + std::to_wstring(ordinal) +
                    L" desc='" + it.description +
                    L"', price=" + std::to_wstring(it.price) +
                    L", clientPercent=" + std::to_wstring(rates.clientPercent) +
                    L"%, perUnitPay=" + std::to_wstring(t.perUnitPay));
                // =====================================================================

                t.date = time(nullptr);
                units.push_back(t);
            }
        }
        g_logger.info(L"PriceTagPrinter: total units to print=" + std::to_wstring(units.size()));

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

        if (SetAbortProc(hdc, ReceiptPrinter::AbortProc) <= 0)
            g_logger.warning(L"PriceTagPrinter: SetAbortProc failed, err=" +
                std::to_wstring(GetLastError()));

        DOCINFOW di = {};
        di.cbSize = sizeof(di);
        std::wstring docName = L"Ценники_приложения_" + std::to_wstring(d.appendixNumber);
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
        std::ostringstream ss;
        char num[64] = {};
        ss << "FIO=" << ReceiptUtils::transliterate(t.clientFullName)
            << ";ID=" << t.clientId
            << ";NAME=" << ReceiptUtils::transliterate(t.description)
            << ";PRICE=";
        sprintf_s(num, "%.2f", t.price); ss << num;
        ss << ";PAY=";
        sprintf_s(num, "%.2f", t.perUnitPay); ss << num;
        const std::string payload = ss.str();
        g_logger.info(L"PriceTagPrinter: buildTagPayload ordinal=" +
            std::to_wstring(t.ordinal) + L", payload='" +
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

    static Gdiplus::Bitmap* loadStatue() {
        static std::once_flag flag;
        static Gdiplus::Bitmap* cached = nullptr;
        std::call_once(flag, []() {
            g_logger.info(L"PriceTagPrinter: loading statue resource IDR_PNG_STATUE");
            HRSRC hr = FindResourceW(g_hInstance, MAKEINTRESOURCEW(IDR_PNG_STATUE), L"PNG");
            if (!hr) {
                g_logger.error(L"PriceTagPrinter: FindResourceW(statue) failed, err=" +
                    std::to_wstring(GetLastError()));
                return;
            }
            HGLOBAL hg = LoadResource(g_hInstance, hr);
            DWORD sz = SizeofResource(g_hInstance, hr);
            const void* p = LockResource(hg);
            if (!hg || !p || sz == 0) {
                g_logger.error(L"PriceTagPrinter: LoadResource/LockResource failed");
                return;
            }
            HGLOBAL hCopy = GlobalAlloc(GMEM_MOVEABLE, sz);
            if (!hCopy) { g_logger.error(L"PriceTagPrinter: GlobalAlloc failed"); return; }
            void* pc = GlobalLock(hCopy);
            memcpy(pc, p, sz);
            GlobalUnlock(hCopy);
            IStream* stm = nullptr;
            if (!SUCCEEDED(CreateStreamOnHGlobal(hCopy, TRUE, &stm))) {
                g_logger.error(L"PriceTagPrinter: CreateStreamOnHGlobal failed");
                GlobalFree(hCopy);
                return;
            }
            Gdiplus::Bitmap* src = Gdiplus::Bitmap::FromStream(stm);
            if (!src || src->GetLastStatus() != Gdiplus::Ok) {
                g_logger.error(L"PriceTagPrinter: Gdiplus::Bitmap::FromStream failed");
                if (src) delete src;
                stm->Release();
                return;
            }
            cached = src->Clone(0, 0, src->GetWidth(), src->GetHeight(), PixelFormat32bppARGB);
            delete src;
            if (!cached) {
                g_logger.error(L"PriceTagPrinter: Clone(statue) failed");
                stm->Release();
                return;
            }
            Gdiplus::BitmapData bd{};
            Gdiplus::Rect r(0, 0, static_cast<int>(cached->GetWidth()), static_cast<int>(cached->GetHeight()));
            if (cached->LockBits(&r, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
                PixelFormat32bppARGB, &bd) == Gdiplus::Ok) {
                const BYTE redR = GetRValue(PriceTagConfig::STATUE_RED);
                const BYTE redG = GetGValue(PriceTagConfig::STATUE_RED);
                const BYTE redB = GetBValue(PriceTagConfig::STATUE_RED);
                for (UINT y = 0; y < cached->GetHeight(); ++y) {
                    BYTE* row = reinterpret_cast<BYTE*>(bd.Scan0) + y * bd.Stride;
                    for (UINT x = 0; x < cached->GetWidth(); ++x) {
                        BYTE* px = row + x * 4;
                        if (px[3] > 0) {
                            px[0] = redB; px[1] = redG; px[2] = redR;
                        }
                    }
                }
                cached->UnlockBits(&bd);
                g_logger.info(L"PriceTagPrinter: statue recolored to RED, size=" +
                    std::to_wstring(cached->GetWidth()) + L"x" + std::to_wstring(cached->GetHeight()));
            }
            else {
                g_logger.warning(L"PriceTagPrinter: LockBits failed, statue left as-is");
            }
            stm->Release();
            });
        return cached;
    }

    static void drawBarcodeVertical(HDC hdc, int x, int yTop, int yBottom, int maxWidthPx,
        const std::string& payload) {
        auto modules = Code128::buildModules(payload);
        if (modules.empty() || yBottom <= yTop) return;
        const int n = static_cast<int>(modules.size());
        const int areaH = yBottom - yTop;
        double module = static_cast<double>(areaH) / static_cast<double>(n);
        if (module > 3.0) module = 3.0;
        if (module < 1.0)
            g_logger.warning(L"PriceTagPrinter: vertical barcode module < 1 px (" +
                std::to_wstring(n) + L" modules in " + std::to_wstring(areaH) + L" px)");
        const int actualH = static_cast<int>(std::round(module * n));
        const int startY = yTop + (areaH - actualH) / 2;
        HBRUSH hBlack = CreateSolidBrush(RGB(0, 0, 0));
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBlack);
        HPEN hOldPen = (HPEN)SelectObject(hdc, (HPEN)GetStockObject(NULL_PEN));
        double cur = startY;
        for (int m : modules) {
            const double next = cur + module;
            if (m) {
                int y0 = static_cast<int>(std::floor(cur + 0.5));
                int y1 = static_cast<int>(std::floor(next + 0.5));
                if (y1 > y0) Rectangle(hdc, x, y0, x + maxWidthPx, y1);
            }
            cur = next;
        }
        SelectObject(hdc, hOldPen);
        SelectObject(hdc, hOldBrush);
        DeleteObject(hBlack);
        g_logger.info(L"PriceTagPrinter: vertical barcode drawn, modules=" + std::to_wstring(n));
    }

    static void renderTagDocument(HDC hdc, const PriceTagData& t, int dpiXOverride, int dpiYOverride) {
        int dpiX = (dpiXOverride > 0) ? dpiXOverride : GetDeviceCaps(hdc, LOGPIXELSX);
        int dpiY = (dpiYOverride > 0) ? dpiYOverride : GetDeviceCaps(hdc, LOGPIXELSY);
        auto mmX = [&](int mm) { return MulDiv(mm, dpiX, 254) * 10; };
        auto mmY = [&](int mm) { return MulDiv(mm, dpiY, 254) * 10; };
        auto font = [&](int pt, bool bold) {
            return CreateFontW(-MulDiv(pt, dpiY, 72), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
            };
        HFONT fHead = font(8, true), fLbl = font(7, false), fVal = font(8, true),
            fSmall = font(6, false), fBig = font(16, true);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0));
        g_logger.info(L"PriceTagPrinter: renderTagDocument ordinal=" + std::to_wstring(t.ordinal) +
            L", dpi=" + std::to_wstring(dpiX));

        Gdiplus::Bitmap* statue = loadStatue();
        if (statue) {
            Gdiplus::Graphics g(hdc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.DrawImage(statue, mmX(2), mmY(2), mmX(12), mmY(12));
            g_logger.info(L"PriceTagPrinter: statue drawn at 2,2 mm, 12x12 mm");
        }
        else {
            g_logger.warning(L"PriceTagPrinter: statue resource unavailable, tag continues without it");
        }

        HFONT old = (HFONT)SelectObject(hdc, fHead);
        std::wstring header = std::wstring(L"Комиссионный магазин ") + PriceTagConfig::STORE_HEADER;
        SIZE sz{};
        GetTextExtentPoint32W(hdc, header.c_str(), (int)header.size(), &sz);
        TextOutW(hdc, (mmX(PriceTagConfig::TAG_WIDTH_MM) - sz.cx) / 2, mmY(2),
            header.c_str(), (int)header.size());

        const std::string payload = buildTagPayload(t);
        ReceiptPrinter::drawBarcodeLine(hdc, mmX(16), mmX(84), mmY(6), mmY(7), payload);

        SelectObject(hdc, fSmall);
        std::wstring tagStr = std::to_wstring(t.appendixNumber) + L"+" + std::to_wstring(t.ordinal);
        GetTextExtentPoint32W(hdc, tagStr.c_str(), (int)tagStr.size(), &sz);
        TextOutW(hdc, (mmX(PriceTagConfig::TAG_WIDTH_MM) - sz.cx) / 2, mmY(13),
            tagStr.c_str(), (int)tagStr.size());

        drawBarcodeVertical(hdc, mmX(92), mmY(6), mmY(44), mmX(5), payload);

        SelectObject(hdc, fLbl);
        TextOutW(hdc, mmX(3), mmY(17), L"Номер товара", (int)wcslen(L"Номер товара"));
        SelectObject(hdc, fVal);
        TextOutW(hdc, mmX(30), mmY(17), tagStr.c_str(), (int)tagStr.size());
        SelectObject(hdc, fLbl);
        TextOutW(hdc, mmX(60), mmY(17), L"Дата", (int)wcslen(L"Дата"));
        SelectObject(hdc, fVal);
        std::wstring dateStr = ReceiptUtils::formatDateDDMMYYYY(t.date);
        TextOutW(hdc, mmX(70), mmY(17), dateStr.c_str(), (int)dateStr.size());

        SelectObject(hdc, fLbl);
        TextOutW(hdc, mmX(3), mmY(22), L"Наименование", (int)wcslen(L"Наименование"));
        SelectObject(hdc, fVal);
        TextOutW(hdc, mmX(30), mmY(22), t.description.c_str(), (int)t.description.size());

        SelectObject(hdc, fLbl);
        TextOutW(hdc, mmX(3), mmY(27), L"Характеристика", (int)wcslen(L"Характеристика"));
        SelectObject(hdc, fVal);
        TextOutW(hdc, mmX(30), mmY(27), t.characteristic.c_str(), (int)t.characteristic.size());

        SelectObject(hdc, fSmall);
        auto noteLines = ReceiptPrinter::wrapText(t.note, 60);
        int ny = mmY(31);
        for (size_t i = 0; i < noteLines.size() && i < 2; ++i) {
            TextOutW(hdc, mmX(3), ny, noteLines[i].c_str(), (int)noteLines[i].size());
            ny += mmY(3);
        }

        SelectObject(hdc, fBig);
        std::wstring priceStr = formatPriceSpaced(t.price);
        TextOutW(hdc, mmX(3), mmY(38), L"Цена:", (int)wcslen(L"Цена:"));
        TextOutW(hdc, mmX(30), mmY(37), priceStr.c_str(), (int)priceStr.size());
        GetTextExtentPoint32W(hdc, priceStr.c_str(), (int)priceStr.size(), &sz);
        SelectObject(hdc, fLbl);
        TextOutW(hdc, mmX(30) + sz.cx + mmX(2), mmY(40), L"руб.", (int)wcslen(L"руб."));

        SelectObject(hdc, fSmall);
        TextOutW(hdc, mmX(3), mmY(47), t.workerFullName.c_str(), (int)t.workerFullName.size());
        DeleteObject(fHead); DeleteObject(fLbl); DeleteObject(fVal);
        DeleteObject(fSmall); DeleteObject(fBig);
        g_logger.info(L"PriceTagPrinter: tag rendered, ordinal=" + std::to_wstring(t.ordinal));
    }

    static std::wstring renderTagPreviewPng(const PriceTagData& t) {
        std::wstring result;
        std::wstring dir = Config::getAppDataPath();
        CreateDirectoryW(dir.c_str(), nullptr);
        dir += L"\\pricetags";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring pngPath = dir + L"\\appendix_" + std::to_wstring(t.appendixNumber) +
            L"_item_" + std::to_wstring(t.ordinal) + L".png";

        int dpi = 600;
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
        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);
        void* pBits = nullptr;
        HBITMAP hBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (!hBmp) {
            dpi = 300;
            pxW = static_cast<int>(std::round(PriceTagConfig::TAG_WIDTH_MM / 25.4 * dpi));
            pxH = static_cast<int>(std::round(PriceTagConfig::TAG_HEIGHT_MM / 25.4 * dpi));
            bmi.bmiHeader.biWidth = pxW;
            bmi.bmiHeader.biHeight = -pxH;
            hBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
            g_logger.warning(L"PriceTagPrinter: 600 DPI DIB failed, fallback 300 DPI");
        }
        if (!hBmp) {
            g_logger.error(L"PriceTagPrinter: CreateDIBSection failed");
            DeleteDC(memDC); ReleaseDC(nullptr, screenDC);
            return result;
        }
        HGDIOBJ hOldBmp = SelectObject(memDC, hBmp);
        if (hOldBmp == nullptr || hOldBmp == HGDI_ERROR) {
            g_logger.error(L"PriceTagPrinter: SelectObject failed, err=" + std::to_wstring(GetLastError()));
            DeleteObject(hBmp); DeleteDC(memDC); ReleaseDC(nullptr, screenDC);
            return result;
        }
        HBRUSH hWhite = CreateSolidBrush(RGB(255, 255, 255));
        RECT rc{ 0, 0, pxW, pxH };
        FillRect(memDC, &rc, hWhite);
        DeleteObject(hWhite);
        renderTagDocument(memDC, t, dpi, dpi);
        {
            Gdiplus::Bitmap bmp(hBmp, nullptr);
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
        SelectObject(memDC, hOldBmp);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        return result;
    }
};