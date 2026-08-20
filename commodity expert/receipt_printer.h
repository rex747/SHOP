// receipt_printer.h
// =============================================================================
// МОДУЛЬ ПЕЧАТИ «ПРИЛОЖЕНИЕ К ДОГОВОРУ» (чек комитента, как в образце 222.jp)
// =============================================================================
// ОБНОВЛЕНИЕ: Добавлен автоматический расчет процентов по цене единицы товара.
// Логика расчета (CommissionCalc::calculateByPrice):
//   - 1-299 ₽:     магазин 48%, комитент 52%
//   - 300-599 ₽:   магазин 46%, комитент 54%
//   - 600-999 ₽:   магазин 44%, комитент 56%
//   - 1000-2499 ₽: магазин 42%, комитент 58%
//   - 2500-3999 ₽: магазин 40%, комитент 60%
//   - 4000-6999 ₽: магазин 36%, комитент 64%
//   - 7000-9999 ₽: магазин 34%, комитент 66%
//   - 10000-19999 ₽: магазин 30%, комитент 70%
//   - 20000-49999 ₽: магазин 26%, комитент 74%
//   - от 50000 ₽:  магазин 19%, комитент 81%
//
// Расчет производится при формировании приложения к договору для контроля
// и валидации данных, полученных из worker_window.h.
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
#pragma comment(lib, "winspool.lib")

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

extern Logger g_logger;

// =============================================================================
// АВТОМАТИЧЕСКИЙ РАСЧЕТ ПРОЦЕНТОВ ПО ЦЕНЕ ЕДИНИЦЫ ТОВАРА
// =============================================================================
namespace CommissionCalc {
    struct CommissionRates {
        double storePercent;
        double clientPercent;
    };

    // Расчет процентов по цене единицы товара
    inline CommissionRates calculateByPrice(double unitPrice) {
        if (unitPrice >= 50000.0)       return { 19.0, 81.0 };
        if (unitPrice >= 20000.0)       return { 26.0, 74.0 };
        if (unitPrice >= 10000.0)       return { 30.0, 70.0 };
        if (unitPrice >= 7000.0)        return { 34.0, 66.0 };
        if (unitPrice >= 4000.0)        return { 36.0, 64.0 };
        if (unitPrice >= 2500.0)        return { 40.0, 60.0 };
        if (unitPrice >= 1000.0)        return { 42.0, 58.0 };
        if (unitPrice >= 600.0)         return { 44.0, 56.0 };
        if (unitPrice >= 300.0)         return { 46.0, 54.0 };
        if (unitPrice >= 1.0)           return { 48.0, 52.0 };
        return { 0.0, 0.0 };
    }
}

// =============================================================================
// РЕКВИЗИТЫ И ТЕКСТЫ ЧЕКА
// =============================================================================
namespace ReceiptConfig {
    constexpr int APPENDIX_VALID_DAYS = 15;
    inline const wchar_t* PRINTER_NAME_OVERRIDE = L"";
    inline const wchar_t* CONTRACT_TITLE = L"Приложение к договору ";
    inline const wchar_t* STORE_NAME = L"«СОВЕТСКИЙ» ";
    inline const std::vector<std::wstring> STORE_REQUISITES = {
        L"ООО «Комиссионный магазин «СОВЕТСКИЙ» ",
        L"ПЕРЕГУДА Е.А.  № 3045.19023100221 ",
        L"Телефон 9168825439 ",
        L"Выдано ИМНС РФ по г. Москве ",
        L"от 19.06.2004 серия 51пр 005867863 "
    };
    inline const wchar_t* CONSIGNOR_TEXT =
        L"именуемый в дальнейшем «комитент», заключил настоящее приложение к договору в соответствии с Правилами комиссионной торговли и Правилами комиссионной торговли б/у товарами. ";
    inline const std::vector<std::wstring> NOTE_TEXT = {
        L"Получатель вещей может наблюдать сразу после продажи. ",
        L"Просим учесть, что передача б/у товара другому лицу по доверенности возможна только при наличии паспорта. "
    };
    inline const std::vector<std::wstring> STORE_FOOTER = {
        L"Работаем с 10.00 до 21.00 без обеда без выходных ",
        L"ул.Садовая д.28-30. 5-1 т. 242-90-51 ",
        L"www.Piterkomok.ru "
    };
}

