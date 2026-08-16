// price_tag_printer.h
// =============================================================================
// МОДУЛЬ ПЕЧАТИ ЦЕННИКА 100 x 50 мм (ПРОДАКШН)
// =============================================================================
// Вызывается из worker_window.h СРАЗУ ПОСЛЕ печати приложения к договору.
// Печатает ОДИН ценник на КАЖДУЮ единицу товара в порядке ввода товароведа
// (позиция с quantity=N даёт N ценников с порядковыми номерами «+1..+N»).
//
// Состав ценника (по образцу 178.jpg и требованиям заказчика):
//  - шапка: наименование магазина (из ReceiptConfig::STORE_NAME — единый реквизит);
//  - слева вверху: миниатюра статуи «Рабочий и колхозница» (ресурс rc.png),
//    принудительно КРАСНЫМ цветом;
//  - горизонтальный Code 128 (набор B) с полезной нагрузкой:
//    ФИО и id комитента, наименование единицы, цена единицы,
//    сумма выплаты комитенту при реализации ЭТОЙ единицы;
//    (Code 128 читается сканером, подключённым к АТОЛ 77Ф — проверено);
//  - человекочитаемая строка под штрих-кодом: «<№ приложения>+<№ единицы>»;
//  - «Товарный ярлык» = номер приложения, «+N» = порядковый номер единицы
//    по приложению; справа «дата» = DD.MM.YYYY;
//  - «Наименование» — значение товароведа; «Характеристика» — значение из
//    строки экрана «Состояние»; под ней — примечание из строки «Примечание»;
//  - «Цена:» крупно + «руб.»;
//  - внизу слева — Ф.И.О. товароведа, составившего ценник
//    (передаётся на сервер через worker_id — см. раздел 1.2 итогового ответа);
//  - справа — вертикальный дублирующий штрих-код (как в образце).
//
// Контроль разработки: PNG-превью КАЖДОГО ценника сохраняется ВСЕГДА в
// %LOCALAPPDATA%\TerminalKiosk\pricetags\appendix_<N>_item_<K>.png (600 DPI).
//
// АРХИТЕКТУРА НЕ МЕНЯЕТСЯ: модуль переиспользует ГОТОВЫЕ статические методы
// ReceiptPrinter через friend (selectPrinter, isPromptPort, createLandscapeDC,
// beginPage, drawBarcodeLine, wrapText, ensureGdiplus, getPngEncoderClsid,
// AbortProc), готовые Code128::buildModules, ReceiptUtils::transliterate,
// ReceiptUtils::formatDateDDMMYYYY и Config::getAppDataPath.
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
#include "receipt_printer.h"   // ReceiptData, ReceiptUtils, Code128, ReceiptPrinter

extern Logger g_logger;
extern HINSTANCE g_hInstance;  // определён в main.cpp

// =============================================================================
// РЕСУРС rc.png ДЛЯ VS2022 (БЕЗ CMake).
// В проектном .rc-файле добавить (см. раздел 4 итогового ответа):
//     IDR_PNG_STATUE PNG "rc.png"
// =============================================================================
#ifndef IDR_PNG_STATUE
#define IDR_PNG_STATUE 300
#endif

namespace PriceTagConfig {
    // Габарит ценника: 10 см x 5 см (требование заказчика)
    constexpr int TAG_WIDTH_MM = 100;
    constexpr int TAG_HEIGHT_MM = 50;
    // Гарантированный красный цвет статуи (требование заказчика)
    constexpr COLORREF STATUE_RED = RGB(211, 47, 47);
    // Шапка ценника — тот же реквизит магазина, что и в приложении
    inline const wchar_t* STORE_HEADER = ReceiptConfig::STORE_NAME;
}

