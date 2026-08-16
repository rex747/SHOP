// receipt_printer.h
// =============================================================================
// МОДУЛЬ ПЕЧАТИ «ПРИЛОЖЕНИЕ К ДОГОВОРУ» (чек комитента, как в образце 222.jpg)
// =============================================================================
// - Печатает чек на принтере, который программа определяет САМА
//   (GetDefaultPrinterW; при отсутствии — первый из EnumPrintersW).
// - Ориентация страницы: альбомная (как в образце), через DEVMODE.
// - Штрих-код Code 128 (B) с полезной нагрузкой: ФИО (транслит), ID комитента,
//   номер приложения, наименования*кол-во, общая сумма и сумма «на руки»
//   (сумма магазина в штрих-код и в чек НЕ включается — по требованию).
//   Вся полезная нагрузка (все позиции) кодируется ОДНИМ штрих-кодом Code 128,
//   размещаемым строго по центру, длиной не более 5 см и высотой не более 2 см
//   (дробный модуль с накоплением координат). Подпись под штрих-кодом —
//   на русском языке; в сам код идёт транслит (Code 128 не содержит кириллицы).
// - Вся математика берётся ГОТОВОЙ из json-полей (client_amount и т.д.) —
//   модуль ничего не пересчитывает.
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
#include <shlobj.h>      // SHGetFolderPathW для папки превью
#include <gdiplus.h>     // EMF -> PNG для контроля качества
#include <mutex>         // std::once_flag
#pragma comment(lib, "gdiplus.lib")

#include "logger.h"
#include "string_utils.h"
#pragma comment(lib, "winspool.lib")

// =============================================================================
// ЗАЩИТА ОТ МАКРОСОВ min/max ИЗ windows.h (WinDef.h).
// Без NOMINMAX макросы max/min ломают std::max/std::min (ошибка E0020).
// Снимаем макросы после всех включений заголовков Windows.
// =============================================================================
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

extern Logger g_logger;

// =============================================================================
// РЕКВИЗИТЫ И ТЕКСТЫ ЧЕКА («КАК ЕСТЬ» — заказчик правит самостоятельно)
// =============================================================================
namespace ReceiptConfig {
    // Срок действия приложения, календарных дней (решение заказчика: 15)
    constexpr int APPENDIX_VALID_DAYS = 15;
    // ВАЖНО ДЛЯ ПРОДАКШН: оставить пустую строку (L""), чтобы использовался
    // реальный принтер по умолчанию. Значение "Microsoft Print to PDF"
    // оставлять ТОЛЬКО для отладки на машине разработчика.
    inline const wchar_t* PRINTER_NAME_OVERRIDE = L""; // было: L"Microsoft Print to PDF"
    // Заголовок
    inline const wchar_t* CONTRACT_TITLE = L"Приложение к договору";
    inline const wchar_t* STORE_NAME = L"«СОВЕТСКИЙ»";
    // Реквизиты магазина (блок под штрих-кодом, как в образце)
    inline const std::vector<std::wstring> STORE_REQUISITES = {
        L"ООО «Комиссионный магазин «СОВЕТСКИЙ»",
        L"ПЕРЕГУДА Е.А.  № 3045.19023100221",
        L"Телефон 9168825439",
        L"Выдано ИМНС РФ по г. Москве",
        L"от 19.06.2004 серия 51пр 005867863"
    };
    // Текст под ФИО комитента
    inline const wchar_t* CONSIGNOR_TEXT =
        L"именуемый в дальнейшем «комитент», заключил настоящее приложение к договору в соответствии с Правилами комиссионной торговли и Правилами комиссионной торговли б/у товарами.";
    // Текст под таблицей
    inline const std::vector<std::wstring> NOTE_TEXT = {
        L"Получатель вещей может наблюдать сразу после продажи.",
        L"Просим учесть, что передача б/у товара другому лицу по доверенности возможна только при наличии паспорта."
    };
    // Режим работы (низ чека)
    inline const std::vector<std::wstring> STORE_FOOTER = {
        L"Работаем с 10.00 до 21.00 без обеда без выходных",
        L"ул.Садовая д.28-30. 5-1 т. 242-90-51",
        L"www.Piterkomok.ru"
    };
}