// =============================================================================
// ДАННЫЕ ДЛЯ ПЕЧАТИ
// =============================================================================
struct ReceiptItem {
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

// =============================================================================
// СЛУЖЕБНЫЕ ФУНКЦИИ
// =============================================================================
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

// =============================================================================
// CODE 128
// =============================================================================
namespace Code128 {
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
        std::vector<int> values;
        values.push_back(104);
        long long sum = 104;
        size_t pos = 1;
        for (unsigned char c : payload) {
            int v = (c >= 32 && c <= 126) ? (c - 32) : ('?' - 32);
            values.push_back(v);
            sum += static_cast<long long>(v) * pos++;
        }
        values.push_back(static_cast<int>(sum % 103));
        values.push_back(106);
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

    inline size_t modulesFor(size_t chars) { return (chars + 3) * 11 + 2; }
}

// =============================================================================
// ПРИНТЕР
// =============================================================================
class ReceiptPrinter {
public:
    static bool print(const ReceiptData& data) {
        g_logger.info(L"ReceiptPrinter::print: started, appendix=" +
            std::to_wstring(data.appendixNumber) + L", client=" + std::to_wstring(data.clientId) +
            L", items=" + std::to_wstring(data.items.size()));

        std::wstring png = renderPreviewPng(data);
        if (!png.empty())
            g_logger.info(L"ReceiptPrinter: PNG preview saved: " + png);
        else
            g_logger.warning(L"ReceiptPrinter: PNG preview was NOT created");

        std::wstring printer = selectPrinter();
        if (printer.empty()) {
            g_logger.error(L"ReceiptPrinter::print: no printer available");
            return false;
        }
        g_logger.info(L"ReceiptPrinter: selected printer = " + printer);

        if (isPromptPort(printer)) {
            g_logger.warning(L"ReceiptPrinter: printer '" + printer +
                L"' is interactive. Spooler print SKIPPED; inspect PNG preview: " + png);
            return true;
        }

        HDC hdc = createLandscapeDC(printer);
        if (!hdc) {
            g_logger.error(L"ReceiptPrinter::print: CreateDC failed for " + printer +
                L", err=" + std::to_wstring(GetLastError()));
            return false;
        }

        if (SetAbortProc(hdc, AbortProc) <= 0) {
            g_logger.warning(L"ReceiptPrinter: SetAbortProc failed, err=" +
                std::to_wstring(GetLastError()));
        }
        else {
            g_logger.info(L"ReceiptPrinter: SetAbortProc installed");
        }

        g_logger.info(L"ReceiptPrinter: starting renderDocument (spool=true)");
        bool ok = renderDocument(hdc, data, true);
        DeleteDC(hdc);
        g_logger.info(L"ReceiptPrinter::print: finished, ok=" +
            std::wstring(ok ? L"true" : L"false") +
            L", appendix=" + std::to_wstring(data.appendixNumber));
        return ok;
    }

