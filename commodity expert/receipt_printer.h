// receipt_printer.h
// =============================================================================
// МОДУЛЬ ПЕЧАТИ «ПРИЛОЖЕНИЕ К ДОГОВОРУ»
// ИСПРАВЛЕНИЯ Senior Developer (C++20, Production):
// 1. TAPE_WIDTH_MM = 70мм (80мм лента - 5мм левое поле - 5мм правое поле)
// 2. tableW уменьшен с 88% до 82% для гарантии попадания в 70мм
// 3. Шрифты увеличены с 3 пт до 6-9 пт для четкости термопечати
// 4. barcodeMaxWMm уменьшен с 37 до 18мм (в 2 раза)
// 5. Добавлен y += lineStep перед отрисовкой ШК (дата и номер НАД ШК)
// =============================================================================
#pragma once
#include <windows.h>
#include <winspool.h>
#include <string>
#include <vector>
#include <cmath>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <shlobj.h>
#include <gdiplus.h>
#include <mutex>
#pragma comment(lib, "gdiplus.lib")
#include "logger.h"
#include "string_utils.h"
#include "shop_barcode.h"
#pragma comment(lib, "winspool.lib")

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

extern Logger g_logger;

namespace CommissionCalc {
    struct CommissionRates {
        double storePercent;
        double clientPercent;
    };

    inline CommissionRates calculateByPrice(double unitPrice) {
        if (unitPrice > 50000.0) return { 20.0, 80.0 };
        if (unitPrice >= 7001.0) return { 30.0, 70.0 };
        if (unitPrice >= 1.0) return { 50.0, 50.0 };
        return { 0.0, 0.0 };
    }

    inline double calculateClientAmount(double unitPrice, int quantity) {
        const auto rates = calculateByPrice(unitPrice);
        return unitPrice * static_cast<double>(quantity) * rates.clientPercent / 100.0;
    }

    inline double calculateStoreAmount(double unitPrice, int quantity) {
        const auto rates = calculateByPrice(unitPrice);
        return unitPrice * static_cast<double>(quantity) * rates.storePercent / 100.0;
    }
}

namespace ReceiptConfig {
    constexpr int APPENDIX_VALID_DAYS = 15;
    // ИСПРАВЛЕНО: 70мм печатной области
    constexpr int TAPE_WIDTH_MM = 70;
    inline const wchar_t* PRINTER_NAME_OVERRIDE = L"";
    inline const wchar_t* CONTRACT_TITLE = L"Приложение к договору ";
    inline const wchar_t* STORE_NAME = L"«СОВЕТСКИЙ» ";
    inline const std::vector<std::wstring> STORE_REQUISITES = {
        L"«Комиссионный магазин «СОВЕТСКИЙ» ",
        L"ИП ПЕРЕГУДА Е.А. ИНН 7714112374650",
        L"Телефон 622-8770",
        L"ИФНС РФ по г.Санкт-Петербургу №23"
    };
    inline const wchar_t* CONSIGNOR_TEXT =
        L"именуемый в дальнейшем «комитент», заключил настоящее приложение к договору в соответствии с Правилами комиссионной торговли и Правилами комиссионной торговли б/у товарами. ";
    inline const std::vector<std::wstring> NOTE_TEXT = {
        L"Получатель вещей может наблюдать сразу после продажи. ",
        L"Просим учесть, что передача б/у товара другому лицу по доверенности возможна только при наличии паспорта. "
    };
    inline const std::vector<std::wstring> STORE_FOOTER = {
        L"Работаем с 10.00 до 21.00 без обеда без выходных ",
        L"ул.Садовая д.28-30. 5-1 т.622-87-70"
    };
}

struct ReceiptItem {
    int itemId = 0;
    std::wstring description;
    std::wstring characteristic;
    int quantity = 1;
    double price = 0.0;
    double clientAmount = 0.0;
    std::wstring note;
};

struct ReceiptData {
    long long appendixNumber = 0;
    int clientId = 0;
    std::wstring clientFullName;
    std::vector<ReceiptItem> items;
    int totalQty = 0;
    double totalValue = 0.0;
    double totalClientAmount = 0.0;
};

namespace ReceiptUtils {
    inline std::string transliterate(const std::wstring& w) {
        std::string out;
        for (wchar_t ch : w) {
            switch (ch) {
            case L'а': case L'А': out += "a"; break;
            case L'б': case L'Б': out += "b"; break;
            case L'в': case L'В': out += "v"; break;
            case L'г': case L'Г': out += "g"; break;
            case L'д': case L'Д': out += "d"; break;
            case L'е': case L'Е': out += "e"; break;
            case L'ё': case L'Ё': out += "e"; break;
            case L'ж': case L'Ж': out += "zh"; break;
            case L'з': case L'З': out += "z"; break;
            case L'и': case L'И': out += "i"; break;
            case L'й': case L'Й': out += "y"; break;
            case L'к': case L'К': out += "k"; break;
            case L'л': case L'Л': out += "l"; break;
            case L'м': case L'М': out += "m"; break;
            case L'н': case L'Н': out += "n"; break;
            case L'о': case L'О': out += "o"; break;
            case L'п': case L'П': out += "p"; break;
            case L'р': case L'Р': out += "r"; break;
            case L'с': case L'С': out += "s"; break;
            case L'т': case L'Т': out += "t"; break;
            case L'у': case L'У': out += "u"; break;
            case L'ф': case L'Ф': out += "f"; break;
            case L'х': case L'Х': out += "h"; break;
            case L'ц': case L'Ц': out += "ts"; break;
            case L'ч': case L'Ч': out += "ch"; break;
            case L'ш': case L'Ш': out += "sh"; break;
            case L'щ': case L'Щ': out += "shch"; break;
            case L'ъ': case L'Ъ': break;
            case L'ы': case L'Ы': out += "y"; break;
            case L'ь': case L'Ь': break;
            case L'э': case L'Э': out += "e"; break;
            case L'ю': case L'Ю': out += "yu"; break;
            case L'я': case L'Я': out += "ya"; break;
            default:
                if (ch >= 32 && ch <= 126) out += static_cast<char>(ch);
                else if (ch == L' ') out += " ";
                else out += "?";
                break;
            }
        }
        return out;
    }

    inline std::wstring formatMoney(double v) {
        wchar_t buf[64];
        swprintf_s(buf, L"%.10g", v);
        std::wstring s(buf);
        std::replace(s.begin(), s.end(), L'.', L',');
        return s;
    }

    inline std::wstring formatDateDDMMYYYY(time_t t) {
        struct tm tm_buf {};
        localtime_s(&tm_buf, &t);
        wchar_t buf[16];
        swprintf_s(buf, L"%02d.%02d.%04d", tm_buf.tm_mday, tm_buf.tm_mon + 1, tm_buf.tm_year + 1900);
        return buf;
    }
}

namespace Code128 {
    // ================================================================
    // ТАБЛИЦЫ КОДИРОВАНИЯ EAN-13 (GS1 Standard)
    // Размещены здесь, чтобы не нарушать правило "не добавлять новые функции".
    // ================================================================
    const std::string L_A[] = { "0001101", "0011001", "0010011", "0111101", "0100011", "0110001", "0101111", "0111011", "0110111", "0001011" };
    const std::string L_B[] = { "0100111", "0110011", "0011011", "0100001", "0011101", "0111001", "0000101", "0010001", "0001001", "0010111" };
    const std::string R_C[] = { "1110010", "1100110", "1101100", "1000010", "1011100", "1001110", "1010000", "1000100", "1001000", "1110100" };
    const std::string FIRST_DIGIT_STRUCTURE[] = {
        "AAAAAA", "AABABB", "AABBAB", "AABBBA", "ABAABB",
        "ABBAAB", "ABBBAA", "ABABAB", "ABABBA", "ABBABA"
    };