// =============================================================================
// ДАННЫЕ ДЛЯ ПЕЧАТИ (заполняются WorkerWindow, математика НЕ меняется)
// =============================================================================
struct ReceiptItem {
    std::wstring description;    // Товар
    std::wstring characteristic; // Характеристика (берётся из condition)
    int quantity = 1;
    double price = 0.0;
    double clientAmount = 0.0;   // «на руки» — готовое значение из client_amount
	std::wstring note;           // Примечание (берётся из note) которое печатается в таблице, но НЕ идёт в штрих-код приложения к договору
};
struct ReceiptData {
    long long appendixNumber = 0;
    int clientId = 0;
    std::wstring clientFullName;
    std::vector<ReceiptItem> items;
    int totalQty = 0;
    double totalValue = 0.0;        // «Сумма на продажу»
    double totalClientAmount = 0.0; // «Сумма на руки»
};

// =============================================================================
// СЛУЖЕБНЫЕ ФУНКЦИИ
// =============================================================================
namespace ReceiptUtils {
    // Детерминированная транслитерация (Code 128 не содержит кириллицы)
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

    // Формат денег как в образце (3521,6 / 130): %g + запятая
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
// CODE 128 (набор B). Стандартная таблица ширин (штрих/пробел, 6 чисел).
// =============================================================================
namespace Code128 {
    // widths[value] = 6 цифр (ширины штрих,пробел,...), сумма 11; stop сумма 13
    inline const wchar_t* widths(int v) {
        static const wchar_t* T[] = {
            L"212222",L"222122",L"222221",L"121223",L"121322",L"131222",L"122213",L"122312",L"132212",L"221213", // 0-9
            L"221312",L"231212",L"112232",L"122132",L"122231",L"113222",L"123122",L"123221",L"223211",L"221132", //10-19
            L"221231",L"213212",L"223112",L"312131",L"311222",L"321122",L"321221",L"312212",L"322112",L"322211", //20-29
            L"212123",L"212321",L"232121",L"111323",L"131123",L"131321",L"112313",L"132113",L"132311",L"211313", //30-39
            L"231113",L"231311",L"112133",L"112331",L"132131",L"113123",L"113321",L"133121",L"313121",L"211331", //40-49
            L"231131",L"213113",L"213311",L"213131",L"311123",L"311321",L"331121",L"312113",L"312311",L"332111", //50-59
            L"314111",L"221411",L"431111",L"111224",L"111422",L"121124",L"121421",L"141122",L"141221",L"112214", //60-69
            L"112412",L"122114",L"122411",L"142112",L"142211",L"241211",L"221114",L"413111",L"241112",L"134111", //70-79
            L"111242",L"121142",L"121241",L"114212",L"124112",L"124211",L"411212",L"421112",L"421211",L"212141", //80-89
            L"214121",L"412121",L"111143",L"111341",L"131141",L"114113",L"114311",L"411113",L"411311",L"113141", //90-99
            L"114131",L"311141",L"411131",L"211412",L"211214",L"211232",L"2331112"                               //100-106(stop)
        };
        return T[v];
    }
    // Построить последовательность модулей (1=штрих,0=пробел) для Code B
    inline std::vector<int> buildModules(const std::string& payload) {
        std::vector<int> values;
        values.push_back(104); // Start B
        long long sum = 104;
        size_t pos = 1;
        for (unsigned char c : payload) {
            int v = (c >= 32 && c <= 126) ? (c - 32) : ('?' - 32);
            values.push_back(v);
            sum += static_cast<long long>(v) * pos++;
        }
        values.push_back(static_cast<int>(sum % 103)); // checksum
        values.push_back(106); // stop
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
    inline size_t modulesFor(size_t chars) { return (chars + 3) * 11 + 2; } // start+check+stop + quiet
}

// =============================================================================
// ПРИНТЕР
// =============================================================================
class ReceiptPrinter {
public:
    // Главная функция: печатает чек. Возвращает true при успехе.
    static bool print(const ReceiptData& data) {
        g_logger.info(L"ReceiptPrinter::print: started, appendix=" +
            std::to_wstring(data.appendixNumber) + L", client=" + std::to_wstring(data.clientId) +
            L", items=" + std::to_wstring(data.items.size()));
        // =====================================================================
        // PNG-превью рисуется ВСЕГДА — детерминированный контроль
        // качества чека, независимый от установленных принтеров.
        // =====================================================================
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

        // =====================================================================
        // ЗАЩИТА ОТ DEADLOCK / ЗАВИСАНИЯ UI.
        // Microsoft Print to PDF / XPS / OneNote и любые принтеры с портом
        // PORTPROMPT: или FILE: требуют интерактивного диалога.
        // Печать на такой принтер из UI-потока = зависание.
        // Поэтому spool-печать пропускаем, контроль качества — через PNG.
        // =====================================================================
        if (isPromptPort(printer)) {
            g_logger.warning(L"ReceiptPrinter: printer '" + printer +
                L"' is interactive (PORTPROMPT:/FILE: or known virtual). "
                L"Spooler print SKIPPED to avoid UI deadlock; inspect PNG preview: " + png);
            return true; // считаем успехом — PNG уже есть
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
            g_logger.info(L"ReceiptPrinter: SetAbortProc installed, spool message pumping enabled");
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

    // =====================================================================
    // ДЕТЕКЦИЯ ИНТЕРАКТИВНОГО ПРИНТЕРА.
    // Исправление зависания: раньше проверялся только PORTPROMPT:.
    // В реальных логах Microsoft Print to PDF возвращает порт FILE:.
    // Теперь распознаём оба варианта + известные виртуальные принтеры по имени.
    // =====================================================================
    static bool isPromptPort(const std::wstring& printer) {
        // Быстрая проверка по имени (без открытия принтера)
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

                // Главное исправление зависания:
                // PORTPROMPT:  — классический диалог
                // FILE:        — именно этот порт возвращает Microsoft Print to PDF
                if (_wcsicmp(port.c_str(), L"PORTPROMPT:") == 0 ||
                    _wcsicmp(port.c_str(), L"FILE:") == 0) {
                    prompt = true;
                }
            }
        }
        ClosePrinter(hPrinter);
        return prompt;
    }