// =============================================================================
// ДАННЫЕ ОДНОГО ЦЕННИКА (одна единица товара)
// =============================================================================
struct PriceTagData {
    long long appendixNumber = 0;   // номер приложения к договору
    int ordinal = 0;                // порядковый номер единицы по приложению («+N»)
    int clientId = 0;               // id комитента
    std::wstring clientFullName;    // Ф.И.О. комитента
    std::wstring workerFullName;    // Ф.И.О. товароведа (низ ценника)
    std::wstring description;       // наименование (строка «Наименование»)
    std::wstring characteristic;    // строка «Состояние» -> «Характеристика»
    std::wstring note;              // строка «Примечание»
    double price = 0.0;             // цена единицы товара
    double perUnitPay = 0.0;        // выплата комитенту за ЭТУ единицу
    time_t date = 0;                // дата составления ценника
};

class PriceTagPrinter {
public:
    // =========================================================================
    // ГЛАВНАЯ ФУНКЦИЯ: ценники на все единицы товара из ReceiptData.
    // Порядок = порядок ввода товароведа (порядок d.items); позиция с
    // quantity=N разворачивается в N ценников с последовательными «+N».
    // =========================================================================
    static bool printForReceipt(const ReceiptData& d, const std::wstring& workerFullName) {
        g_logger.info(L"PriceTagPrinter::printForReceipt: started, appendix=" +
            std::to_wstring(d.appendixNumber) + L", worker='" + workerFullName +
            L"', positions=" + std::to_wstring(d.items.size()));
        if (d.items.empty()) {
            g_logger.warning(L"PriceTagPrinter::printForReceipt: no items, nothing to print");
            return true;
        }
        // GDI+ инициализируется тем же once-флагом, что и для приложения
        ReceiptPrinter::ensureGdiplus();

        // ---------------------------------------------------------------------
        // РАЗВЕРТКА ПОЗИЦИЙ В ЕДИНИЦЫ (порядок ввода сохраняется)
        // ---------------------------------------------------------------------
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
                // Выплата комитенту за единицу = client_amount позиции / quantity.
                // Математика клиента не меняется: берём ГОТОВОЕ client_amount.
                t.perUnitPay = (qty > 0) ? (it.clientAmount / qty) : 0.0;
                t.date = time(nullptr);
                units.push_back(t);
                g_logger.info(L"PriceTagPrinter: unit #" + std::to_wstring(ordinal) +
                    L" prepared: desc='" + it.description + L"', price=" +
                    std::to_wstring(it.price) + L", perUnitPay=" +
                    std::to_wstring(t.perUnitPay));
            }
        }
        g_logger.info(L"PriceTagPrinter: total units to print=" + std::to_wstring(units.size()));

        // ---------------------------------------------------------------------
        // PNG-ПРЕВЬЮ ВСЕГДА (контроль разработки, независимо от принтеров)
        // ---------------------------------------------------------------------
        for (const auto& t : units) {
            std::wstring png = renderTagPreviewPng(t);
            if (!png.empty())
                g_logger.info(L"PriceTagPrinter: PNG preview saved: " + png);
            else
                g_logger.warning(L"PriceTagPrinter: PNG preview NOT created, ordinal=" +
                    std::to_wstring(t.ordinal));
        }

        // ---------------------------------------------------------------------
        // ПЕЧАТЬ: тот же принтер, что выбирает программа (как у приложения)
        // ---------------------------------------------------------------------
        std::wstring printer = ReceiptPrinter::selectPrinter();
        if (printer.empty()) {
            g_logger.error(L"PriceTagPrinter: no printer available");
            return false;
        }
        g_logger.info(L"PriceTagPrinter: selected printer = " + printer);
        // Та же защита от зависания UI на интерактивных принтерах
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
            // Явная белая заливка страницы (та же, что у приложения)
            ReceiptPrinter::beginPage(hdc, GetDeviceCaps(hdc, HORZRES), GetDeviceCaps(hdc, VERTRES));
            renderTagDocument(hdc, t, 0, 0);   // DPI из DC принтера
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
    // =========================================================================
    // ПОЛЕЗНАЯ НАГРУЗКА ШТРИХ-КОДА ЦЕННИКА (Code 128 B, транслит — без кириллицы)
    // Состав по требованию: ФИО и id комитента, наименование единицы,
    // цена единицы, сумма выплаты комитенту при реализации этой единицы.
    // =========================================================================
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

    // =========================================================================
    // ФОРМАТ ЦЕНЫ КАК В ОБРАЗЦЕ: «1 000» (группировка тысяч пробелом)
    // =========================================================================
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
    // ЗАГРУЗКА СТАТУИ «РАБОЧИЙ И КОЛХОЗНИЦА» ИЗ РЕСУРСА rc.png +
    // ПРИНУДИТЕЛЬНАЯ ПЕРЕКРАСКА НЕПРОЗРАЧНЫХ ПИКСЕЛЕЙ В КРАСНЫЙ.
    // Кэш на всё время работы программы (std::once_flag).
    // =========================================================================
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
            // Копия в перемещаемую память для IStream (stream освобождает её сам)
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
            // Принудительная перекраска в красный с сохранением альфа-канала
            // (антиалиасинг остаётся корректным)
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
                        BYTE* px = row + x * 4;      // порядок байт BGRA
                        if (px[3] > 0) {             // непрозрачный/полупрозрачный пиксель
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
            stm->Release();   // stream освобождает hCopy (fDeleteOnRelease=TRUE)
            });
        return cached;
    }

    // =========================================================================
    // ВЕРТИКАЛЬНЫЙ ШТРИХ-КОД (дубль справа, как в образце 178.jpg),
    // с дробным модулем по аналогии с готовым drawBarcodeLine
    // =========================================================================
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

    // =========================================================================
    // ОТРИСОВКА ОДНОГО ЦЕННИКА 100 x 50 мм (раскладка по образцу 178.jpg)
    // =========================================================================
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

        // ---- Миниатюра статуи слева вверху (вместо круглого лого образца) ----
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

        // ---- Шапка: наименование магазина по центру ----
        HFONT old = (HFONT)SelectObject(hdc, fHead);
        std::wstring header = std::wstring(L"Комиссионный магазин ") + PriceTagConfig::STORE_HEADER;
        SIZE sz{};
        GetTextExtentPoint32W(hdc, header.c_str(), (int)header.size(), &sz);
        TextOutW(hdc, (mmX(PriceTagConfig::TAG_WIDTH_MM) - sz.cx) / 2, mmY(2),
            header.c_str(), (int)header.size());

        // ---- Горизонтальный Code 128 (полезная нагрузка по требованию) ----
        const std::string payload = buildTagPayload(t);
        ReceiptPrinter::drawBarcodeLine(hdc, mmX(16), mmX(84), mmY(6), mmY(7), payload);

        // ---- Человекочитаемая строка под штрих-кодом: «<приложение>+<единица>» ----
        SelectObject(hdc, fSmall);
        std::wstring tagStr = std::to_wstring(t.appendixNumber) + L"+" + std::to_wstring(t.ordinal);
        GetTextExtentPoint32W(hdc, tagStr.c_str(), (int)tagStr.size(), &sz);
        TextOutW(hdc, (mmX(PriceTagConfig::TAG_WIDTH_MM) - sz.cx) / 2, mmY(13),
            tagStr.c_str(), (int)tagStr.size());

        // ---- Вертикальный дублирующий штрих-код справа (как в образце) ----
        drawBarcodeVertical(hdc, mmX(92), mmY(6), mmY(44), mmX(5), payload);

        // ---- «Товарный ярлык» + «дата» ----
        SelectObject(hdc, fLbl);
        TextOutW(hdc, mmX(3), mmY(17), L"Товарный ярлык", (int)wcslen(L"Товарный ярлык"));
        SelectObject(hdc, fVal);
        TextOutW(hdc, mmX(30), mmY(17), tagStr.c_str(), (int)tagStr.size());
        SelectObject(hdc, fLbl);
        TextOutW(hdc, mmX(60), mmY(17), L"дата", (int)wcslen(L"дата"));
        SelectObject(hdc, fVal);
        std::wstring dateStr = ReceiptUtils::formatDateDDMMYYYY(t.date);
        TextOutW(hdc, mmX(70), mmY(17), dateStr.c_str(), (int)dateStr.size());

        // ---- «Наименование» (значение товароведа) ----
        SelectObject(hdc, fLbl);
        TextOutW(hdc, mmX(3), mmY(22), L"Наименование", (int)wcslen(L"Наименование"));
        SelectObject(hdc, fVal);
        TextOutW(hdc, mmX(30), mmY(22), t.description.c_str(), (int)t.description.size());

        // ---- «Характеристика» (значение из строки экрана «Состояние») ----
        SelectObject(hdc, fLbl);
        TextOutW(hdc, mmX(3), mmY(27), L"Характеристика", (int)wcslen(L"Характеристика"));
        SelectObject(hdc, fVal);
        TextOutW(hdc, mmX(30), mmY(27), t.characteristic.c_str(), (int)t.characteristic.size());

        // ---- Примечание (строка экрана «Примечание»), под «Характеристика» ----
        SelectObject(hdc, fSmall);
        auto noteLines = ReceiptPrinter::wrapText(t.note, 60);
        int ny = mmY(31);
        for (size_t i = 0; i < noteLines.size() && i < 2; ++i) {
            TextOutW(hdc, mmX(3), ny, noteLines[i].c_str(), (int)noteLines[i].size());
            ny += mmY(3);
        }

        // ---- «Цена:» крупно + «руб.» ----
        SelectObject(hdc, fBig);
        std::wstring priceStr = formatPriceSpaced(t.price);
        TextOutW(hdc, mmX(3), mmY(38), L"Цена:", (int)wcslen(L"Цена:"));
        TextOutW(hdc, mmX(30), mmY(37), priceStr.c_str(), (int)priceStr.size());
        GetTextExtentPoint32W(hdc, priceStr.c_str(), (int)priceStr.size(), &sz);
        SelectObject(hdc, fLbl);
        TextOutW(hdc, mmX(30) + sz.cx + mmX(2), mmY(40), L"руб.", (int)wcslen(L"руб."));

        // ---- Внизу слева: Ф.И.О. товароведа, составившего ценник ----
        SelectObject(hdc, fSmall);
        TextOutW(hdc, mmX(3), mmY(47), t.workerFullName.c_str(), (int)t.workerFullName.size());

        DeleteObject(fHead); DeleteObject(fLbl); DeleteObject(fVal);
        DeleteObject(fSmall); DeleteObject(fBig);
        g_logger.info(L"PriceTagPrinter: tag rendered, ordinal=" + std::to_wstring(t.ordinal));
    }

    // =========================================================================
    // PNG-ПРЕВЬЮ ЦЕННИКА (600 DPI, 100x50 мм) — контроль разработки, ВСЕГДА
    // Путь: %LOCALAPPDATA%\TerminalKiosk\pricetags\appendix_<N>_item_<K>.png
    // =========================================================================
    static std::wstring renderTagPreviewPng(const PriceTagData& t) {
        std::wstring result;
        std::wstring dir = Config::getAppDataPath();
        CreateDirectoryW(dir.c_str(), nullptr);
        dir += L"\\pricetags";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring pngPath = dir + L"\\appendix_" + std::to_wstring(t.appendixNumber) +
            L"_item_" + std::to_wstring(t.ordinal) + L".png";
        int dpi = 600;
        int pxW = static_cast<int>(std::round(PriceTagConfig::TAG_WIDTH_MM / 25.4 * dpi));   // 2362
        int pxH = static_cast<int>(std::round(PriceTagConfig::TAG_HEIGHT_MM / 25.4 * dpi));  // 1181
        g_logger.info(L"PriceTagPrinter: preview DIB " + std::to_wstring(pxW) + L"x" +
            std::to_wstring(pxH) + L" @ " + std::to_wstring(dpi) + L" DPI");
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = pxW;
        bmi.bmiHeader.biHeight = -pxH;             // top-down
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