    friend class PriceTagPrinter;

private:
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
                if (_wcsicmp(port.c_str(), L"PORTPROMPT:") == 0 ||
                    _wcsicmp(port.c_str(), L"FILE:") == 0) {
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
        std::call_once(flag, []() {
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
        std::wstring pngPath = dir + L"\\appendix_" + std::to_wstring(d.appendixNumber) + L".png";

        int dpi = 600;
        int pxW = (int)std::round(297.0 / 25.4 * dpi);
        int pxH = (int)std::round(210.0 / 25.4 * dpi);
        g_logger.info(L"ReceiptPrinter: preview DIB " + std::to_wstring(pxW) + L"x" +
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
            pxW = (int)std::round(297.0 / 25.4 * dpi);
            pxH = (int)std::round(210.0 / 25.4 * dpi);
            bmi.bmiHeader.biWidth = pxW;
            bmi.bmiHeader.biHeight = -pxH;
            hBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
            g_logger.warning(L"ReceiptPrinter: 600 DPI DIB failed, fallback 300 DPI");
        }
        if (!hBmp) {
            g_logger.error(L"ReceiptPrinter: CreateDIBSection failed");
            DeleteDC(memDC); ReleaseDC(nullptr, screenDC);
            return result;
        }
        HGDIOBJ hOldBmp = SelectObject(memDC, hBmp);
        if (hOldBmp == nullptr || hOldBmp == HGDI_ERROR) {
            g_logger.error(L"ReceiptPrinter: SelectObject(memDC, hBmp) FAILED, err=" +
                std::to_wstring(GetLastError()));
            DeleteObject(hBmp);
            DeleteDC(memDC);
            ReleaseDC(nullptr, screenDC);
            return result;
        }
        g_logger.info(L"ReceiptPrinter: DIB selected into memDC");

        HBRUSH hWhite = CreateSolidBrush(RGB(255, 255, 255));
        RECT rc{ 0, 0, pxW, pxH };
        FillRect(memDC, &rc, hWhite);
        DeleteObject(hWhite);

        renderDocument(memDC, d, false, dpi, dpi);

        {
            Gdiplus::Bitmap bmp(hBmp, nullptr);
            bmp.SetResolution(static_cast<Gdiplus::REAL>(dpi),
                static_cast<Gdiplus::REAL>(dpi));
            CLSID clsid{};
            if (getPngEncoderClsid(clsid)) {
                if (bmp.Save(pngPath.c_str(), &clsid, nullptr) == Gdiplus::Ok)
                    result = pngPath;
                else
                    g_logger.error(L"ReceiptPrinter: PNG save failed");
            }
        }
        SelectObject(memDC, hOldBmp);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
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
        std::ostringstream ss;
        ss << "APP=" << d.appendixNumber
            << ";ID=" << d.clientId
            << ";FIO=" << ReceiptUtils::transliterate(d.clientFullName)
            << ";ITEMS=";
        for (size_t i = 0; i < d.items.size(); ++i) {
            if (i) ss << ", ";
            ss << ReceiptUtils::transliterate(d.items[i].description)
                << " " << d.items[i].quantity;
        }
        ss << ";SUM=" << static_cast<long long>(std::llround(d.totalValue))
            << ";PAY=" << d.totalClientAmount;
        return ss.str();
    }

    static std::wstring buildRussianCaption(const ReceiptData& d) {
        std::wstring s = L"Приложение №  " + std::to_wstring(d.appendixNumber) +
            L"; комитент:  " + d.clientFullName + L"; состав:  ";
        for (size_t i = 0; i < d.items.size(); ++i) {
            if (i) s += L",  ";
            s += d.items[i].description + L" —  " + std::to_wstring(d.items[i].quantity) + L" шт ";
        }
        s += L"; сумма:  " + ReceiptUtils::formatMoney(d.totalValue) +
            L"; выплата комитенту:  " + ReceiptUtils::formatMoney(d.totalClientAmount);
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
            std::to_wstring(SS) + L" (" + std::to_wstring(sw) + L"x" +
            std::to_wstring(sh) + L") for module<1.5px");
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
        int dpiXOverride = 0, int dpiYOverride = 0) {
        int dpiX = (dpiXOverride > 0) ? dpiXOverride : GetDeviceCaps(hdc, LOGPIXELSX);
        int dpiY = (dpiYOverride > 0) ? dpiYOverride : GetDeviceCaps(hdc, LOGPIXELSY);
        int pageW, pageH;
        if (dpiXOverride > 0) {
            pageW = static_cast<int>(std::round(297.0 / 25.4 * dpiX));
            pageH = static_cast<int>(std::round(210.0 / 25.4 * dpiY));
        }
        else {
            pageW = GetDeviceCaps(hdc, HORZRES);
            pageH = GetDeviceCaps(hdc, VERTRES);
        }
        g_logger.info(L"ReceiptPrinter: renderDocument dpi=" + std::to_wstring(dpiX) +
            L", pageW=" + std::to_wstring(pageW) + L", pageH=" + std::to_wstring(pageH));

        auto mmX = [&](int mm) { return MulDiv(mm, dpiX, 254) * 10; };
        auto mmY = [&](int mm) { return MulDiv(mm, dpiY, 254) * 10; };
        auto font = [&](int pt, bool bold) {
            return CreateFontW(-MulDiv(pt, dpiY, 72), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
            };
        HFONT fTitle = font(14, true), fNormal = font(9, false), fBold = font(9, true),
            fSmall = font(7, false), fBig = font(12, true);

        if (spool) {
            DOCINFOW di = {};
            di.cbSize = sizeof(di);
            std::wstring docName = L"Приложение_" + std::to_wstring(d.appendixNumber);
            di.lpszDocName = docName.c_str();
            if (StartDocW(hdc, &di) <= 0) {
                g_logger.error(L"ReceiptPrinter: StartDocW failed, err=" + std::to_wstring(GetLastError()));
                DeleteObject(fTitle); DeleteObject(fNormal); DeleteObject(fBold);
                DeleteObject(fSmall); DeleteObject(fBig);
                return false;
            }
            g_logger.info(L"ReceiptPrinter: StartDocW succeeded");
        }

        time_t now = time(nullptr);
        time_t validUntil = now + static_cast<time_t>(ReceiptConfig::APPENDIX_VALID_DAYS) * 86400;

        const int colPct[6] = { 6, 32, 20, 8, 15, 19 };
        int rowH = mmY(6);
        int left = mmX(10), right = pageW - mmX(10);
        int tableW = right - left;

        if (spool) {
            if (StartPage(hdc) <= 0) {
                g_logger.error(L"ReceiptPrinter: StartPage failed, err=" + std::to_wstring(GetLastError()));
            }
        }
        beginPage(hdc, pageW, pageH);
        int y = mmY(8);

        HFONT old = (HFONT)SelectObject(hdc, fTitle);
        std::wstring title = std::wstring(ReceiptConfig::CONTRACT_TITLE) + L" " + ReceiptConfig::STORE_NAME;
        TextOutW(hdc, left, y, title.c_str(), (int)title.size());
        y += mmY(8);

        SelectObject(hdc, fBold);
        std::wstring dateStr = ReceiptUtils::formatDateDDMMYYYY(now);
        TextOutW(hdc, left, y, dateStr.c_str(), (int)dateStr.size());
        std::wstring numStr = L"№ " + std::to_wstring(d.appendixNumber);
        TextOutW(hdc, right - mmX(40), y, numStr.c_str(), (int)numStr.size());

        const int barcodeMaxWMm = 50;
        const int barcodeMaxHMm = 20;
        const int areaW = mmX(barcodeMaxWMm);
        const int areaLeft = left + (tableW - areaW) / 2;
        const std::string payload = buildPayload(d);
        drawBarcodeLine(hdc, areaLeft, areaLeft + areaW, y, mmY(barcodeMaxHMm), payload);
        y += mmY(barcodeMaxHMm) + mmY(2);

        SelectObject(hdc, fSmall);
        SetBkMode(hdc, TRANSPARENT);
        for (const auto& line : wrapText(buildRussianCaption(d), 120)) {
            SIZE sz = {};
            GetTextExtentPoint32W(hdc, line.c_str(), (int)line.size(), &sz);
            TextOutW(hdc, left + (tableW - sz.cx) / 2, y, line.c_str(), (int)line.size());
            y += sz.cy + 2;
        }
        y += mmY(2);

        SelectObject(hdc, fSmall);
        for (auto& s : ReceiptConfig::STORE_REQUISITES) {
            TextOutW(hdc, left, y, s.c_str(), (int)s.size());
            y += mmY(4);
        }
        y += mmY(2);

        SelectObject(hdc, fBold);
        std::wstring fio = d.clientFullName;
        TextOutW(hdc, left, y, fio.c_str(), (int)fio.size());
        y += mmY(6);
        SelectObject(hdc, fNormal);
        std::wstring ct = ReceiptConfig::CONSIGNOR_TEXT;
        TextOutW(hdc, left, y, ct.c_str(), (int)ct.size());
        y += mmY(7);

        SelectObject(hdc, fBold);
        std::wstring listTitle = L"Перечень товаров";
        TextOutW(hdc, left, y, listTitle.c_str(), (int)listTitle.size());
        y += mmY(6);

        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HPEN oldPen = (HPEN)SelectObject(hdc, hPen);
        HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, hNull);

        auto colX = [&](int i) {
            int x = left;
            for (int k = 0; k < i; ++k) x += tableW * colPct[k] / 100;
            return x;
            };

        // =====================================================================
        // НОВОЕ: КОНТРОЛЬ РАСЧЕТА ПРОЦЕНТОВ ПО НОВОЙ ЛОГИКЕ
        // =====================================================================
        g_logger.info(L"ReceiptPrinter: === COMMISSION CALCULATION CONTROL ===");
        double totalClientAmountCalculated = 0.0;
        double totalStoreAmountCalculated = 0.0;
        for (size_t i = 0; i < d.items.size(); ++i) {
            const auto& item = d.items[i];
            auto rates = CommissionCalc::calculateByPrice(item.price);
            double clientAmount = item.price * item.quantity * rates.clientPercent / 100.0;
            double storeAmount = item.price * item.quantity * rates.storePercent / 100.0;
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
        // =====================================================================

        auto drawTableHeader = [&]() {
            SelectObject(hdc, fBold);
            Rectangle(hdc, left, y, right, y + rowH);
            for (int c = 1; c < 6; ++c) {
                if (c == 1) continue;
                MoveToEx(hdc, colX(c), y, nullptr); LineTo(hdc, colX(c), y + rowH);
            }
            SelectObject(hdc, fBold);
            std::wstring t0 = L"Общее количество", t2 = L"Сумма на продажу", t4 = L"Сумма на руки";
            TextOutW(hdc, colX(0) + 2, y + 2, t0.c_str(), (int)t0.size());
            TextOutW(hdc, colX(2) + 2, y + 2, t2.c_str(), (int)t2.size());
            TextOutW(hdc, colX(4) + 2, y + 2, t4.c_str(), (int)t4.size());
            SIZE szT0{};
            GetTextExtentPoint32W(hdc, t0.c_str(), (int)t0.size(), &szT0);
            std::wstring v1 = std::to_wstring(d.totalQty);
            std::wstring v3 = ReceiptUtils::formatMoney(d.totalValue);
            std::wstring v5 = ReceiptUtils::formatMoney(d.totalClientAmount);
            TextOutW(hdc, colX(0) + 2 + szT0.cx + mmX(3), y + 2, v1.c_str(), (int)v1.size());
            TextOutW(hdc, colX(3) + 2, y + 2, v3.c_str(), (int)v3.size());
            TextOutW(hdc, colX(5) + 2, y + 2, v5.c_str(), (int)v5.size());
            g_logger.info(L"ReceiptPrinter: totals row rendered");
            y += rowH + mmY(3);
            };
        drawTableHeader();
        SelectObject(hdc, fNormal);

        for (size_t i = 0; i < d.items.size(); ++i) {
            if (y + rowH * 3 > pageH - mmY(8)) {
                if (spool) {
                    EndPage(hdc);
                    StartPage(hdc);
                    beginPage(hdc, pageW, pageH);
                    y = mmY(8);
                    drawTableHeader();
                    SelectObject(hdc, fNormal);
                }
                else {
                    g_logger.warning(L"ReceiptPrinter: preview overflow at y=" +
                        std::to_wstring(y) + L" (pageH=" + std::to_wstring(pageH) +
                        L") — continuing without page reset");
                }
            }
            const ReceiptItem& it = d.items[i];
            Rectangle(hdc, left, y, right, y + rowH);
            for (int c = 1; c < 6; ++c) {
                MoveToEx(hdc, colX(c), y, nullptr); LineTo(hdc, colX(c), y + rowH);
            }
            std::wstring cells[6] = {
                std::to_wstring(i + 1), it.description, it.characteristic,
                std::to_wstring(it.quantity), ReceiptUtils::formatMoney(it.price),
                ReceiptUtils::formatMoney(it.clientAmount)
            };
            for (int c = 0; c < 6; ++c)
                TextOutW(hdc, colX(c) + 2, y + 2, cells[c].c_str(), (int)cells[c].size());
            y += rowH;
        }

        Rectangle(hdc, left, y, right, y + rowH);
        for (int c = 1; c < 6; ++c) {
            MoveToEx(hdc, colX(c), y, nullptr); LineTo(hdc, colX(c), y + rowH);
        }
        SelectObject(hdc, fBold);
        std::wstring t0 = L"Общее количество", t2 = L"Сумма на продажу", t4 = L"Сумма на руки";
        TextOutW(hdc, colX(0) + 2, y + 2, t0.c_str(), (int)t0.size());
        TextOutW(hdc, colX(2) + 2, y + 2, t2.c_str(), (int)t2.size());
        TextOutW(hdc, colX(4) + 2, y + 2, t4.c_str(), (int)t4.size());
        std::wstring v1 = std::to_wstring(d.totalQty);
        std::wstring v3 = ReceiptUtils::formatMoney(d.totalValue);
        std::wstring v5 = ReceiptUtils::formatMoney(d.totalClientAmount);
        TextOutW(hdc, colX(1) + 2, y + 2, v1.c_str(), (int)v1.size());
        TextOutW(hdc, colX(3) + 2, y + 2, v3.c_str(), (int)v3.size());
        TextOutW(hdc, colX(5) + 2, y + 2, v5.c_str(), (int)v5.size());
        y += rowH + mmY(3);
        SelectObject(hdc, oldPen); SelectObject(hdc, oldBr);

        SelectObject(hdc, fNormal);
        for (auto& s : ReceiptConfig::NOTE_TEXT) {
            TextOutW(hdc, left, y, s.c_str(), (int)s.size());
            y += mmY(5);
        }
        y += mmY(2);

        SelectObject(hdc, fBig);
        std::wstring valid = L"Приложение действительно до  " +
            ReceiptUtils::formatDateDDMMYYYY(validUntil);
        TextOutW(hdc, left, y, valid.c_str(), (int)valid.size());
        y += mmY(8);

        SelectObject(hdc, fBold);
        for (auto& s : ReceiptConfig::STORE_FOOTER) {
            TextOutW(hdc, left, y, s.c_str(), (int)s.size());
            y += mmY(5);
        }
        y += mmY(3);

        int midX = left + tableW / 2;
        int bh = mmY(24);
        SelectObject(hdc, hPen);
        Rectangle(hdc, left, y, right, y + bh);
        MoveToEx(hdc, midX, y, nullptr); LineTo(hdc, midX, y + bh);
        SelectObject(hdc, fBold);
        std::wstring h1 = L"Комиссионер", h2 = L"Комитент";
        TextOutW(hdc, left + 2, y + 2, h1.c_str(), (int)h1.size());
        TextOutW(hdc, midX + 2, y + 2, h2.c_str(), (int)h2.size());
        SelectObject(hdc, fSmall);
        int ry = y + mmY(6);
        for (auto& s : ReceiptConfig::STORE_REQUISITES) {
            TextOutW(hdc, left + 2, ry, s.c_str(), (int)s.size());
            ry += mmY(4);
        }
        std::wstring idStr = std::to_wstring(d.clientId);
        TextOutW(hdc, midX + 2, y + mmY(6), idStr.c_str(), (int)idStr.size());
        SelectObject(hdc, oldPen);
        DeleteObject(hPen);

        if (spool) {
            if (EndPage(hdc) <= 0) {
                g_logger.error(L"ReceiptPrinter: EndPage failed, err=" + std::to_wstring(GetLastError()));
            }
            if (EndDoc(hdc) <= 0) {
                g_logger.error(L"ReceiptPrinter: EndDoc failed, err=" + std::to_wstring(GetLastError()));
                DeleteObject(fTitle); DeleteObject(fNormal); DeleteObject(fBold);
                DeleteObject(fSmall); DeleteObject(fBig);
                return false;
            }
            g_logger.info(L"ReceiptPrinter: EndDoc succeeded");
        }
        DeleteObject(fTitle); DeleteObject(fNormal); DeleteObject(fBold);
        DeleteObject(fSmall); DeleteObject(fBig);
        g_logger.info(L"ReceiptPrinter: document rendered, appendix=" +
            std::to_wstring(d.appendixNumber));
        return true;
    }
};