    // =====================================================================
    // PNG-ПРЕВЬЮ ЧЕКА (контроль качества БЕЗ принтера).
    // Тот же renderDocument рисуется в EMF, затем GDI+ сохраняет PNG в
    // %LOCALAPPDATA%\TerminalKiosk\receipts\appendix_<N>.png
    // =====================================================================
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

        // =====================================================================
        // ИСПРАВЛЕНИЕ НЕВИДИМОГО ШТРИХ-КОДА: ранее превью шло через DISPLAY DC
        // (96 DPI), где 5 см = 190 px и 1003 модуля вырождались в module<1px.
        // Теперь превью рисуется в растр 600 DPI (A4 альбомная): 5 см = 1180 px,
        // module ≈ 1,18 px — штрих-код ВИДИМ и сохраняет физический размер 5×2 см.
        // =====================================================================
        int dpi = 600;
        int pxW = (int)std::round(297.0 / 25.4 * dpi);   // 7016
        int pxH = (int)std::round(210.0 / 25.4 * dpi);   // 4961
        g_logger.info(L"ReceiptPrinter: preview DIB " + std::to_wstring(pxW) + L"x" +
            std::to_wstring(pxH) + L" @ " + std::to_wstring(dpi) + L" DPI");

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = pxW;
        bmi.bmiHeader.biHeight = -pxH;                 // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);
        // Исправление C6387: передаем валидный указатель для получения адреса памяти DIB
        void* pBits = nullptr;
        HBITMAP hBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);

        if (!hBmp) {
            // Фолбэк 300 DPI, если под 600 DPI не хватает памяти (~139 МБ)
            dpi = 300;
            pxW = (int)std::round(297.0 / 25.4 * dpi);
            pxH = (int)std::round(210.0 / 25.4 * dpi);
            bmi.bmiHeader.biWidth = pxW;
            bmi.bmiHeader.biHeight = -pxH;

            // Исправление C6387 для fallback-вызова
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
            // Без выбранного DIB рисование шло бы в DC 1x1/экрана — брак гарантирован.
            g_logger.error(L"ReceiptPrinter: SelectObject(memDC, hBmp) FAILED, err=" +
                std::to_wstring(GetLastError()));
            DeleteObject(hBmp);
            DeleteDC(memDC);
            ReleaseDC(nullptr, screenDC);
            return result;
        }
        g_logger.info(L"ReceiptPrinter: DIB selected into memDC, HORZRES=" +
            std::to_wstring(GetDeviceCaps(memDC, HORZRES)) + L", VERTRES=" +
            std::to_wstring(GetDeviceCaps(memDC, VERTRES)));

        // Белая подложка страницы
        HBRUSH hWhite = CreateSolidBrush(RGB(255, 255, 255));
        RECT rc{ 0, 0, pxW, pxH };
        FillRect(memDC, &rc, hWhite);
        DeleteObject(hWhite);