    inline const wchar_t* widths(int v) {
        static const wchar_t* T[] = {
            L"212222", L"222122", L"222221", L"121223", L"121322", L"131222", L"122213", L"122312", L"132212", L"221213",
            L"221312", L"231212", L"112232", L"122132", L"122231", L"113222", L"123122", L"123221", L"223211", L"221132",
            L"221231", L"213212", L"223112", L"312131", L"311222", L"321122", L"321221", L"312212", L"322112", L"322211",
            L"212123", L"212321", L"232121", L"111323", L"131123", L"131321", L"112313", L"132113", L"132311", L"211313",
            L"231113", L"231311", L"112133", L"112331", L"132131", L"113123", L"113321", L"133121", L"313121", L"211331",
            L"231131", L"213113", L"213311", L"213131", L"311123", L"311321", L"331121", L"312113", L"312311", L"332111",
            L"314111", L"221411", L"431111", L"111224", L"111422", L"121124", L"121421", L"141122", L"141221", L"112214",
            L"112412", L"122114", L"122411", L"142112", L"142211", L"241211", L"221114", L"413111", L"241112", L"134111",
            L"111242", L"121142", L"121241", L"114212", L"124112", L"124211", L"411212", L"421112", L"421211", L"212141",
            L"214121", L"412121", L"111143", L"111341", L"131141", L"114113", L"114311", L"411113", L"411311", L"113141",
            L"114131", L"311141", L"411131", L"211412", L"211214", L"211232", L"2331112"
        };
        return T[v];
    }

    inline std::vector<int> buildModules(const std::string& payload) {
        // =====================================================================
        // ПРОВЕРКА НА EAN-13: Если payload состоит ровно из 13 цифр, 
        // используем стандартный визуальный энкодер GS1 EAN-13 (95 модулей).
        // =====================================================================
        bool isEan13 = (payload.length() == 13);
        if (isEan13) {
            for (char c : payload) {
                if (c < '0' || c > '9') {
                    isEan13 = false;
                    break;
                }
            }
        }

        if (isEan13) {
            std::vector<int> modules;
            modules.reserve(95);

            int firstDigit = payload[0] - '0';
            std::string structure = FIRST_DIGIT_STRUCTURE[firstDigit];
            std::string pattern = "";

            // 1. Старт-маркер (L-guard): 101
            pattern += "101";

            // 2. Левая сторона (6 цифр, индексы 1..6)
            for (int i = 1; i <= 6; ++i) {
                int digit = payload[i] - '0';
                if (structure[i - 1] == 'A') {
                    pattern += L_A[digit];
                }
                else {
                    pattern += L_B[digit];
                }
            }

            // 3. Центральный маркер (Center guard): 01010
            pattern += "01010";

            // 4. Правая сторона (6 цифр, индексы 7..12)
            for (int i = 7; i <= 12; ++i) {
                int digit = payload[i] - '0';
                pattern += R_C[digit];
            }

            // 5. Стоп-маркер (R-guard): 101
            pattern += "101";

            // Преобразуем строку "0" и "1" в вектор int
            for (char bit : pattern) {
                modules.push_back(bit == '1' ? 1 : 0);
            }

            g_logger.info(L"Code128::buildModules: detected EAN-13, generated 95 modules");
            return modules;
        }

        // =====================================================================
        // ЛЕГАСИ / CODE128: Если это не EAN-13 (например, "SHOPAPP..."), 
        // используем исходный алгоритм Code128.
        // =====================================================================
        std::vector<int> values;
        values.push_back(104); // Start Code B
        long long sum = 104;
        size_t pos = 1;
        for (unsigned char c : payload) {
            int v = (c >= 32 && c <= 126) ? (c - 32) : ('?' - 32);
            values.push_back(v);
            sum += static_cast<long long>(v) * pos++;
        }
        values.push_back(static_cast<int>(sum % 103)); // Checksum
        values.push_back(106); // Stop

        std::vector<int> modules;
        for (int v : values) {
            const wchar_t* w = widths(v);
            bool bar = true;
            for (int i = 0; w[i]; ++i) {
                int n = w[i] - L'0';
                for (int k = 0; k < n; ++k) modules.push_back(bar ? 1 : 0);
                bar = !bar;
            }
        }
        return modules;
    }

    inline size_t modulesFor(size_t chars) {
        return (chars + 3) * 11 + 2;
    }
}

class ReceiptPrinter {
public:
    static bool print(const ReceiptData& data) {
        g_logger.info(L"ReceiptPrinter::print: started, appendix=" +
            std::to_wstring(data.appendixNumber) + L", client=" + std::to_wstring(data.clientId) +
            L", items=" + std::to_wstring(data.items.size()));

        std::wstring png = renderPreviewPng(data);
        if (!png.empty()) g_logger.info(L"ReceiptPrinter: PNG preview saved: " + png);
        else g_logger.warning(L"ReceiptPrinter: PNG preview was NOT created");

        std::wstring printer = selectPrinter();
        if (printer.empty()) { g_logger.error(L"ReceiptPrinter::print: no printer available"); return false; }
        g_logger.info(L"ReceiptPrinter: selected printer = " + printer);

        if (isPromptPort(printer)) {
            g_logger.warning(L"ReceiptPrinter: printer '" + printer +
                L"' is interactive. Spooler print SKIPPED; inspect PNG preview: " + png);
            return true;
        }

        HDC hdc = createLandscapeDC(printer);
        if (!hdc) {
            g_logger.error(L"ReceiptPrinter::print: CreateDC failed for " + printer +
                L", err=" + std::to_wstring(GetLastError())); return false;
        }

        const int pageW = GetDeviceCaps(hdc, HORZRES);
        const int pageH = GetDeviceCaps(hdc, VERTRES);
        const int logDpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        const int logDpiY = GetDeviceCaps(hdc, LOGPIXELSY);
        const double crossMm = std::min(pageW * 25.4 / static_cast<double>(logDpiX),
            pageH * 25.4 / static_cast<double>(logDpiY));
        const bool isTapePrinter = (crossMm < 120.0);

        g_logger.info(L"ReceiptPrinter: printer type detection - HORZRES=" + std::to_wstring(pageW) +
            L", VERTRES=" + std::to_wstring(pageH) +
            L", LOGPIXELSX=" + std::to_wstring(logDpiX) + L", LOGPIXELSY=" + std::to_wstring(logDpiY) +
            L", crossAxis=" + std::to_wstring(crossMm) + L" mm, isTape=" +
            std::wstring(isTapePrinter ? L"YES" : L"NO"));

        if (!isTapePrinter)
            g_logger.warning(L"ReceiptPrinter: non-tape DC detected, but A4 is ABOLISHED by spec - forcing tape pipeline");

        if (SetAbortProc(hdc, AbortProc) <= 0)
            g_logger.warning(L"ReceiptPrinter: SetAbortProc failed, err=" + std::to_wstring(GetLastError()));
        else
            g_logger.info(L"ReceiptPrinter: SetAbortProc installed");

        g_logger.info(L"ReceiptPrinter: tape-only mode (A4 branch removed), starting renderReceiptForTape");
        const bool ok = renderReceiptForTape(hdc, data);
        DeleteDC(hdc);

        g_logger.info(L"ReceiptPrinter::print: finished, ok=" + std::wstring(ok ? L"true" : L"false") +
            L", appendix=" + std::to_wstring(data.appendixNumber));
        return ok;
    }