        // Отрисовка чека с явным DPI превью (mm→px и шрифты масштабируются)
        renderDocument(memDC, d, false, dpi, dpi);

        // Сохранение PNG через GDI+ с метаданными 600 DPI
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

    // =====================================================================
    // АБОРТ-ПРОЦЕДУРА GDI С НАКАЧКОЙ СООБЩЕНИЙ (устранение deadlock).
    // Спулер вызывает её многократно во время StartDoc/StartPage/EndPage/
    // EndDoc. Без неё диалог «Сохранить как» создаётся на нашем потоке
    // и никогда не получает сообщений → взаимная блокировка.
    // =====================================================================
    static BOOL CALLBACK AbortProc(HDC hdc, int nCode) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return TRUE; // продолжаем печать
    }

    // ---- Автоопределение принтера (требование задачи) ----
    static std::wstring selectPrinter() {
        // Явно заданный принтер (контроль качества / продакшн-назначение)
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

    // ---- DC с альбомной ориентацией (как в образце чека) ----
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

    // =====================================================================
 // НОВОЕ: ЕДИНАЯ полезная нагрузка на ВСЕ товары (без разбиения на строки).
 // Состав по требованию: № приложения, id, ФИО (транслит — Code 128 не
 // содержит кириллицы), наименование*кол-во каждой вещи, SUM, PAY.
 // =====================================================================
    static std::string buildPayload(const ReceiptData& d) {
        std::ostringstream ss;
        ss << "APP=" << d.appendixNumber
            << ";ID=" << d.clientId
            << ";FIO=" << ReceiptUtils::transliterate(d.clientFullName)
            << ";ITEMS=";
        for (size_t i = 0; i < d.items.size(); ++i) {
            if (i) ss << ",";
            ss << ReceiptUtils::transliterate(d.items[i].description)
                << "*" << d.items[i].quantity;
        }
        ss << ";SUM=" << static_cast<long long>(std::llround(d.totalValue))
            << ";PAY=" << d.totalClientAmount;
        return ss.str();
    }
    // =====================================================================
    // НОВОЕ: РУССКАЯ подпись под штрих-кодом (для человека), по центру.
    // =====================================================================
    static std::wstring buildRussianCaption(const ReceiptData& d) {
        std::wstring s = L"Приложение № " + std::to_wstring(d.appendixNumber) +
            L"; комитент: " + d.clientFullName + L"; состав: ";
        for (size_t i = 0; i < d.items.size(); ++i) {
            if (i) s += L", ";
            s += d.items[i].description + L" — " + std::to_wstring(d.items[i].quantity) + L" шт";
        }
        s += L"; сумма: " + ReceiptUtils::formatMoney(d.totalValue) +
            L"; выплата комитенту: " + ReceiptUtils::formatMoney(d.totalClientAmount);
        return s;
    }
    // Перенос длинной подписи на строки (не шире maxLen символов)
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
        int maxHeightPx, const std::string& payload)
    {
        auto modules = Code128::buildModules(payload);
        g_logger.info(L"ReceiptPrinter: drawBarcodeLine: payload='" +
            std::wstring(payload.begin(), payload.end()) + L"', modules=" +
            std::to_wstring(modules.size()));
        if (modules.empty() || areaRight <= areaLeft) return;
        const int n = static_cast<int>(modules.size());
        const int areaW = areaRight - areaLeft;
        double module = static_cast<double>(areaW) / static_cast<double>(n);
        // ВОССТАНОВЛЕНО: потолок 3.0 (было 6.0 — давал «жирную» полосу на всю страницу)
        if (module > 3.0) module = 3.0;
        const int actualW = static_cast<int>(std::round(module * n));
        const int startX = areaLeft + (areaW - actualW) / 2;   // строгое центрирование
        g_logger.info(L"ReceiptPrinter: drawBarcodeLine: n=" + std::to_wstring(n) +
            L", areaW=" + std::to_wstring(areaW) +
            L", module=" + std::to_wstring(module) +
            L", actualW=" + std::to_wstring(actualW) +
            L", startX=" + std::to_wstring(startX));

        // ---------------------------------------------------------------------
        // ПУТЬ А: module >= 1.5 px — прямая векторная отрисовка с дробным модулем
        // (как в рабочей версии; ценники попадают сюда и НЕ меняются).
        // ---------------------------------------------------------------------
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

        // ---------------------------------------------------------------------
        // ПУТЬ Б: module < 1.5 px (приложение: 1168 модулей в 1180 px → 1.01 px).
        // СУПЕРСЭМПЛИНГ x4 + HALFTONE: гребёнка рисуется во временный DIB с
        // module*4 (≈4 px — штрихи целостные), затем с противоналожением (анти‑
        // алиасинг) сжимается ровно в те же 5x2 см. Результат: размер строго
        // 5x2 см по центру, и НИКАКИХ «отдельных полос» при любом масштабе
        // просмотра/печати (ранее бинарные 1 px штрихи деградировали при даунскейле).
        // ---------------------------------------------------------------------
        const int SS = 4;
        const int sw = actualW * SS;
        const int sh = maxHeightPx * SS;
        g_logger.info(L"ReceiptPrinter: drawBarcodeLine: supersampling x" +
            std::to_wstring(SS) + L" (" + std::to_wstring(sw) + L"x" +
            std::to_wstring(sh) + L") for module<1.5px");
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = sw;
        bmi.bmiHeader.biHeight = -sh;              // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        HDC screenDC = GetDC(nullptr);
        HDC tmpDC = CreateCompatibleDC(screenDC);
        void* pBits = nullptr;
        HBITMAP hTmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (!hTmp) {
            // Деградация gracefully: временный DIB недоступен — рисуем напрямую
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
        // Гребёнка с модулем *4 (целостные штрихи 4+ px)
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
        // Антиалиасинг-сжатие в те же 5x2 см целевого DC
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
    // =====================================================================
    // ЯВНАЯ БЕЛАЯ ЗАЛИВКА СТРАНИЦЫ + АТРИБУТЫ ПОСЛЕ StartPage.
    // StartPage сбрасывает атрибуты DC, а непрокрашенный фон метафайла
    // отображается конвертером (Print to PDF / GDI+) как серый. Поэтому:
    // 1) заливаем всю страницу белым;
    // 2) SetBkColor = белый;
    // 3) SetBkMode = TRANSPARENT;
    // 4) SetTextColor = чёрный.
    // =====================================================================
    static void beginPage(HDC hdc, int pageW, int pageH) {
        HBRUSH hWhite = CreateSolidBrush(RGB(255, 255, 255));
        RECT rc{ 0, 0, pageW, pageH };
        FillRect(hdc, &rc, hWhite);
        DeleteObject(hWhite);
        SetBkColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0));
    }

    // ---- Отрисовка документа (с пагинацией таблицы) ----
    static bool renderDocument(HDC hdc, const ReceiptData& d, bool spool, 
        int dpiXOverride = 0, int dpiYOverride = 0) {
        // НОВОЕ: для превью DPI задаётся явно (600), для принтера — из DC.
        int dpiX = (dpiXOverride > 0) ? dpiXOverride : GetDeviceCaps(hdc, LOGPIXELSX);
        int dpiY = (dpiYOverride > 0) ? dpiYOverride : GetDeviceCaps(hdc, LOGPIXELSY);
        // =====================================================================
        // ИСПРАВЛЕНИЕ НАСЛОЕНИЯ И ПРОПАЖИ ШТРИХ-КОДА.
        // В превью HORZRES/VERTRES DC не соответствовали растру DIB (1920x1080
        // вместо 7016x4961) → ложная пагинация: beginPage() затирал штрих-код,
        // блоки рисовались поверх друг друга. Теперь в превью размер страницы
        // вычисляется СТРОГО из явного DPI (A4 альбомная 297x210 мм),
        // для принтера — как прежде, из DC (бумага может отличаться).
        // =====================================================================
        int pageW, pageH;
        if (dpiXOverride > 0) {
            pageW = static_cast<int>(std::round(297.0 / 25.4 * dpiX));  // 7016 @600
            pageH = static_cast<int>(std::round(210.0 / 25.4 * dpiY));  // 4961 @600
        }
        else {
            pageW = GetDeviceCaps(hdc, HORZRES);
            pageH = GetDeviceCaps(hdc, VERTRES);
        }
        g_logger.info(L"ReceiptPrinter: renderDocument dpi=" + std::to_wstring(dpiX) +
            L", pageW=" + std::to_wstring(pageW) + L", pageH=" + std::to_wstring(pageH) +
            L", DC HORZRES=" + std::to_wstring(GetDeviceCaps(hdc, HORZRES)) +
            L", DC VERTRES=" + std::to_wstring(GetDeviceCaps(hdc, VERTRES)));
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

        // Колонки таблицы (проценты ширины, как в образце)
        const int colPct[6] = { 6, 32, 20, 8, 15, 19 };
        const wchar_t* colNames[6] = { L"№", L"Товар", L"Характеристика", L"кол-во", L"цена", L"на руки" };

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

        // ---- Заголовок ----
        HFONT old = (HFONT)SelectObject(hdc, fTitle);
        std::wstring title = std::wstring(ReceiptConfig::CONTRACT_TITLE) + L" " + ReceiptConfig::STORE_NAME;
        TextOutW(hdc, left, y, title.c_str(), (int)title.size());
        y += mmY(8);

        // ---- Дата слева, № приложения справа ----
        SelectObject(hdc, fBold);
        std::wstring dateStr = ReceiptUtils::formatDateDDMMYYYY(now);
        TextOutW(hdc, left, y, dateStr.c_str(), (int)dateStr.size());
        std::wstring numStr = L"№ " + std::to_wstring(d.appendixNumber);
        TextOutW(hdc, right - mmX(40), y, numStr.c_str(), (int)numStr.size());

        // ---- Штрих-код строго по центру, ширина НЕ более 5 см, высота НЕ более 2 см ----
        // ВОССТАНОВЛЕНО: «адаптивная ширина» убрана — она растягивала полосу на всю
        // страницу (1168 модулей * 5 px > 5600 px → areaW = maxW). Физический размер
        // снова строго 50x20 мм; читаемость при любом масштабе просмотра обеспечивает
        // суперсэмплинг ВНУТРИ drawBarcodeLine (см. исправление №2), а не расширение области.
        const int barcodeMaxWMm = 50;   // 5 см — жёстко (требование заказчика)
        const int barcodeMaxHMm = 20;   // 2 см — жёстко
        const int areaW = mmX(barcodeMaxWMm);
        const int areaLeft = left + (tableW - areaW) / 2;   // строго центр страницы
        const std::string payload = buildPayload(d);        // состав нагрузки НЕ меняется
        drawBarcodeLine(hdc, areaLeft, areaLeft + areaW, y, mmY(barcodeMaxHMm), payload);
        y += mmY(barcodeMaxHMm) + mmY(2);

        // =====================================================================
        // НОВОЕ: подпись под штрих-кодом НА РУССКОМ, выровнена по центру.
        // (В сам Code 128 кириллица не помещается — туда идёт транслит,
        //  а человек читает русскую подпись.)
        // =====================================================================
        SelectObject(hdc, fSmall);
        SetBkMode(hdc, TRANSPARENT);
        for (const auto& line : wrapText(buildRussianCaption(d), 120)) {
            SIZE sz = {};
            GetTextExtentPoint32W(hdc, line.c_str(), (int)line.size(), &sz);
            TextOutW(hdc, left + (tableW - sz.cx) / 2, y, line.c_str(), (int)line.size());
            y += sz.cy + 2;
        }
        y += mmY(2);

        // ---- Реквизиты магазина ----
        SelectObject(hdc, fSmall);
        for (auto& s : ReceiptConfig::STORE_REQUISITES) {
            TextOutW(hdc, left, y, s.c_str(), (int)s.size());
            y += mmY(4);
        }
        y += mmY(2);

        // ---- ФИО комитента + текст ----
        SelectObject(hdc, fBold);
        std::wstring fio = d.clientFullName;
        TextOutW(hdc, left, y, fio.c_str(), (int)fio.size());
        y += mmY(6);
        SelectObject(hdc, fNormal);
        std::wstring ct = ReceiptConfig::CONSIGNOR_TEXT;
        TextOutW(hdc, left, y, ct.c_str(), (int)ct.size());
        y += mmY(7);

        // ---- Заголовок таблицы ----
        SelectObject(hdc, fBold);
        std::wstring listTitle = L"Перечень товаров";
        TextOutW(hdc, left, y, listTitle.c_str(), (int)listTitle.size());
        y += mmY(6);

        // ---- Таблица с пагинацией ----
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HPEN oldPen = (HPEN)SelectObject(hdc, hPen);
        HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, hNull);
        auto colX = [&](int i) {
            int x = left;
            for (int k = 0; k < i; ++k) x += tableW * colPct[k] / 100;
            return x;
        };
        auto drawTableHeader = [&]() {
            SelectObject(hdc, fBold);
            Rectangle(hdc, left, y, right, y + rowH);
            for (int c = 1; c < 6; ++c) {
                if (c == 1) continue;   // объединённая ячейка «Общее количество» (0+1)
                MoveToEx(hdc, colX(c), y, nullptr); LineTo(hdc, colX(c), y + rowH);
            }
            SelectObject(hdc, fBold);
            std::wstring t0 = L"Общее количество", t2 = L"Сумма на продажу", t4 = L"Сумма на руки";
            TextOutW(hdc, colX(0) + 2, y + 2, t0.c_str(), (int)t0.size());
            TextOutW(hdc, colX(2) + 2, y + 2, t2.c_str(), (int)t2.size());
            TextOutW(hdc, colX(4) + 2, y + 2, t4.c_str(), (int)t4.size());
            // Замер фактической ширины метки t0 текущим шрифтом (fBold, 9 pt):
            SIZE szT0{};
            GetTextExtentPoint32W(hdc, t0.c_str(), (int)t0.size(), &szT0);
            std::wstring v1 = std::to_wstring(d.totalQty);
            std::wstring v3 = ReceiptUtils::formatMoney(d.totalValue);
            std::wstring v5 = ReceiptUtils::formatMoney(d.totalClientAmount);
            // Значение количества — после метки с отступом 3 мм (внутри объединённой ячейки 0+1,
            // до границы colX(2) запас ≈ 50 мм — пересечение с «Сумма на продажу» исключено)
            TextOutW(hdc, colX(0) + 2 + szT0.cx + mmX(3), y + 2, v1.c_str(), (int)v1.size());
            TextOutW(hdc, colX(3) + 2, y + 2, v3.c_str(), (int)v3.size());   // без изменений
            TextOutW(hdc, colX(5) + 2, y + 2, v5.c_str(), (int)v5.size());   // без изменений
            g_logger.info(L"ReceiptPrinter: totals row rendered: labelW=" + std::to_wstring(szT0.cx) +
                L" px, qty=" + v1 + L", sale=" + v3 + L", client=" + v5);
            y += rowH + mmY(3);
        };