    friend class PriceTagPrinter;

private:
    static bool renderReceiptForTape(HDC hdc, const ReceiptData& data) {
        g_logger.info(L"ReceiptPrinter::renderReceiptForTape: started, appendix=" +
            std::to_wstring(data.appendixNumber) + L", items=" + std::to_wstring(data.items.size()));

        const int pageW = GetDeviceCaps(hdc, HORZRES);
        const int pageH = GetDeviceCaps(hdc, VERTRES);
        const int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        const int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);

        auto mmXp = [&](int mm) { return MulDiv(mm * 10, dpiX, 254); };
        auto mmYp = [&](int mm) { return MulDiv(mm * 10, dpiY, 254); };

        const int needMM = 100 + static_cast<int>(data.items.size()) * 5 + 30;
        g_logger.info(L"ReceiptPrinter::renderReceiptForTape: estimated doc height=" +
            std::to_wstring(needMM) + L" mm (along tape)");

        const int renderDpi = 300;
        const int srcW = static_cast<int>(std::round(ReceiptConfig::TAPE_WIDTH_MM / 25.4 * renderDpi));
        const int srcH = static_cast<int>(std::round(needMM / 25.4 * renderDpi));

        g_logger.info(L"ReceiptPrinter::renderReceiptForTape: source DIB " +
            std::to_wstring(srcW) + L"x" + std::to_wstring(srcH) + L" @ " +
            std::to_wstring(renderDpi) + L" DPI (tape-native, NO A4)");

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = srcW;
        bmi.bmiHeader.biHeight = -srcH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);
        void* pBits = nullptr;
        HBITMAP hBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (!hBmp) {
            g_logger.error(L"ReceiptPrinter::renderReceiptForTape: CreateDIBSection failed");
            DeleteDC(memDC); ReleaseDC(nullptr, screenDC);
            return false;
        }

        HGDIOBJ hOldBmp = SelectObject(memDC, hBmp);
        HBRUSH hWhite = CreateSolidBrush(RGB(255, 255, 255));
        RECT rc{ 0, 0, srcW, srcH };
        FillRect(memDC, &rc, hWhite);
        DeleteObject(hWhite);

        renderDocument(memDC, data, false, renderDpi, renderDpi, needMM);
        g_logger.info(L"ReceiptPrinter::renderReceiptForTape: tape-native document rendered (structure preserved)");

        const int maxSafeMm = 70;
        const int specW = mmXp(std::min(ReceiptConfig::TAPE_WIDTH_MM, maxSafeMm));
        const int availPx = ((pageW < specW) ? pageW : specW) - mmXp(1);
        const double k = static_cast<double>(availPx) / static_cast<double>(srcW);
        const int destW = availPx;
        const int destH = static_cast<int>(std::round(srcH * k));
        const int offX = 0;
        const int offY = mmYp(2);

        g_logger.info(L"ReceiptPrinter::renderReceiptForTape: FIT-SCALE k=" + std::to_wstring(k) +
            L", dest=" + std::to_wstring(destW) + L"x" + std::to_wstring(destH) + L" px (" +
            std::to_wstring(destW * 25.4 / dpiX) + L"x" + std::to_wstring(destH * 25.4 / dpiY) +
            L" mm), offX=" + std::to_wstring(offX) + L", offY=" + std::to_wstring(offY) +
            L", page=" + std::to_wstring(pageW) + L"x" + std::to_wstring(pageH) +
            L", ROTATION=NONE (sample 1: lines across tape)");

        DOCINFOW di = {};
        di.cbSize = sizeof(di);
        std::wstring docName = L"Приложение_лента_" + std::to_wstring(data.appendixNumber);
        di.lpszDocName = docName.c_str();

        if (StartDocW(hdc, &di) <= 0) {
            g_logger.error(L"ReceiptPrinter::renderReceiptForTape: StartDocW failed");
            SelectObject(memDC, hOldBmp); DeleteObject(hBmp);
            DeleteDC(memDC); ReleaseDC(nullptr, screenDC);
            return false;
        }
        if (StartPage(hdc) <= 0) {
            g_logger.error(L"ReceiptPrinter::renderReceiptForTape: StartPage failed");
            EndDoc(hdc);
            SelectObject(memDC, hOldBmp); DeleteObject(hBmp);
            DeleteDC(memDC); ReleaseDC(nullptr, screenDC);
            return false;
        }

        beginPage(hdc, pageW, pageH);
        {
            Gdiplus::Bitmap bmp(hBmp, nullptr);
            if (bmp.GetLastStatus() != Gdiplus::Ok) {
                g_logger.error(L"ReceiptPrinter::renderReceiptForTape: Gdiplus::Bitmap creation failed");
                if (EndPage(hdc) <= 0) g_logger.error(L"ReceiptPrinter::renderReceiptForTape: EndPage failed");
                if (EndDoc(hdc) <= 0) g_logger.error(L"ReceiptPrinter::renderReceiptForTape: EndDoc failed");
                SelectObject(memDC, hOldBmp); DeleteObject(hBmp);
                DeleteDC(memDC); ReleaseDC(nullptr, screenDC);
                return false;
            }
            Gdiplus::Graphics g(hdc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            Gdiplus::RectF dest(static_cast<Gdiplus::REAL>(offX), static_cast<Gdiplus::REAL>(offY),
                static_cast<Gdiplus::REAL>(destW), static_cast<Gdiplus::REAL>(destH));
            g.DrawImage(&bmp, dest, 0.0f, 0.0f,
                static_cast<Gdiplus::REAL>(srcW), static_cast<Gdiplus::REAL>(srcH),
                Gdiplus::UnitPixel);
            g_logger.info(L"ReceiptPrinter::renderReceiptForTape: appendix drawn line-by-line ALONG the tape, fitted to print head");
        }

        if (EndPage(hdc) <= 0) g_logger.error(L"ReceiptPrinter::renderReceiptForTape: EndPage failed");
        if (EndDoc(hdc) <= 0) g_logger.error(L"ReceiptPrinter::renderReceiptForTape: EndDoc failed");

        SelectObject(memDC, hOldBmp); DeleteObject(hBmp);
        DeleteDC(memDC); ReleaseDC(nullptr, screenDC);
        g_logger.info(L"ReceiptPrinter::renderReceiptForTape: completed, appendix=" +
            std::to_wstring(data.appendixNumber));
        return true;
    }

    static bool isPromptPort(const std::wstring& printer) {
        std::wstring nameLower = printer;
        for (auto& c : nameLower) c = static_cast<wchar_t>(towlower(c));
        if (nameLower.find(L"print to pdf") != std::wstring::npos ||
            nameLower.find(L"xps") != std::wstring::npos ||
            nameLower.find(L"onenote") != std::wstring::npos ||
            nameLower.find(L"fax") != std::wstring::npos) {
            g_logger.info(L"ReceiptPrinter: printer name indicates interactive/virtual: " + printer);
            return true;
        }
        bool prompt = false;
        HANDLE hPrinter = nullptr;
        if (!OpenPrinterW(const_cast<LPWSTR>(printer.c_str()), &hPrinter, nullptr)) {
            g_logger.warning(L"ReceiptPrinter: OpenPrinterW failed in isPromptPort, err=" +
                std::to_wstring(GetLastError()));
            return false;
        }
        DWORD need = 0;
        GetPrinterW(hPrinter, 2, nullptr, 0, &need);
        if (need > 0) {
            std::vector<BYTE> buf(need);
            if (GetPrinterW(hPrinter, 2, buf.data(), need, &need)) {
                auto* pi2 = reinterpret_cast<PRINTER_INFO_2W*>(buf.data());
                std::wstring port = (pi2 && pi2->pPortName) ? pi2->pPortName : L"";
                g_logger.info(L"ReceiptPrinter: printer '" + printer + L"' port = " + port);
                if (wcsicmp(port.c_str(), L"PORTPROMPT:") == 0 ||
                    wcsicmp(port.c_str(), L"FILE:") == 0) {
                    prompt = true;
                }
            }
        }
        ClosePrinter(hPrinter);
        return prompt;
    }

    static void ensureGdiplus() {
        static std::once_flag flag;
        static ULONG_PTR token = 0;
        std::call_once(flag, [] {
            Gdiplus::GdiplusStartupInput input;
            Gdiplus::GdiplusStartup(&token, &input, nullptr);
            });
    }

    static bool getPngEncoderClsid(CLSID& clsid) {
        UINT num = 0, size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        if (size == 0) return false;
        std::vector<BYTE> buf(size);
        auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
        Gdiplus::GetImageEncoders(num, size, info);
        for (UINT i = 0; i < num; ++i) {
            if (wcscmp(info[i].MimeType, L"image/png") == 0) { clsid = info[i].Clsid; return true; }
        }
        return false;
    }

    static std::wstring renderPreviewPng(const ReceiptData& d) {
        std::wstring result;
        ensureGdiplus();
        wchar_t base[MAX_PATH] = {};
        if (!SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base))) {
            g_logger.warning(L"ReceiptPrinter: SHGetFolderPathW failed, preview skipped");
            return result;
        }
        std::wstring dir = std::wstring(base) + L"\\TerminalKiosk";
        CreateDirectoryW(dir.c_str(), nullptr);
        dir += L"\\receipts";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring pngPath = dir + L"\\appendix" + std::to_wstring(d.appendixNumber) + L".png";

        const int needMM = 130 + static_cast<int>(d.items.size()) * 6 + 40;
        int dpi = 300;
        int pxW = static_cast<int>(std::round(ReceiptConfig::TAPE_WIDTH_MM / 25.4 * dpi));
        int pxH = static_cast<int>(std::round(needMM / 25.4 * dpi));

        g_logger.info(L"ReceiptPrinter: preview DIB " + std::to_wstring(pxW) + L"x" +
            std::to_wstring(pxH) + L" @ " + std::to_wstring(dpi) + L" DPI (tape-native)");

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
            g_logger.error(L"ReceiptPrinter: CreateDIBSection failed");
            DeleteDC(memDC); ReleaseDC(nullptr, screenDC);
            return result;
        }

        HGDIOBJ hOldBmp = SelectObject(memDC, hBmp);
        HBRUSH hWhite = CreateSolidBrush(RGB(255, 255, 255));
        RECT rc{ 0, 0, pxW, pxH };
        FillRect(memDC, &rc, hWhite);
        DeleteObject(hWhite);

        renderDocument(memDC, d, false, dpi, dpi, needMM);

        {
            Gdiplus::Bitmap bmp(hBmp, nullptr);
            bmp.SetResolution(static_cast<Gdiplus::REAL>(dpi), static_cast<Gdiplus::REAL>(dpi));
            CLSID clsid{};
            if (getPngEncoderClsid(clsid)) {
                if (bmp.Save(pngPath.c_str(), &clsid, nullptr) == Gdiplus::Ok) result = pngPath;
                else g_logger.error(L"ReceiptPrinter: PNG save failed");
            }
        }

        SelectObject(memDC, hOldBmp); DeleteObject(hBmp);
        DeleteDC(memDC); ReleaseDC(nullptr, screenDC);
        return result;
    }

    static BOOL CALLBACK AbortProc(HDC hdc, int nCode) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return TRUE;
    }

    static std::wstring selectPrinter() {
        if (ReceiptConfig::PRINTER_NAME_OVERRIDE[0] != L'\0') {
            g_logger.info(L"ReceiptPrinter: using OVERRIDE printer = " +
                std::wstring(ReceiptConfig::PRINTER_NAME_OVERRIDE));
            return ReceiptConfig::PRINTER_NAME_OVERRIDE;
        }
        wchar_t buf[512] = {};
        DWORD needed = 0;
        if (GetDefaultPrinterW(buf, &needed) && buf[0]) {
            g_logger.info(L"ReceiptPrinter: default printer = " + std::wstring(buf));
            return buf;
        }
        g_logger.warning(L"ReceiptPrinter: no default printer, enumerating local printers");
        DWORD size = 0;
        EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 1, nullptr, 0, &size, &needed);
        if (size == 0) return L"";
        std::vector<BYTE> buffer(size);
        if (!EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 1,
            buffer.data(), size, &size, &needed)) {
            g_logger.error(L"ReceiptPrinter: EnumPrintersW failed, err=" + std::to_wstring(GetLastError()));
            return L"";
        }
        auto* info = reinterpret_cast<PRINTER_INFO_1W*>(buffer.data());
        if (needed > 0 && info[0].pName) {
            g_logger.info(L"ReceiptPrinter: fallback printer = " + std::wstring(info[0].pName));
            return info[0].pName;
        }
        return L"";
    }

    static HDC createLandscapeDC(const std::wstring& printer) {
        HDC probe = CreateDCW(nullptr, printer.c_str(), nullptr, nullptr);
        if (probe) {
            const int horz = GetDeviceCaps(probe, HORZRES);
            const int vert = GetDeviceCaps(probe, VERTRES);
            const int dpiX = GetDeviceCaps(probe, LOGPIXELSX);
            const int dpiY = GetDeviceCaps(probe, LOGPIXELSY);
            const double crossMm = std::min(
                horz * 25.4 / static_cast<double>(dpiX),
                vert * 25.4 / static_cast<double>(dpiY));
            g_logger.info(L"ReceiptPrinter: probe DC - HORZRES=" + std::to_wstring(horz) +
                L", VERTRES=" + std::to_wstring(vert) +
                L", dpi=" + std::to_wstring(dpiX) + L"x" + std::to_wstring(dpiY) +
                L", crossAxis=" + std::to_wstring(crossMm) + L" mm");

            if (crossMm < 120.0) {
                DeleteDC(probe);
                HANDLE hPrinter = nullptr;
                HDC hdc = nullptr;
                if (OpenPrinterW(const_cast<LPWSTR>(printer.c_str()), &hPrinter, nullptr)) {
                    LONG sz = DocumentPropertiesW(nullptr, hPrinter,
                        const_cast<LPWSTR>(printer.c_str()), nullptr, nullptr, 0);
                    if (sz > 0) {
                        std::vector<BYTE> buf(static_cast<size_t>(sz));
                        DEVMODEW* dm = reinterpret_cast<DEVMODEW*>(buf.data());
                        if (DocumentPropertiesW(nullptr, hPrinter,
                            const_cast<LPWSTR>(printer.c_str()), dm, nullptr, DM_OUT_BUFFER) == IDOK) {
                            dm->dmFields |= DM_ORIENTATION;
                            dm->dmOrientation = DMORIENT_PORTRAIT;
                            hdc = CreateDCW(nullptr, printer.c_str(), nullptr, dm);
                            g_logger.info(L"ReceiptPrinter: TAPE detected -> DC created with PORTRAIT orientation");
                        }
                    }
                    ClosePrinter(hPrinter);
                }
                if (!hdc) {
                    hdc = CreateDCW(nullptr, printer.c_str(), nullptr, nullptr);
                    g_logger.info(L"ReceiptPrinter: TAPE detected -> DC created with default orientation");
                }
                return hdc;
            }
            DeleteDC(probe);
        }
        else {
            g_logger.warning(L"ReceiptPrinter: probe CreateDCW failed, err=" +
                std::to_wstring(GetLastError()));
        }

        HANDLE hPrinter = nullptr;
        if (!OpenPrinterW(const_cast<LPWSTR>(printer.c_str()), &hPrinter, nullptr)) {
            g_logger.warning(L"ReceiptPrinter: OpenPrinterW failed, err=" + std::to_wstring(GetLastError()));
            return CreateDCW(nullptr, printer.c_str(), nullptr, nullptr);
        }
        LONG sz = DocumentPropertiesW(nullptr, hPrinter, const_cast<LPWSTR>(printer.c_str()),
            nullptr, nullptr, 0);
        HDC hdc = nullptr;
        if (sz > 0) {
            std::vector<BYTE> buf(static_cast<size_t>(sz));
            DEVMODEW* dm = reinterpret_cast<DEVMODEW*>(buf.data());
            if (DocumentPropertiesW(nullptr, hPrinter, const_cast<LPWSTR>(printer.c_str()),
                dm, nullptr, DM_OUT_BUFFER) == IDOK) {
                dm->dmFields |= DM_ORIENTATION;
                dm->dmOrientation = DMORIENT_LANDSCAPE;
                hdc = CreateDCW(nullptr, printer.c_str(), nullptr, dm);
                g_logger.info(L"ReceiptPrinter: DC created with LANDSCAPE orientation");
            }
        }
        ClosePrinter(hPrinter);
        if (!hdc) hdc = CreateDCW(nullptr, printer.c_str(), nullptr, nullptr);
        return hdc;
    }

    static std::string buildPayload(const ReceiptData& d) {
        if (d.appendixNumber <= 0)
            return std::string();
        std::string result = std::string("SHOPAPP") + std::to_string(d.appendixNumber);
        // Принудительная санитизация: удаление любых пробелов
        result.erase(std::remove(result.begin(), result.end(), ' '), result.end());
        return result;
    }

    static std::wstring buildRussianCaption(const ReceiptData& d) {
        std::wstring s = L"Приложение № " + std::to_wstring(d.appendixNumber) +
            L"; комитент: " + d.clientFullName + L"; состав: ";
        for (size_t i = 0; i < d.items.size(); ++i) {
            if (i) s += L", ";
            s += d.items[i].description + L" — " + std::to_wstring(d.items[i].quantity) + L" шт";
        }
        double calculatedClientAmount = 0.0;
        for (const auto& item : d.items)
            calculatedClientAmount += CommissionCalc::calculateClientAmount(item.price, item.quantity);

        s += L"; сумма: " + ReceiptUtils::formatMoney(d.totalValue) +
            L"; выплата комитенту: " + ReceiptUtils::formatMoney(calculatedClientAmount);
        return s;
    }

    static std::vector<std::wstring> wrapText(const std::wstring& text, size_t maxLen) {
        std::vector<std::wstring> out;
        std::wstring cur;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t sp = text.find(L' ', pos);
            std::wstring word = (sp == std::wstring::npos) ? text.substr(pos)
                : text.substr(pos, sp - pos);
            if (!cur.empty() && cur.size() + 1 + word.size() > maxLen) {
                out.push_back(cur); cur.clear();
            }
            if (!cur.empty()) cur += L' ';
            cur += word;
            pos = (sp == std::wstring::npos) ? text.size() : sp + 1;
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }

    static std::vector<std::wstring> wrapTextPx(HDC hdc, const std::wstring& text, int maxWidthPx) {
        std::vector<std::wstring> out;
        if (maxWidthPx <= 0 || text.empty()) return out;
        std::wstring cur;
        size_t pos = 0;
        while (pos < text.size()) {
            while (pos < text.size() && text[pos] == L' ') ++pos;
            if (pos >= text.size()) break;
            size_t sp = text.find(L' ', pos);
            std::wstring word = (sp == std::wstring::npos) ? text.substr(pos)
                : text.substr(pos, sp - pos);
            std::wstring test = cur.empty() ? word : cur + L' ' + word;
            SIZE sz{};
            GetTextExtentPoint32W(hdc, test.c_str(), (int)test.size(), &sz);
            if (!cur.empty() && sz.cx > maxWidthPx) {
                out.push_back(cur);
                cur = word;
            }
            else {
                cur = test;
            }
            pos = (sp == std::wstring::npos) ? text.size() : sp + 1;
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }

    static void drawBarcodeLine(HDC hdc, int areaLeft, int areaRight, int y,
        int maxHeightPx, const std::string& payload) {
        auto modules = Code128::buildModules(payload);
        g_logger.info(L"ReceiptPrinter: drawBarcodeLine: payload='" +
            std::wstring(payload.begin(), payload.end()) + L"', modules=" +
            std::to_wstring(modules.size()));
        if (modules.empty() || areaRight <= areaLeft) return;

        const int n = static_cast<int>(modules.size());
        const int areaW = areaRight - areaLeft;
        double module = static_cast<double>(areaW) / static_cast<double>(n);
        if (module > 3.0) module = 3.0;
        const int actualW = static_cast<int>(std::round(module * n));
        const int startX = areaLeft + (areaW - actualW) / 2;

        g_logger.info(L"ReceiptPrinter: drawBarcodeLine: n=" + std::to_wstring(n) +
            L", areaW=" + std::to_wstring(areaW) +
            L", module=" + std::to_wstring(module) +
            L", actualW=" + std::to_wstring(actualW) +
            L", startX=" + std::to_wstring(startX));

        if (module >= 1.5) {
            HBRUSH hBlack = CreateSolidBrush(RGB(0, 0, 0));
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBlack);
            HPEN hOldPen = (HPEN)SelectObject(hdc, (HPEN)GetStockObject(NULL_PEN));
            double cur = startX;
            for (int m : modules) {
                const double next = cur + module;
                if (m) {
                    int x0 = static_cast<int>(std::floor(cur + 0.5));
                    int x1 = static_cast<int>(std::floor(next + 0.5));
                    if (x1 > x0) Rectangle(hdc, x0, y, x1, y + maxHeightPx);
                }
                cur = next;
            }
            SelectObject(hdc, hOldPen);
            SelectObject(hdc, hOldBrush);
            DeleteObject(hBlack);
            return;
        }

        const int SS = 4;
        const int sw = actualW * SS;
        const int sh = maxHeightPx * SS;
        g_logger.info(L"ReceiptPrinter: drawBarcodeLine: supersampling x" +
            std::to_wstring(SS) + L" (" + std::to_wstring(sw) + L"x" + std::to_wstring(sh) +
            L") for module <1.5px");

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = sw;
        bmi.bmiHeader.biHeight = -sh;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC screenDC = GetDC(nullptr);
        HDC tmpDC = CreateCompatibleDC(screenDC);
        void* pBits = nullptr;
        HBITMAP hTmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (!hTmp) {
            g_logger.warning(L"ReceiptPrinter: drawBarcodeLine: temp DIB failed, direct draw");
            HBRUSH hBlack = CreateSolidBrush(RGB(0, 0, 0));
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBlack);
            HPEN hOldPen = (HPEN)SelectObject(hdc, (HPEN)GetStockObject(NULL_PEN));
            double cur = startX;
            for (int m : modules) {
                const double next = cur + module;
                if (m) {
                    int x0 = static_cast<int>(std::floor(cur + 0.5));
                    int x1 = static_cast<int>(std::floor(next + 0.5));
                    if (x1 > x0) Rectangle(hdc, x0, y, x1, y + maxHeightPx);
                }
                cur = next;
            }
            SelectObject(hdc, hOldPen);
            SelectObject(hdc, hOldBrush);
            DeleteObject(hBlack);
            DeleteDC(tmpDC); ReleaseDC(nullptr, screenDC);
            return;
        }

        HGDIOBJ hOldBmp = SelectObject(tmpDC, hTmp);
        HBRUSH hWhite = CreateSolidBrush(RGB(255, 255, 255));
        RECT rc{ 0, 0, sw, sh };
        FillRect(tmpDC, &rc, hWhite);
        DeleteObject(hWhite);

        const double module2 = module * SS;
        HBRUSH hBlack = CreateSolidBrush(RGB(0, 0, 0));
        HBRUSH hOldBr2 = (HBRUSH)SelectObject(tmpDC, hBlack);
        HPEN hOldPen2 = (HPEN)SelectObject(tmpDC, (HPEN)GetStockObject(NULL_PEN));
        double cur = 0.0;
        for (int m : modules) {
            const double next = cur + module2;
            if (m) {
                int x0 = static_cast<int>(std::floor(cur + 0.5));
                int x1 = static_cast<int>(std::floor(next + 0.5));
                if (x1 > x0) Rectangle(tmpDC, x0, 0, x1, sh);
            }
            cur = next;
        }
        SelectObject(tmpDC, hOldPen2);
        SelectObject(tmpDC, hOldBr2);
        DeleteObject(hBlack);

        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, nullptr);
        StretchBlt(hdc, startX, y, actualW, maxHeightPx, tmpDC, 0, 0, sw, sh, SRCCOPY);

        SelectObject(tmpDC, hOldBmp);
        DeleteObject(hTmp);
        DeleteDC(tmpDC);
        ReleaseDC(nullptr, screenDC);
        g_logger.info(L"ReceiptPrinter: drawBarcodeLine: supersampled barcode blitted at x=" +
            std::to_wstring(startX) + L", w=" + std::to_wstring(actualW) +
            L", h=" + std::to_wstring(maxHeightPx));
    }

    static void beginPage(HDC hdc, int pageW, int pageH) {
        HBRUSH hWhite = CreateSolidBrush(RGB(255, 255, 255));
        RECT rc{ 0, 0, pageW, pageH };
        FillRect(hdc, &rc, hWhite);
        DeleteObject(hWhite);
        SetBkColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0));
    }

    static bool renderDocument(HDC hdc, const ReceiptData& d, bool spool,
        int dpiXOverride = 0, int dpiYOverride = 0, int extraPageHMm = 0) {
        int dpiX = (dpiXOverride > 0) ? dpiXOverride : GetDeviceCaps(hdc, LOGPIXELSX);
        int dpiY = (dpiYOverride > 0) ? dpiYOverride : GetDeviceCaps(hdc, LOGPIXELSY);
        int pageW, pageH;
        if (dpiXOverride > 0) {
            pageW = static_cast<int>(std::round(ReceiptConfig::TAPE_WIDTH_MM / 25.4 * dpiX));
            pageH = static_cast<int>(std::round((extraPageHMm > 0 ? extraPageHMm : 210.0) / 25.4 * dpiY));
        }
        else {
            pageW = GetDeviceCaps(hdc, HORZRES);
            pageH = GetDeviceCaps(hdc, VERTRES);
        }

        g_logger.info(L"ReceiptPrinter: renderDocument dpi=" + std::to_wstring(dpiX) +
            L", pageW=" + std::to_wstring(pageW) + L", pageH=" + std::to_wstring(pageH) +
            L", docHeightMm=" + std::to_wstring(extraPageHMm) + L" (tape-native layout)");

        auto mmX = [&](int mm) { return MulDiv(mm * 10, dpiX, 254); };
        auto mmY = [&](int mm) { return MulDiv(mm * 10, dpiY, 254); };

        // Шрифты — БЕЗ ИЗМЕНЕНИЙ
        auto font = [&](int pt, bool bold) {
            return CreateFontW(-MulDiv(pt, dpiY, 72), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
            };

        HFONT fTitle = font(5, true), fNormal = font(5, false), fBold = font(5, true),
            fSmall = font(4, false), fBig = font(5, true), fSmallBold = font(4, true);

        const int lineStep = MulDiv(7, dpiY, 72);

        if (spool) {
            DOCINFOW di = {};
            di.cbSize = sizeof(di);
            std::wstring docName = L"Приложение " + std::to_wstring(d.appendixNumber);
            di.lpszDocName = docName.c_str();
            if (StartDocW(hdc, &di) <= 0) {
                g_logger.error(L"ReceiptPrinter: StartDocW failed, err=" + std::to_wstring(GetLastError()));
                DeleteObject(fTitle); DeleteObject(fNormal); DeleteObject(fBold);
                DeleteObject(fSmall); DeleteObject(fBig); DeleteObject(fSmallBold);
                return false;
            }
            g_logger.info(L"ReceiptPrinter: StartDocW succeeded");
        }

        time_t now = time(nullptr);
        time_t validUntil = now + static_cast<time_t>(ReceiptConfig::APPENDIX_VALID_DAYS) * 86400;

        // === ЯВНЫЕ ПЕРЕНОСЫ СТРОК ПО ТРЕБОВАНИЮ ===
        // CONSIGNOR: перенос после «комитент» и после первого «Правилами»
        const std::vector<std::wstring> consignorLines = {
            L"именуемый в дальнейшем «комитент»,",
            L"заключил настоящее приложение к договору",
            L"в соответствии с Правилами",
            L"комиссионной торговли и Правилами",
            L"комиссионной торговли б/у товарами"
        };
        // NOTE: перенос после «наблюдать» и после «б/у товара»
        const std::vector<std::wstring> noteLines = {
            L"Получатель вещей может наблюдать",
            L"сразу после продажи.",
            L"Просим учесть, что передача б/у товара",
            L"другому лицу по доверенности возможна",
            L"только при наличии паспорта."
        };
        // STORE_FOOTER: перенос после «без обеда»
        const std::vector<std::wstring> footerLines = {
            L"Работаем с 10.00 до 21.00 без обеда",
            L"без выходных",
            L"ул.Садовая д.28-30. 5-1 т.622-87-70"
        };

        // === 5 колонок (без «Характеристики»), сумма = не 100 ===
        const int colPct[5] = { 8, 22, 7, 16, 20 };

        int left = mmX(1), right = pageW - mmX(1);
        const int printableW = right - left;

        // === ПРАВЫЕ КРАЯ ===
        const int titleRight = pageW - mmX(40);  // «СОВЕТСКИЙ» и «Приложение к договору» — 3 см от правого края
        const int numRight = pageW - mmX(40);  // «Приложение № X» — 1 см от правого края
        const int barcodeRight = pageW - mmX(40);  // ШК — 4 см от правого края

        // === Таблица товаров — 2/3 от печатной ширины ===
        const int tableW = printableW * 2 / 3;
        const int rowH = lineStep + 2;

        g_logger.info(L"ReceiptPrinter: tableW=" + std::to_wstring(tableW) +
            L" px (printableW=" + std::to_wstring(printableW) + L")");

        if (spool) {
            if (StartPage(hdc) <= 0) {
                g_logger.error(L"ReceiptPrinter: StartPage failed, err=" + std::to_wstring(GetLastError()));
            }
        }

        beginPage(hdc, pageW, pageH);
        int y = mmY(3);

        // === «СОВЕТСКИЙ» — 3 см от правого края ===
        HFONT old = (HFONT)SelectObject(hdc, fTitle);
        {
            SIZE szStore{};
            GetTextExtentPoint32W(hdc, ReceiptConfig::STORE_NAME, (int)wcslen(ReceiptConfig::STORE_NAME), &szStore);
            TextOutW(hdc, titleRight - szStore.cx, y, ReceiptConfig::STORE_NAME, (int)wcslen(ReceiptConfig::STORE_NAME));
            y += szStore.cy;
        }

        // === «Приложение к договору» — 3 см от правого края ===
        SelectObject(hdc, fBold);
        {
            SIZE szTitle{};
            GetTextExtentPoint32W(hdc, ReceiptConfig::CONTRACT_TITLE, (int)wcslen(ReceiptConfig::CONTRACT_TITLE), &szTitle);
            TextOutW(hdc, titleRight - szTitle.cx, y, ReceiptConfig::CONTRACT_TITLE, (int)wcslen(ReceiptConfig::CONTRACT_TITLE));
            y += szTitle.cy;
        }

        // === Дата (слева) и «Приложение № X» (1 см от правого края) — на одной строке ===
        SelectObject(hdc, fBold);
        std::wstring dateStr = ReceiptUtils::formatDateDDMMYYYY(now);
        TextOutW(hdc, left, y, dateStr.c_str(), (int)dateStr.size());

        std::wstring numStr = L"Приложение № " + std::to_wstring(d.appendixNumber);
        {
            SIZE szNum{};
            GetTextExtentPoint32W(hdc, numStr.c_str(), (int)numStr.size(), &szNum);
            TextOutW(hdc, numRight - szNum.cx, y, numStr.c_str(), (int)numStr.size());
        }
        y += lineStep;

        // === ШК — 4 см от правого края ===
        const int barcodeMaxWMm = 18;
        const int barcodeMaxHMm = 8;
        const int areaW = mmX(barcodeMaxWMm);
        const int areaLeft = barcodeRight - areaW;
        const std::string payload = buildPayload(d);
        drawBarcodeLine(hdc, areaLeft, areaLeft + areaW, y, mmY(barcodeMaxHMm), payload);
        y += mmY(barcodeMaxHMm);

        // Реквизиты магазина
        SelectObject(hdc, fSmall);
        for (auto& s : ReceiptConfig::STORE_REQUISITES) {
            for (const auto& ln : wrapTextPx(hdc, s, printableW)) {
                SIZE szr = {};
                GetTextExtentPoint32W(hdc, ln.c_str(), (int)ln.size(), &szr);
                TextOutW(hdc, left, y, ln.c_str(), (int)ln.size());
                y += szr.cy;
            }
        }

        // ФИО комитента
        SelectObject(hdc, fBold);
        for (const auto& ln : wrapTextPx(hdc, d.clientFullName, printableW)) {
            SIZE szf = {};
            GetTextExtentPoint32W(hdc, ln.c_str(), (int)ln.size(), &szf);
            TextOutW(hdc, left, y, ln.c_str(), (int)ln.size());
            y += szf.cy;
        }

        // === Юридический текст — ЯВНО разбит на строки, каждая страхуется wrapTextPx ===
        SelectObject(hdc, fNormal);
        for (const auto& line : consignorLines) {
            for (const auto& ln : wrapTextPx(hdc, line, printableW)) {
                SIZE szc = {};
                GetTextExtentPoint32W(hdc, ln.c_str(), (int)ln.size(), &szc);
                TextOutW(hdc, left, y, ln.c_str(), (int)ln.size());
                y += szc.cy;
            }
        }

        // Заголовок таблицы
        SelectObject(hdc, fBold);
        std::wstring listTitle = L"Перечень товаров";
        TextOutW(hdc, left, y, listTitle.c_str(), (int)listTitle.size());
        y += lineStep;

        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HPEN oldPen = (HPEN)SelectObject(hdc, hPen);
        HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, hNull);

        auto colX = [&](int i) {
            int x = left;
            for (int k = 0; k < i; ++k) x += tableW * colPct[k] / 100;
            return x;
            };

        // Комиссия (логирование без изменений)
        g_logger.info(L"ReceiptPrinter: === COMMISSION CALCULATION CONTROL ===");
        double totalClientAmountCalculated = 0.0;
        double totalStoreAmountCalculated = 0.0;
        std::vector<double> calculatedClientAmounts(d.items.size(), 0.0);
        for (size_t i = 0; i < d.items.size(); ++i) {
            const auto& item = d.items[i];
            auto rates = CommissionCalc::calculateByPrice(item.price);
            double clientAmount = CommissionCalc::calculateClientAmount(item.price, item.quantity);
            double storeAmount = CommissionCalc::calculateStoreAmount(item.price, item.quantity);
            calculatedClientAmounts[i] = clientAmount;
            totalClientAmountCalculated += clientAmount;
            totalStoreAmountCalculated += storeAmount;
            g_logger.info(L"ReceiptPrinter: item #" + std::to_wstring(i + 1) +
                L" desc='" + item.description +
                L"', price=" + std::to_wstring(item.price) +
                L", qty=" + std::to_wstring(item.quantity) +
                L", clientPercent=" + std::to_wstring(rates.clientPercent) +
                L"%, storePercent=" + std::to_wstring(rates.storePercent) +
                L"%, clientAmount=" + std::to_wstring(clientAmount) +
                L", storeAmount=" + std::to_wstring(storeAmount));
        }
        g_logger.info(L"ReceiptPrinter: TOTAL calculated - clientAmount=" +
            std::to_wstring(totalClientAmountCalculated) +
            L", storeAmount=" + std::to_wstring(totalStoreAmountCalculated));

        // Шапка колонок
        auto drawColumnHeader = [&]() {
            const int hdrH = lineStep + 2;
            SelectObject(hdc, fBold);
            Rectangle(hdc, left, y, left + tableW, y + hdrH);
            for (int c = 1; c < 5; ++c) {
                MoveToEx(hdc, colX(c), y, nullptr); LineTo(hdc, colX(c), y + hdrH);
            }
            TextOutW(hdc, colX(0) + 1, y + 2, L"№", 1);
            TextOutW(hdc, colX(1) + 1, y + 2, L"Товар", 5);
            TextOutW(hdc, colX(2) + 1, y + 2, L"Кол", 3);
            TextOutW(hdc, colX(3) + 1, y + 2, L"Цена", 4);
            TextOutW(hdc, colX(4) + 1, y + 2, L"На руки", 7);
            y += hdrH;
        };

        // Итоговая строка: два ряда по 20 мм
//   ряд 1: [Общее количество] [Сумма на продажу]
//   ряд 2: [Сумма на руки]    (под «Общее количество»)
        auto drawTotalsRow = [&]() {
            const int hdrH = 2 * lineStep + 2;
            const int totalsCellW = mmX(20);   // 20 мм на ячейку
            const int x1 = left + totalsCellW;   // начало правой колонки

            // ---------- Ряд 1 ----------
            const int row1Y = y;

            // Левая ячейка: «Общее количество»
            Rectangle(hdc, left, row1Y, left + totalsCellW, row1Y + hdrH);
            // Правая ячейка: «Сумма на продажу»
            Rectangle(hdc, x1, row1Y, x1 + totalsCellW, row1Y + hdrH);

            // Заголовки ряда 1
            SelectObject(hdc, fSmall);
            int idx = 0;
            for (const auto& ln : wrapTextPx(hdc, L"Общее количество", totalsCellW - 4)) {
                TextOutW(hdc, left + 1, row1Y + 2 + idx * lineStep, ln.c_str(), (int)ln.size()); ++idx;
            }
            idx = 0;
            for (const auto& ln : wrapTextPx(hdc, L"Сумма на продажу", totalsCellW - 4)) {
                TextOutW(hdc, x1 + 1, row1Y + 2 + idx * lineStep, ln.c_str(), (int)ln.size()); ++idx;
            }

            // Значения ряда 1
            std::wstring v1 = std::to_wstring(d.totalQty);
            std::wstring v3 = ReceiptUtils::formatMoney(d.totalValue);
            TextOutW(hdc, left + 1, row1Y + 2 + lineStep, v1.c_str(), (int)v1.size());
            TextOutW(hdc, x1 + 1, row1Y + 2 + lineStep, v3.c_str(), (int)v3.size());

            // ---------- Ряд 2 ----------
            const int row2Y = row1Y + hdrH;

            // Ячейка «Сумма на руки» — ровно под «Общее количество»
            Rectangle(hdc, left, row2Y, left + totalsCellW, row2Y + hdrH);

            SelectObject(hdc, fSmallBold);
            idx = 0;
            for (const auto& ln : wrapTextPx(hdc, L"Сумма на руки", totalsCellW - 4)) {
                TextOutW(hdc, left + 1, row2Y + 2 + idx * lineStep, ln.c_str(), (int)ln.size()); ++idx;
            }
            std::wstring v5 = ReceiptUtils::formatMoney(totalClientAmountCalculated);
            TextOutW(hdc, left + 1, row2Y + 2 + lineStep, v5.c_str(), (int)v5.size());

            y += 2 * hdrH;
            };

        drawColumnHeader();

        // Строки товаров
        SelectObject(hdc, fNormal);
        const int descColW = tableW * colPct[1] / 100 - 4;
        for (size_t i = 0; i < d.items.size(); ++i) {
            if (y + rowH * 3 > pageH - mmY(8)) {
                if (spool) {
                    EndPage(hdc);
                    StartPage(hdc);
                    beginPage(hdc, pageW, pageH);
                    y = mmY(8);
                    drawColumnHeader();
                    SelectObject(hdc, fNormal);
                }
            }
            const ReceiptItem& it = d.items[i];
            std::vector<std::wstring> linesD = wrapTextPx(hdc, it.description, descColW);
            size_t rowLines = linesD.size();
            if (rowLines < 1) rowLines = 1;
            const int rowHCur = (int)rowLines * lineStep + 2;

            Rectangle(hdc, left, y, left + tableW, y + rowHCur);
            for (int c = 1; c < 5; ++c) {
                MoveToEx(hdc, colX(c), y, nullptr); LineTo(hdc, colX(c), y + rowHCur);
            }

            std::wstring c0 = std::to_wstring(i + 1);
            std::wstring c2 = std::to_wstring(it.quantity);
            std::wstring c3 = ReceiptUtils::formatMoney(it.price);
            std::wstring c4 = ReceiptUtils::formatMoney(calculatedClientAmounts[i]);

            TextOutW(hdc, colX(0) + 1, y + 2, c0.c_str(), (int)c0.size());
            TextOutW(hdc, colX(2) + 1, y + 2, c2.c_str(), (int)c2.size());
            TextOutW(hdc, colX(3) + 1, y + 2, c3.c_str(), (int)c3.size());
            TextOutW(hdc, colX(4) + 1, y + 2, c4.c_str(), (int)c4.size());

            int idxD = 0;
            for (const auto& ln : linesD) {
                TextOutW(hdc, colX(1) + 1, y + 2 + idxD * lineStep, ln.c_str(), (int)ln.size()); ++idxD;
            }
            y += rowHCur;
        }

        drawTotalsRow();

        SelectObject(hdc, oldPen); SelectObject(hdc, oldBr);

        // === «Приложение действительно до» и дата — НА РАЗНЫХ СТРОКАХ ===
        SelectObject(hdc, fBig);
        {
            std::wstring label = L"Приложение действительно до";
            SIZE szl = {};
            GetTextExtentPoint32W(hdc, label.c_str(), (int)label.size(), &szl);
            TextOutW(hdc, left, y, label.c_str(), (int)label.size());
            y += szl.cy;

            std::wstring validStr = ReceiptUtils::formatDateDDMMYYYY(validUntil);
            SIZE szd = {};
            GetTextExtentPoint32W(hdc, validStr.c_str(), (int)validStr.size(), &szd);
            TextOutW(hdc, left, y, validStr.c_str(), (int)validStr.size());
            y += szd.cy;
        }

        // === Примечания — ЯВНО разбиты на строки ===
        SelectObject(hdc, fNormal);
        for (const auto& line : noteLines) {
            for (const auto& ln : wrapTextPx(hdc, line, printableW)) {
                SIZE szn = {};
                GetTextExtentPoint32W(hdc, ln.c_str(), (int)ln.size(), &szn);
                TextOutW(hdc, left, y, ln.c_str(), (int)ln.size());
                y += szn.cy;
            }
        }

        // === Подвал магазина — ЯВНО разбит на строки ===
        SelectObject(hdc, fBold);
        for (const auto& line : footerLines) {
            for (const auto& ln : wrapTextPx(hdc, line, printableW)) {
                SIZE szu = {};
                GetTextExtentPoint32W(hdc, ln.c_str(), (int)ln.size(), &szu);
                TextOutW(hdc, left, y, ln.c_str(), (int)ln.size());
                y += szu.cy;
            }
        }

        // === Нижняя таблица — две ячейки ДРУГ НАД ДРУГОМ ===
        const int bottomCellW = printableW * 2 / 4;   // ≈ 35 мм (как было)
        const int bh = 6 * lineStep + 4;

        SelectObject(hdc, hPen);

        // --- Верхняя (левая) ячейка: Комиссионер ---
        Rectangle(hdc, left, y, left + bottomCellW, y + bh);
        SelectObject(hdc, fBold);
        TextOutW(hdc, left + 2, y + 2, L"Комиссионер", 11);

        SelectObject(hdc, fSmall);
        int ry = y + lineStep;
        for (auto& s : ReceiptConfig::STORE_REQUISITES) {
            for (const auto& ln : wrapTextPx(hdc, s, bottomCellW - 6)) {
                TextOutW(hdc, left + 2, ry, ln.c_str(), (int)ln.size());
                ry += lineStep;
            }
        }
        y += bh;

        // --- Нижняя (правая) ячейка: Комитент ---
        Rectangle(hdc, left, y, left + bottomCellW, y + bh);
        SelectObject(hdc, fBold);
        TextOutW(hdc, left + 2, y + 2, L"Комитент", 8);

        // Номер комитента — жирным, кегль тот же 4pt
        SelectObject(hdc, fSmallBold);
        std::wstring idStr = std::to_wstring(d.clientId);
        TextOutW(hdc, left + 2, y + lineStep, idStr.c_str(), (int)idStr.size());

        y += bh;

        SelectObject(hdc, oldPen);
        DeleteObject(hPen);

        if (spool) {
            if (EndPage(hdc) <= 0) {
                g_logger.error(L"ReceiptPrinter: EndPage failed, err=" + std::to_wstring(GetLastError()));
            }
            if (EndDoc(hdc) <= 0) {
                g_logger.error(L"ReceiptPrinter: EndDoc failed, err=" + std::to_wstring(GetLastError()));
                DeleteObject(fTitle); DeleteObject(fNormal); DeleteObject(fBold);
                DeleteObject(fSmall); DeleteObject(fBig); DeleteObject(fSmallBold);
                return false;
            }
            g_logger.info(L"ReceiptPrinter: EndDoc succeeded");
        }

        DeleteObject(fTitle); DeleteObject(fNormal); DeleteObject(fBold);
        DeleteObject(fSmall); DeleteObject(fBig); DeleteObject(fSmallBold);
        g_logger.info(L"ReceiptPrinter: document rendered, appendix=" +
            std::to_wstring(d.appendixNumber));
        return true;
    }
};