        drawTableHeader();
        SelectObject(hdc, fNormal);
        for (size_t i = 0; i < d.items.size(); ++i) {
            if (y + rowH * 3 > pageH - mmY(8)) {
                if (spool) {
                    // Реальная печать: новая физическая страница.
                    EndPage(hdc);
                    StartPage(hdc);
                    beginPage(hdc, pageW, pageH);
                    y = mmY(8);
                    drawTableHeader();
                    SelectObject(hdc, fNormal);
                }
                else {
                    // =============================================================
                    // ИСПРАВЛЕНИЕ: в превью ЗАПРЕЩЕНО повторно вызывать beginPage() —
                    // FillRect на всю страницу стирает уже отрисованное (штрих-код,
                    // заголовок) и даёт «наслоение». При нехватке места продолжаем
                    // рисовать ниже без сброса (растр 4961 px имеет запас); хвост,
                    // если не поместится, будет обрезан GDI — это видно в логе.
                    // =============================================================
                    g_logger.warning(L"ReceiptPrinter: preview overflow at y=" +
                        std::to_wstring(y) + L" (pageH=" + std::to_wstring(pageH) +
                        L") — продолжаем без сброса страницы");
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
        // ---- Итоговая строка ----
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

        // ---- Текст под таблицей ----
        SelectObject(hdc, fNormal);
        for (auto& s : ReceiptConfig::NOTE_TEXT) {
            TextOutW(hdc, left, y, s.c_str(), (int)s.size());
            y += mmY(5);
        }
        y += mmY(2);

        // ---- Срок действия ----
        SelectObject(hdc, fBig);
        std::wstring valid = L"Приложение действительно до  " +
            ReceiptUtils::formatDateDDMMYYYY(validUntil);
        TextOutW(hdc, left, y, valid.c_str(), (int)valid.size());
        y += mmY(8);

        // ---- Режим работы ----
        SelectObject(hdc, fBold);
        for (auto& s : ReceiptConfig::STORE_FOOTER) {
            TextOutW(hdc, left, y, s.c_str(), (int)s.size());
            y += mmY(5);
        }
        y += mmY(3);

        // ---- Нижняя таблица: Комиссионер | Комитент ----
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