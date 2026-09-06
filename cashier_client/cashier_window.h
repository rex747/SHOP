// cashier_window.h
// =============================================================================
// МОДУЛЬ «КАССИР» ДЛЯ POS АТОЛ С WINDOWS IoT
// =============================================================================
//
// НАЗНАЧЕНИЕ:
// Клиентская часть кассира комиссионного магазина «Советский».
// Работает на POS-моноблоке АТОЛ под управлением Windows IoT.
//
// ФУНКЦИОНАЛ:
// 1. Авторизация кассира (телефон + пароль, как у товароведа).
// 2. Поиск комитента по ID или ФИО (эндпоинт /api/v1/clients/search).
// 3. Выбор приложения к договору и товаров из него.
// 4. Печать ценников (переиспользование PriceTagPrinter::printForReceipt).
// 5. Печать приложения к договору (переиспользование ReceiptPrinter::print).
// 6. Формирование и печать накладных:
//    - Накладная о возврате товара
//    - Расписка о получении товара
//    - Накладная об утрате товара
//    - Накладная о выплате вознаграждения за реализованный товар
// 7. Выплата компенсации за утраченный товар через сканирование
//    штрих-кода сканером АТОЛ-77 (режим HID-эмуляции клавиатуры).
// 8. Формирование списка нереализованных товаров со сроком > 15 суток.
// 9. Пометка товаров как «возвращён» или «возмещён ущерб» на сервере.
//
// АРХИТЕКТУРА:
// - Все сетевые запросы выполняются в фоновых потоках.
// - Результаты доставляются в UI-поток через PostMessageW.
// - Общие данные защищены мьютексами.
// - Сканер штрих-кода АТОЛ-77 работает в режиме HID-эмуляции
//   клавиатуры: данные приходят через поле ввода, завершение
//   сканирования определяется по символу Enter (CR).
//
// ИСПОЛЬЗУЕМЫЕ СЕРВЕРНЫЕ ЭНДПОИНТЫ:
// - GET  /api/v1/clients/search?id=... или ?last_name=...&first_name=...&middle_name=...
// - GET  /api/v1/cashier/appendices?client_id=...
// - GET  /api/v1/cashier/appendix_items?appendix_id=...&client_id=...
// - POST /api/v1/cashier/mark_returned
// - POST /api/v1/cashier/mark_compensated
// - GET  /api/v1/cashier/sold_items?client_id=...
// - GET  /api/v1/cashier/unsold_items?client_id=...
// - POST /api/v1/cashier/create_document
//
// =============================================================================
#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <nlohmann/json.hpp>

#include "config.h"
#include "logger.h"
#include "https_client.h"
#include "auth_manager.h"
#include "string_utils.h"
#include "receipt_printer.h"
#include "price_tag_printer.h"

#pragma comment(lib, "comctl32.lib")

extern Logger g_logger;
extern HTTPSClient g_httpsClient;
extern AuthManager g_authManager;
extern HINSTANCE g_hInstance;

using json = nlohmann::json;

// =============================================================================
// ИДЕНТИФИКАТОРЫ ЭЛЕМЕНТОВ УПРАВЛЕНИЯ
// =============================================================================
#define IDC_CASH_ID_EDIT            8101
#define IDC_CASH_LASTNAME_EDIT      8102
#define IDC_CASH_FIRSTNAME_EDIT     8103
#define IDC_CASH_MIDDLENAME_EDIT    8104
#define IDC_CASH_SEARCH_BTN         8105
#define IDC_CASH_CLIENT_INFO        8106
#define IDC_CASH_APPENDIX_COMBO     8107
#define IDC_CASH_ITEMS_LIST         8108
#define IDC_CASH_BARCODE_EDIT       8109
#define IDC_CASH_PRINT_TAG_BTN      8110
#define IDC_CASH_PRINT_APP_BTN      8111
#define IDC_CASH_RETURN_BTN         8112
#define IDC_CASH_RECEIPT_BTN        8113
#define IDC_CASH_LOST_BTN           8114
#define IDC_CASH_PAYMENT_BTN        8115
#define IDC_CASH_OVERDUE_BTN        8116
#define IDC_CASH_SCAN_BTN           8117
#define IDC_CASH_LOGOUT_BTN         8118
#define IDC_CASH_STATUS_LABEL       8119
#define IDC_CASH_RESULT_EDIT        8120
#define IDC_CASH_SELECT_ALL_BTN     8121
#define IDC_CASH_DESELECT_ALL_BTN   8122

// Сообщения для асинхронного обновления UI
#define WM_CASHIER_SEARCH_RESULT    (WM_APP + 300)
#define WM_CASHIER_APPENDICES_LOADED (WM_APP + 301)
#define WM_CASHIER_ITEMS_LOADED     (WM_APP + 302)
#define WM_CASHIER_OP_COMPLETE      (WM_APP + 303)
#define WM_CASHIER_SOLD_LOADED      (WM_APP + 304)
#define WM_CASHIER_OVERDUE_LOADED   (WM_APP + 305)
#define WM_CASHIER_ERROR            (WM_APP + 306)
#define WM_CASHIER_SCAN_COMPLETE    (WM_APP + 307)

// =============================================================================
// КОНСТАНТЫ
// =============================================================================
namespace CashierConfig {
    constexpr COLORREF HEADER_COLOR = RGB(45, 45, 48);
    constexpr COLORREF PRIMARY_COLOR = RGB(0, 122, 204);
    constexpr COLORREF SUCCESS_COLOR = RGB(76, 175, 80);
    constexpr COLORREF DANGER_COLOR = RGB(211, 47, 47);
    constexpr COLORREF WARNING_COLOR = RGB(255, 152, 0);
    constexpr COLORREF BACKGROUND_COLOR = RGB(245, 245, 245);
    constexpr int BTN_HEIGHT = 50;
    constexpr int BTN_WIDTH = 200;
    constexpr int MARGIN = 15;
    // Максимальная длина штрих-кода от сканера АТОЛ-77
    constexpr int MAX_SCAN_LENGTH = 2048;
}

// =============================================================================
// КЛАСС ПЕЧАТИ ДОКУМЕНТОВ КАССИРА
// =============================================================================
//
// Печатает накладные, расписки и другие документы на принтере.
// Использует тот же механизм печати, что и ReceiptPrinter
// (GDI + спулер), но с другим форматом документа.
//
// Формат документов соответствует требованиям задания:
// - Шапка: КОМИССИОННЫЙ МАГАЗИН СОВЕТСКИЙ, ООО «Советский» ИНН
// - Номер документа (порядковый для каждого комитента)
// - Дата составления
// - Основание: заявление комитента (ФИО, паспорт)
// - Таблица товаров
// - Итого
// - Подписи (для расписки и накладной об утрате)
//
// ПОТОКОБЕЗОПАСНОСТЬ:
// Метод printDocument вызывается из UI-потока после получения
// номера документа с сервера. Все данные передаются по значению.
// =============================================================================
class CashierDocumentPrinter {
public:
    // Тип документа
    enum class DocType {
        RETURN,     // Накладная о возврате товара
        RECEIPT,    // Расписка о получении товара
        LOSS,       // Накладная об утрате товара
        REWARD      // Накладная о выплате вознаграждения
    };

    /**
     * @brief Печатает документ кассира.
     *
     * @param docType тип документа
     * @param docNumber порядковый номер документа
     * @param clientFullName ФИО комитента
     * @param passportSeries серия паспорта
     * @param passportNumber номер паспорта
     * @param items вектор товаров для таблицы (ReceiptItem из receipt_printer.h)
     * @param totalQty итоговое количество
     * @param totalValue итоговая сумма
     * @return true при успешной печати
     */
    static bool printDocument(
        DocType docType,
        int docNumber,
        const std::wstring& clientFullName,
        const std::wstring& passportSeries,
        const std::wstring& passportNumber,
        const std::vector<ReceiptItem>& items,
        int totalQty,
        double totalValue) {

        g_logger.info(L"CashierDocumentPrinter::printDocument: started, type=" +
            std::to_wstring(static_cast<int>(docType)) +
            L", docNumber=" + std::to_wstring(docNumber) +
            L", client=" + clientFullName +
            L", items=" + std::to_wstring(items.size()));

        // --- НОВОЕ: Сохранение PNG-превью ДО проверки принтера ---
        std::wstring pngPath = renderPreviewPng(docType, docNumber,
            clientFullName, passportSeries, passportNumber,
            items, totalQty, totalValue);
        if (!pngPath.empty()) {
            g_logger.info(L"CashierDocumentPrinter: PNG preview saved: " + pngPath);
        }
        else {
            g_logger.warning(L"CashierDocumentPrinter: PNG preview was NOT created");
        }

        // Выбираем принтер (переиспользуем метод из ReceiptPrinter)
        std::wstring printer = ReceiptPrinter::selectPrinter();
        if (printer.empty()) {
            g_logger.error(L"CashierDocumentPrinter: no printer available");
            return false;
        }
        g_logger.info(L"CashierDocumentPrinter: selected printer=" + printer);

        // Проверяем интерактивный принтер (переиспользуем метод из ReceiptPrinter)
        if (ReceiptPrinter::isPromptPort(printer)) {
            g_logger.warning(L"CashierDocumentPrinter: printer '" + printer +
                L"' is interactive. Spooler print SKIPPED.");
            return true;
        }

        // Создаём контекст устройства (переиспользуем метод из ReceiptPrinter)
        HDC hdc = ReceiptPrinter::createLandscapeDC(printer);
        if (!hdc) {
            g_logger.error(L"CashierDocumentPrinter: CreateDC failed for " + printer);
            return false;
        }

        // Устанавливаем AbortProc (переиспользуем из ReceiptPrinter)
        if (SetAbortProc(hdc, ReceiptPrinter::AbortProc) <= 0) {
            g_logger.warning(L"CashierDocumentPrinter: SetAbortProc failed");
        }

        // Начинаем документ
        DOCINFOW di = {};
        di.cbSize = sizeof(di);
        std::wstring docName = getDocTypeName(docType) + L"_" + std::to_wstring(docNumber);
        di.lpszDocName = docName.c_str();

        if (StartDocW(hdc, &di) <= 0) {
            g_logger.error(L"CashierDocumentPrinter: StartDocW failed, err=" +
                std::to_wstring(GetLastError()));
            DeleteDC(hdc);
            return false;
        }
        g_logger.info(L"CashierDocumentPrinter: StartDocW succeeded");

        // Начинаем страницу
        if (StartPage(hdc) <= 0) {
            g_logger.error(L"CashierDocumentPrinter: StartPage failed");
            DeleteDC(hdc);
            return false;
        }

        // Заполняем страницу белым
        int pageW = GetDeviceCaps(hdc, HORZRES);
        int pageH = GetDeviceCaps(hdc, VERTRES);
        ReceiptPrinter::beginPage(hdc, pageW, pageH);

        // Рендерим документ
        bool ok = renderDocumentPage(hdc, docType, docNumber,
            clientFullName, passportSeries, passportNumber,
            items, totalQty, totalValue);

        // Завершаем страницу и документ
        if (EndPage(hdc) <= 0) {
            g_logger.error(L"CashierDocumentPrinter: EndPage failed");
            ok = false;
        }
        if (EndDoc(hdc) <= 0) {
            g_logger.error(L"CashierDocumentPrinter: EndDoc failed");
            ok = false;
        }

        DeleteDC(hdc);
        g_logger.info(L"CashierDocumentPrinter: finished, ok=" +
            std::wstring(ok ? L"true" : L"false"));
        return ok;
    }

private:
    // Получить имя документа для спулера
    static std::wstring getDocTypeName(DocType type) {
        switch (type) {
        case DocType::RETURN:  return L"Накладная_возврат";
        case DocType::RECEIPT: return L"Расписка";
        case DocType::LOSS:    return L"Накладная_утрата";
        case DocType::REWARD:  return L"Накладная_вознаграждение";
        }
        return L"Документ";
    }

    // Получить заголовок документа
    static std::wstring getDocTitle(DocType type) {
        switch (type) {
        case DocType::RETURN:
            return L"Накладная о возврате товара";
        case DocType::RECEIPT:
            return L"Расписка о получении товара";
        case DocType::LOSS:
            return L"Накладная об утрате товара";
        case DocType::REWARD:
            return L"Накладная о выплате вознаграждения за реализованный товар";
        }
        return L"";
    }

    // Получить текст основания
    static std::wstring getDocBasis(DocType type) {
        switch (type) {
        case DocType::RETURN:
            return L"о возврате товара";
        case DocType::RECEIPT:
            return L"о возврате товара";
        case DocType::LOSS:
            return L"о возврате утраченного товара";
        case DocType::REWARD:
            return L"о выплате вознаграждения за реализованный товар";
        }
        return L"";
    }

    // Рендеринг страницы документа
    static bool renderDocumentPage(
        HDC hdc,
        DocType docType,
        int docNumber,
        const std::wstring& clientFullName,
        const std::wstring& passportSeries,
        const std::wstring& passportNumber,
        const std::vector<ReceiptItem>& items,
        int totalQty,
        double totalValue,
        int dpiXOverride = 0,
        int dpiYOverride = 0) {

        // Используем переданные DPI, если они заданы, иначе берём из HDC
        int dpiX = (dpiXOverride > 0) ? dpiXOverride : GetDeviceCaps(hdc, LOGPIXELSX);
        int dpiY = (dpiYOverride > 0) ? dpiYOverride : GetDeviceCaps(hdc, LOGPIXELSY);
        int pageW = (dpiXOverride > 0) ? static_cast<int>(std::round(297.0 / 25.4 * dpiX))
            : GetDeviceCaps(hdc, HORZRES);
        int pageH = (dpiYOverride > 0) ? static_cast<int>(std::round(210.0 / 25.4 * dpiY))
            : GetDeviceCaps(hdc, VERTRES);


        g_logger.info(L"CashierDocumentPrinter: renderDocumentPage, dpi=" +
            std::to_wstring(dpiX) + L"x" + std::to_wstring(dpiY) +
            L", page=" + std::to_wstring(pageW) + L"x" + std::to_wstring(pageH));

        auto mmX = [&](int mm) { return MulDiv(mm, dpiX, 254) * 10; };
        auto mmY = [&](int mm) { return MulDiv(mm, dpiY, 254) * 10; };
        auto font = [&](int pt, bool bold) {
            return CreateFontW(-MulDiv(pt, dpiY, 72), 0, 0, 0,
                bold ? FW_BOLD : FW_NORMAL,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
            };

        HFONT fTitle = font(14, true);
        HFONT fNormal = font(9, false);
        HFONT fBold = font(9, true);
        HFONT fSmall = font(8, false);

        int left = mmX(10);
        int y = mmY(8);

        // Шапка: КОМИССИОННЫЙ МАГАЗИН СОВЕТСКИЙ
        HFONT old = (HFONT)SelectObject(hdc, fTitle);
        std::wstring header = L"КОМИССИОННЫЙ МАГАЗИН СОВЕТСКИЙ";
        TextOutW(hdc, left, y, header.c_str(), (int)header.size());
        y += mmY(7);

        // ООО «Советский» ИНН
        SelectObject(hdc, fNormal);
        std::wstring inn = L"ООО «Советский» ИНН 770190290229";
        TextOutW(hdc, left, y, inn.c_str(), (int)inn.size());
        y += mmY(8);

        // Название документа и номер
        SelectObject(hdc, fBold);
        std::wstring title = getDocTitle(docType) + L" № " + std::to_wstring(docNumber);
        TextOutW(hdc, left, y, title.c_str(), (int)title.size());
        y += mmY(6);

        // Дата
        SelectObject(hdc, fNormal);
        time_t now = time(nullptr);
        struct tm tm_buf;
        localtime_s(&tm_buf, &now);
        wchar_t dateBuf[32];
        wcsftime(dateBuf, 32, L"%d.%m.%Y", &tm_buf);
        std::wstring dateStr = L"Дата: " + std::wstring(dateBuf);
        TextOutW(hdc, left, y, dateStr.c_str(), (int)dateStr.size());
        y += mmY(6);

        // Основание: заявление комитента (ФИО, паспорт)
        std::wstring basis = L"Основание: заявление комитента " +
            clientFullName +
            L", паспорт серия " + passportSeries +
            L" номер " + passportNumber +
            L" " + getDocBasis(docType) + L".";
        auto basisLines = ReceiptPrinter::wrapText(basis, 100);
        for (const auto& line : basisLines) {
            TextOutW(hdc, left, y, line.c_str(), (int)line.size());
            y += mmY(4);
        }
        y += mmY(4);

        // Таблица товаров
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HPEN oldPen = (HPEN)SelectObject(hdc, hPen);
        HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, hNull);

        int right = pageW - mmX(10);
        int tableW = right - left;
        int rowH = mmY(6);

        // Ширины колонок: Наименование(35%) | Ед.(8%) | Кол-во(10%) | Состояние(22%) | Цена(12%) | Сумма(13%)
        const int colPct[6] = { 35, 8, 10, 22, 12, 13 };
        auto colX = [&](int i) {
            int x = left;
            for (int k = 0; k < i; ++k)
                x += tableW * colPct[k] / 100;
            return x;
            };

        // Заголовок таблицы
        SelectObject(hdc, fBold);
        Rectangle(hdc, left, y, right, y + rowH);
        for (int c = 1; c < 6; ++c) {
            MoveToEx(hdc, colX(c), y, nullptr);
            LineTo(hdc, colX(c), y + rowH);
        }
        TextOutW(hdc, colX(0) + 2, y + 2, L"Наименование / артикул", (int)wcslen(L"Наименование / артикул"));
        TextOutW(hdc, colX(1) + 2, y + 2, L"Ед.", (int)wcslen(L"Ед."));
        TextOutW(hdc, colX(2) + 2, y + 2, L"Кол-во", (int)wcslen(L"Кол-во"));
        TextOutW(hdc, colX(3) + 2, y + 2, L"Состояние", (int)wcslen(L"Состояние"));
        TextOutW(hdc, colX(4) + 2, y + 2, L"Цена", (int)wcslen(L"Цена"));
        TextOutW(hdc, colX(5) + 2, y + 2, L"Сумма", (int)wcslen(L"Сумма"));
        y += rowH;

        // Строки товаров
        SelectObject(hdc, fSmall);
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];

            // Проверка переполнения страницы
            if (y + rowH * 3 > pageH - mmY(10)) {
                EndPage(hdc);
                StartPage(hdc);
                ReceiptPrinter::beginPage(hdc, pageW, pageH);
                y = mmY(8);
                // Перерисовка заголовка таблицы
                SelectObject(hdc, fBold);
                Rectangle(hdc, left, y, right, y + rowH);
                for (int c = 1; c < 6; ++c) {
                    MoveToEx(hdc, colX(c), y, nullptr);
                    LineTo(hdc, colX(c), y + rowH);
                }
                TextOutW(hdc, colX(0) + 2, y + 2, L"Наименование / артикул", (int)wcslen(L"Наименование / артикул"));
                TextOutW(hdc, colX(1) + 2, y + 2, L"Ед.", (int)wcslen(L"Ед."));
                TextOutW(hdc, colX(2) + 2, y + 2, L"Кол-во", (int)wcslen(L"Кол-во"));
                TextOutW(hdc, colX(3) + 2, y + 2, L"Состояние", (int)wcslen(L"Состояние"));
                TextOutW(hdc, colX(4) + 2, y + 2, L"Цена", (int)wcslen(L"Цена"));
                TextOutW(hdc, colX(5) + 2, y + 2, L"Сумма", (int)wcslen(L"Сумма"));
                y += rowH;
                SelectObject(hdc, fSmall);
            }

            Rectangle(hdc, left, y, right, y + rowH);
            for (int c = 1; c < 6; ++c) {
                MoveToEx(hdc, colX(c), y, nullptr);
                LineTo(hdc, colX(c), y + rowH);
            }

            std::wstring cells[6] = {
                item.description,
                L"шт.",
                std::to_wstring(item.quantity),
                item.characteristic.empty() ? item.note : item.characteristic,
                ReceiptUtils::formatMoney(item.price),
                ReceiptUtils::formatMoney(item.price * item.quantity)
            };
            for (int c = 0; c < 6; ++c) {
                TextOutW(hdc, colX(c) + 2, y + 2, cells[c].c_str(), (int)cells[c].size());
            }
            y += rowH;
        }
                
        // Итого
        SelectObject(hdc, fBold);
        Rectangle(hdc, left, y, right, y + rowH);
        for (int c = 1; c < 6; ++c) {
            MoveToEx(hdc, colX(c), y, nullptr);
            LineTo(hdc, colX(c), y + rowH);
        }
        TextOutW(hdc, colX(0) + 2, y + 2, L"Итого:", (int)wcslen(L"Итого:"));
        TextOutW(hdc, colX(1) + 2, y + 2, L"шт.", (int)wcslen(L"шт."));
        TextOutW(hdc, colX(2) + 2, y + 2, std::to_wstring(totalQty).c_str(),
            (int)std::to_wstring(totalQty).size());
        TextOutW(hdc, colX(5) + 2, y + 2, ReceiptUtils::formatMoney(totalValue).c_str(),
            (int)ReceiptUtils::formatMoney(totalValue).size());
        y += rowH + mmY(5);

        // Подписи (для расписки и накладной об утрате)
        if (docType == DocType::RECEIPT) {
            TextOutW(hdc, left, y, L"Получил на руки: ____________________",
                (int)wcslen(L"Получил на руки: ____________________"));
            y += mmY(5);
            TextOutW(hdc, left, y, (L"ФИО: " + clientFullName).c_str(),
                (int)(L"ФИО: " + clientFullName).size());
            y += mmY(5);
            TextOutW(hdc, left, y, L"Подпись комитента: ____________________",
                (int)wcslen(L"Подпись комитента: ____________________"));
            y += mmY(5);
        }
        else if (docType == DocType::LOSS) {
            TextOutW(hdc, left, y, L"Получил выплату на руки: ____________________",
                (int)wcslen(L"Получил выплату на руки: ____________________"));
            y += mmY(5);
            TextOutW(hdc, left, y, (L"ФИО: " + clientFullName).c_str(),
                (int)(L"ФИО: " + clientFullName).size());
            y += mmY(5);
            TextOutW(hdc, left, y, L"Подпись комитента: ____________________",
                (int)wcslen(L"Подпись комитента: ____________________"));
            y += mmY(5);
        }

        // Освобождение ресурсов
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBr);
        DeleteObject(hPen);
        DeleteObject(fTitle);
        DeleteObject(fNormal);
        DeleteObject(fBold);
        DeleteObject(fSmall);

        g_logger.info(L"CashierDocumentPrinter: document rendered successfully");
        return true;
    }
    // =========================================================================
    // НОВЫЙ МЕТОД: сохранение PNG-превью документа в файл
    // =========================================================================
    static std::wstring renderPreviewPng(
        DocType docType,
        int docNumber,
        const std::wstring& clientFullName,
        const std::wstring& passportSeries,
        const std::wstring& passportNumber,
        const std::vector<ReceiptItem>& items,
        int totalQty,
        double totalValue) {

        std::wstring result;
        ReceiptPrinter::ensureGdiplus();

        // Используем Config::getAppDataPath() (как в ценниках) для гарантии записи
        std::wstring dir = Config::getAppDataPath();
        CreateDirectoryW(dir.c_str(), nullptr);
        dir += L"\\documents";
        CreateDirectoryW(dir.c_str(), nullptr);

        // Имя файла: тип_номер.png
        std::wstring pngPath = dir + L"\\" + getDocTypeName(docType) +
            L"_" + std::to_wstring(docNumber) + L".png";

        // Размер страницы A4 в пикселях при 300 DPI (для превью)
        int dpi = 300;
        int pxW = static_cast<int>(std::round(297.0 / 25.4 * dpi));
        int pxH = static_cast<int>(std::round(210.0 / 25.4 * dpi));

        g_logger.info(L"CashierDocumentPrinter: renderPreviewPng DIB " +
            std::to_wstring(pxW) + L"x" + std::to_wstring(pxH) +
            L" @ " + std::to_wstring(dpi) + L" DPI, path=" + pngPath);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = pxW;
        bmi.bmiHeader.biHeight = -pxH;          // top-down DIB
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);
        void* pBits = nullptr;
        HBITMAP hBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);

        if (!hBmp) {
            g_logger.error(L"CashierDocumentPrinter: CreateDIBSection failed, err=" +
                std::to_wstring(GetLastError()));
            DeleteDC(memDC);
            ReleaseDC(nullptr, screenDC);
            return result;
        }

        HGDIOBJ hOldBmp = SelectObject(memDC, hBmp);
        if (hOldBmp == nullptr || hOldBmp == HGDI_ERROR) {
            g_logger.error(L"CashierDocumentPrinter: SelectObject(hBmp) failed");
            DeleteObject(hBmp);
            DeleteDC(memDC);
            ReleaseDC(nullptr, screenDC);
            return result;
        }

        // Заливаем белым фоном
        HBRUSH hWhite = CreateSolidBrush(RGB(255, 255, 255));
        RECT rc = { 0, 0, pxW, pxH };
        FillRect(memDC, &rc, hWhite);
        DeleteObject(hWhite);

        // Рендерим документ на DIB (используем существующий renderDocumentPage)
        bool renderOk = renderDocumentPage(memDC, docType, docNumber,
            clientFullName, passportSeries, passportNumber,
            items, totalQty, totalValue, dpi, dpi);   // передаём DPI для масштабирования

        if (!renderOk) {
            g_logger.warning(L"CashierDocumentPrinter: renderDocumentPage returned false for preview");
        }

        // Сохраняем как PNG
        {
            Gdiplus::Bitmap bmp(hBmp, nullptr);
            bmp.SetResolution(static_cast<Gdiplus::REAL>(dpi),
                static_cast<Gdiplus::REAL>(dpi));
            CLSID clsid = {};
            if (ReceiptPrinter::getPngEncoderClsid(clsid)) {
                if (bmp.Save(pngPath.c_str(), &clsid, nullptr) == Gdiplus::Ok) {
                    result = pngPath;
                    g_logger.info(L"CashierDocumentPrinter: PNG saved: " + pngPath);
                }
                else {
                    g_logger.error(L"CashierDocumentPrinter: PNG save failed for " + pngPath);
                }
            }
            else {
                g_logger.error(L"CashierDocumentPrinter: PNG encoder CLSID not found");
            }
        }

        SelectObject(memDC, hOldBmp);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);

        return result;
    }

};

// =============================================================================
// КЛАСС ОКНА КАССИРА
// =============================================================================
class CashierWindow {
private:
    HWND m_hWnd;
    HWND m_hIdEdit, m_hLastNameEdit, m_hFirstNameEdit, m_hMiddleNameEdit;
    HWND m_hSearchBtn, m_hClientInfo, m_hAppendixCombo, m_hItemsList;
    HWND m_hBarcodeEdit;
    HWND m_hPrintTagBtn, m_hPrintAppBtn;
    HWND m_hReturnBtn, m_hReceiptBtn, m_hLostBtn;
    HWND m_hPaymentBtn, m_hOverdueBtn, m_hScanBtn, m_hLogoutBtn;
    HWND m_hStatusLabel, m_hResultEdit;
    HWND m_hSelectAllBtn, m_hDeselectAllBtn;
    HFONT m_hFont, m_hFontBold, m_hFontLarge;

    // Данные текущего комитента
    json m_currentClient;
    // Данные текущего приложения к договору
    json m_currentAppendix;
    // Товары текущего приложения
    std::vector<json> m_items;
    // Реализованные товары (для выплаты вознаграждения)
    std::vector<json> m_soldItems;
    // Просроченные товары (>15 суток)
    std::vector<json> m_overdueItems;
    // Выбранные товары (индексы в m_items)
    std::vector<int> m_selectedItemIds;

    std::vector<json> m_appendices;

    std::mutex m_dataMutex;
    std::atomic<bool> m_isBusy{ false };

    static constexpr const wchar_t* CLASS_NAME = L"CashierWindowClass";

    // =================================================================
    // Создание элементов управления
    // =================================================================
    void createControls() {
        g_logger.info(L"CashierWindow: createControls started");

        RECT rc;
        GetClientRect(m_hWnd, &rc);
        int clientWidth = rc.right - rc.left;
        int centerX = clientWidth / 2;

        m_hFontLarge = CreateFontW(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontBold = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");

        int top = 10;

        // Заголовок
        HWND hTitle = CreateWindowExW(0, L"STATIC",
            L"КАССИР - КОМИССИОННЫЙ МАГАЗИН СОВЕТСКИЙ",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            0, top, clientWidth, 50,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)m_hFontLarge, TRUE);
        top += 60;

        // Блок поиска комитента
        int labelW = 120, editW = 180, gap = 10;
        int startX = CashierConfig::MARGIN;

        CreateWindowExW(0, L"STATIC", L"ID:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            startX, top + 5, labelW, 30, m_hWnd, nullptr, g_hInstance, nullptr);

        m_hIdEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER,
            startX + labelW + 5, top, editW, 35,
            m_hWnd, (HMENU)IDC_CASH_ID_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hIdEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        // Переменная x указывает на позицию после поля ID
        int x = startX + labelW + 5 + editW + gap;

        // ---- Фамилия ----
        CreateWindowExW(0, L"STATIC", L"Фамилия:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            x, top + 5, labelW, 30,
            m_hWnd, nullptr, g_hInstance, nullptr);
        x += labelW + gap;

        m_hLastNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            x, top, editW, 35,
            m_hWnd, (HMENU)IDC_CASH_LASTNAME_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hLastNameEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        x += editW + gap;

        // ---- Имя ----
        CreateWindowExW(0, L"STATIC", L"Имя:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            x, top + 5, 60, 30,   // ширина метки 60
            m_hWnd, nullptr, g_hInstance, nullptr);
        x += 60 + gap;

        m_hFirstNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            x, top, editW, 35,
            m_hWnd, (HMENU)IDC_CASH_FIRSTNAME_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hFirstNameEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        x += editW + gap;

        // ---- Отчество ----
        CreateWindowExW(0, L"STATIC", L"Отчество:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            x, top + 5, 60, 30,   // ширина метки 60
            m_hWnd, nullptr, g_hInstance, nullptr);
        x += 60 + gap;

        m_hMiddleNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            x, top, editW, 35,
            m_hWnd, (HMENU)IDC_CASH_MIDDLENAME_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hMiddleNameEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hSearchBtn = CreateWindowExW(0, L"BUTTON", L"НАЙТИ",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            clientWidth - CashierConfig::BTN_WIDTH - CashierConfig::MARGIN, top,
            CashierConfig::BTN_WIDTH, CashierConfig::BTN_HEIGHT,
            m_hWnd, (HMENU)IDC_CASH_SEARCH_BTN, g_hInstance, nullptr);
        SendMessageW(m_hSearchBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        top += CashierConfig::BTN_HEIGHT + 10;

        // Информация о комитенте
        m_hClientInfo = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            CashierConfig::MARGIN, top, clientWidth - 2 * CashierConfig::MARGIN, 80,
            m_hWnd, (HMENU)IDC_CASH_CLIENT_INFO, g_hInstance, nullptr);
        SendMessageW(m_hClientInfo, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += 90;

        // Выбор приложения к договору
        CreateWindowExW(0, L"STATIC", L"Приложение к договору:",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            CashierConfig::MARGIN, top + 5, 250, 30,
            m_hWnd, nullptr, g_hInstance, nullptr);
        m_hAppendixCombo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            CashierConfig::MARGIN + 260, top, 400, 200,
            m_hWnd, (HMENU)IDC_CASH_APPENDIX_COMBO, g_hInstance, nullptr);
        SendMessageW(m_hAppendixCombo, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += 45;

        // Список товаров с чекбоксами
        m_hItemsList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL,
            CashierConfig::MARGIN, top, clientWidth - 2 * CashierConfig::MARGIN, 250,
            m_hWnd, (HMENU)IDC_CASH_ITEMS_LIST, g_hInstance, nullptr);
        SendMessageW(m_hItemsList, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        ListView_SetExtendedListViewStyle(m_hItemsList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);

        // Колонки списка товаров
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        struct ColumnDef { const wchar_t* text; int width; };
        ColumnDef columns[] = {
            { L"№", 50 },
            { L"Наименование", 250 },
            { L"Цена (руб.)", 100 },
            { L"Кол-во", 70 },
            { L"Не продано", 90 },
            { L"Состояние", 150 },
            { L"Примечание", 150 },
            { L"Статус", 100 }
        };
        for (int i = 0; i < _countof(columns); i++) {
            col.pszText = const_cast<LPWSTR>(columns[i].text);
            col.cx = columns[i].width;
            col.iSubItem = i;
            ListView_InsertColumn(m_hItemsList, i, &col);
        }
        top += 260;

        // Кнопки выбора всех / снятия выбора
        m_hSelectAllBtn = CreateWindowExW(0, L"BUTTON", L"Выбрать все",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            CashierConfig::MARGIN, top, 150, 40,
            m_hWnd, (HMENU)IDC_CASH_SELECT_ALL_BTN, g_hInstance, nullptr);
        SendMessageW(m_hSelectAllBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);

        m_hDeselectAllBtn = CreateWindowExW(0, L"BUTTON", L"Снять все",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            CashierConfig::MARGIN + 160, top, 150, 40,
            m_hWnd, (HMENU)IDC_CASH_DESELECT_ALL_BTN, g_hInstance, nullptr);
        SendMessageW(m_hDeselectAllBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        top += 50;

        // Поле сканера штрих-кода (АТОЛ-77)
        CreateWindowExW(0, L"STATIC", L"Сканер (АТОЛ-77):",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            CashierConfig::MARGIN, top + 5, 200, 30,
            m_hWnd, nullptr, g_hInstance, nullptr);
        m_hBarcodeEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            CashierConfig::MARGIN + 210, top, 500, 35,
            m_hWnd, (HMENU)IDC_CASH_BARCODE_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hBarcodeEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hScanBtn = CreateWindowExW(0, L"BUTTON", L"Сканировать",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            CashierConfig::MARGIN + 720, top, 150, 40,
            m_hWnd, (HMENU)IDC_CASH_SCAN_BTN, g_hInstance, nullptr);
        SendMessageW(m_hScanBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        top += 50;

        // Кнопки операций (первый ряд)
        int btnX = CashierConfig::MARGIN;
        m_hPrintTagBtn = CreateWindowExW(0, L"BUTTON", L"Печать ценников",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            btnX, top, CashierConfig::BTN_WIDTH, CashierConfig::BTN_HEIGHT,
            m_hWnd, (HMENU)IDC_CASH_PRINT_TAG_BTN, g_hInstance, nullptr);
        SendMessageW(m_hPrintTagBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        btnX += CashierConfig::BTN_WIDTH + 10;

        m_hPrintAppBtn = CreateWindowExW(0, L"BUTTON", L"Печать приложения",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            btnX, top, CashierConfig::BTN_WIDTH, CashierConfig::BTN_HEIGHT,
            m_hWnd, (HMENU)IDC_CASH_PRINT_APP_BTN, g_hInstance, nullptr);
        SendMessageW(m_hPrintAppBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        btnX += CashierConfig::BTN_WIDTH + 10;

        m_hReturnBtn = CreateWindowExW(0, L"BUTTON", L"Возврат товара",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            btnX, top, CashierConfig::BTN_WIDTH, CashierConfig::BTN_HEIGHT,
            m_hWnd, (HMENU)IDC_CASH_RETURN_BTN, g_hInstance, nullptr);
        SendMessageW(m_hReturnBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        btnX += CashierConfig::BTN_WIDTH + 10;

        m_hReceiptBtn = CreateWindowExW(0, L"BUTTON", L"Расписка",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            btnX, top, CashierConfig::BTN_WIDTH, CashierConfig::BTN_HEIGHT,
            m_hWnd, (HMENU)IDC_CASH_RECEIPT_BTN, g_hInstance, nullptr);
        SendMessageW(m_hReceiptBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        btnX += CashierConfig::BTN_WIDTH + 10;

        m_hLostBtn = CreateWindowExW(0, L"BUTTON", L"Утрата товара",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            btnX, top, CashierConfig::BTN_WIDTH, CashierConfig::BTN_HEIGHT,
            m_hWnd, (HMENU)IDC_CASH_LOST_BTN, g_hInstance, nullptr);
        SendMessageW(m_hLostBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        top += CashierConfig::BTN_HEIGHT + 10;

        // Кнопки операций (второй ряд)
        btnX = CashierConfig::MARGIN;
        m_hPaymentBtn = CreateWindowExW(0, L"BUTTON", L"Выплата за проданное",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            btnX, top, CashierConfig::BTN_WIDTH + 50, CashierConfig::BTN_HEIGHT,
            m_hWnd, (HMENU)IDC_CASH_PAYMENT_BTN, g_hInstance, nullptr);
        SendMessageW(m_hPaymentBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        btnX += CashierConfig::BTN_WIDTH + 60;

        m_hOverdueBtn = CreateWindowExW(0, L"BUTTON", L"Просрочка >15 дн",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            btnX, top, CashierConfig::BTN_WIDTH, CashierConfig::BTN_HEIGHT,
            m_hWnd, (HMENU)IDC_CASH_OVERDUE_BTN, g_hInstance, nullptr);
        SendMessageW(m_hOverdueBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        btnX += CashierConfig::BTN_WIDTH + 10;

        m_hLogoutBtn = CreateWindowExW(0, L"BUTTON", L"ВЫХОД",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            btnX, top, 150, CashierConfig::BTN_HEIGHT,
            m_hWnd, (HMENU)IDC_CASH_LOGOUT_BTN, g_hInstance, nullptr);
        SendMessageW(m_hLogoutBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        top += CashierConfig::BTN_HEIGHT + 10;

        // Статус
        m_hStatusLabel = CreateWindowExW(0, L"STATIC", L"Готово к работе",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            CashierConfig::MARGIN, top, clientWidth - 2 * CashierConfig::MARGIN, 30,
            m_hWnd, (HMENU)IDC_CASH_STATUS_LABEL, g_hInstance, nullptr);
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += 35;

        // Результаты
        m_hResultEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL | WS_VSCROLL,
            CashierConfig::MARGIN, top, clientWidth - 2 * CashierConfig::MARGIN, 200,
            m_hWnd, (HMENU)IDC_CASH_RESULT_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hResultEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        g_logger.info(L"CashierWindow: all controls created");
    }

    // =================================================================
    // Поиск комитента (использует эндпоинт /api/v1/clients/search)
    // =================================================================
    void onSearch() {
        if (m_isBusy.exchange(true)) {
            g_logger.warning(L"CashierWindow: search already in progress");
            return;
        }

        wchar_t buf[256];
        GetWindowTextW(m_hIdEdit, buf, 256);
        std::wstring idText = buf;
        GetWindowTextW(m_hLastNameEdit, buf, 256);
        std::wstring lastName = buf;
        GetWindowTextW(m_hFirstNameEdit, buf, 256);
        std::wstring firstName = buf;
        GetWindowTextW(m_hMiddleNameEdit, buf, 256);
        std::wstring middleName = buf;

        bool hasId = !idText.empty();
        bool hasFio = !lastName.empty() || !firstName.empty() || !middleName.empty();

        if (!hasId && !hasFio) {
            SetWindowTextW(m_hStatusLabel, L"Введите ID или ФИО");
            m_isBusy = false;
            return;
        }

        SetWindowTextW(m_hStatusLabel, L"Поиск на сервере...");
        g_logger.info(L"CashierWindow: search started, id='" + idText +
            L"', lastName='" + lastName + L"', firstName='" + firstName +
            L"', middleName='" + middleName + L"'");

        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Ошибка авторизации");
            m_isBusy = false;
            return;
        }

        std::wstring path;
        if (hasId) {
            path = L"/api/v1/clients/search?id=" + idText;
        }
        else {
            path = L"/api/v1/clients/search?";
            bool first = true;
            if (!lastName.empty()) { path += L"last_name=" + lastName; first = false; }
            if (!firstName.empty()) { if (!first) path += L"&"; path += L"first_name=" + firstName; first = false; }
            if (!middleName.empty()) { if (!first) path += L"&"; path += L"middle_name=" + middleName; }
        }

        HWND hWndCopy = m_hWnd;
        std::thread([hWndCopy, path, authToken]() {
            g_logger.info(L"CashierWindow: search thread started, path=" + path);
            auto response = g_httpsClient.get(path, authToken);
            json* pResp = new json(response.value_or(json()));
            if (IsWindow(hWndCopy)) {
                PostMessageW(hWndCopy, WM_CASHIER_SEARCH_RESULT, 0, (LPARAM)pResp);
            }
            else {
                delete pResp;
            }
            }).detach();
    }

    // =================================================================
    // Обработка результата поиска
    // =================================================================
    void onSearchResult(json* pResp) {
        m_isBusy = false;
        if (!pResp) return;
        json response = *pResp;
        delete pResp;

        if (!response.contains("clients") || !response["clients"].is_array() || response["clients"].empty()) {
            SetWindowTextW(m_hStatusLabel, L"Комитент не найден");
            return;
        }
        const auto& clients = response["clients"];
        if (clients.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Комитент не найден");
            return;
        }

        std::lock_guard<std::mutex> lock(m_dataMutex);
        m_currentClient = clients[0];

        int clientId = m_currentClient.value("id", 0);
        std::wstring fullName = utf8_to_wstring(m_currentClient.value("full_name", ""));
        std::wstring phone = utf8_to_wstring(m_currentClient.value("phone", ""));
        std::wstring passSeries = utf8_to_wstring(m_currentClient.value("passport_series", ""));
        std::wstring passNumber = utf8_to_wstring(m_currentClient.value("passport_number", ""));
        std::wstring address = utf8_to_wstring(m_currentClient.value("address", ""));

        std::wstring info = L"ID: " + std::to_wstring(clientId) +
            L"\r\nФИО: " + fullName +
            L"\r\nТелефон: " + phone +
            L"\r\nПаспорт: " + passSeries + L" N" + passNumber +
            L"\r\nАдрес: " + address;
        SetWindowTextW(m_hClientInfo, info.c_str());
        SetWindowTextW(m_hStatusLabel, L"Комитент найден. Загрузка приложений...");
        g_logger.info(L"CashierWindow: client found, id=" + std::to_wstring(clientId));

        // Загружаем приложения к договору
        loadAppendices(clientId);
    }

    // =================================================================
    // Загрузка приложений к договору
    // =================================================================
    void loadAppendices(int clientId) {
        std::wstring authToken = g_authManager.getAuthToken();
        std::wstring path = L"/api/v1/cashier/appendices?client_id=" + std::to_wstring(clientId);
        HWND hWndCopy = m_hWnd;

        std::thread([hWndCopy, path, authToken]() {
            g_logger.info(L"CashierWindow: appendices thread started");
            auto response = g_httpsClient.get(path, authToken);
            json* pResp = new json(response.value_or(json()));
            if (IsWindow(hWndCopy)) {
                PostMessageW(hWndCopy, WM_CASHIER_APPENDICES_LOADED, 0, (LPARAM)pResp);
            }
            else {
                delete pResp;
            }
            }).detach();
    }

    // =================================================================
    // Обработка загруженных приложений
    // =================================================================
    void onAppendicesLoaded(json* pResp) {
        if (!pResp) return;
        json response = *pResp;
        delete pResp;

        SendMessageW(m_hAppendixCombo, CB_RESETCONTENT, 0, 0);
        m_appendices.clear();

        if (response.contains("appendices") && response["appendices"].is_array()) {
            const auto& appendices = response["appendices"];
            for (size_t i = 0; i < appendices.size(); i++) {
                const auto& app = appendices[i];
                m_appendices.push_back(app); // Сохраняем объект приложения в вектор

                long long appNumber = app.value("appendix_number", 0LL);
                int totalQty = app.value("total_quantity", 0);
                double totalValue = app.value("total_value", 0.0);
                std::wstring text = L"Приложение N" + std::to_wstring(appNumber) +
                    L" (" + std::to_wstring(totalQty) + L" шт., " +
                    ReceiptUtils::formatMoney(totalValue) + L" руб.)";
                SendMessageW(m_hAppendixCombo, CB_ADDSTRING, 0, (LPARAM)text.c_str());
            }

            if (!m_appendices.empty()) {
                SendMessageW(m_hAppendixCombo, CB_SETCURSEL, 0, 0);
                m_currentAppendix = m_appendices[0];

                // ИСПРАВЛЕНИЕ: Извлекаем ID из JSON-объекта m_currentClient
                int clientId = m_currentClient.value("id", 0);
                long long appendixId = m_appendices[0].value("id", 0LL);

                loadAppendixItems(appendixId, clientId);
            }
        }
        SetWindowTextW(m_hStatusLabel, L"Приложения загружены");
        g_logger.info(L"CashierWindow: appendices loaded, count=" + std::to_wstring(m_appendices.size()));
    }

    // =================================================================
    // Загрузка товаров приложения
    // =================================================================
    void loadAppendixItems(long long appendixId, int clientId) {
        std::wstring authToken = g_authManager.getAuthToken();
        std::wstring path = L"/api/v1/cashier/appendix_items?appendix_id=" +
            std::to_wstring(appendixId) + L"&client_id=" + std::to_wstring(clientId);
        HWND hWndCopy = m_hWnd;

        std::thread([hWndCopy, path, authToken]() {
            g_logger.info(L"CashierWindow: items thread started");
            auto response = g_httpsClient.get(path, authToken);
            json* pResp = new json(response.value_or(json()));
            if (IsWindow(hWndCopy)) {
                PostMessageW(hWndCopy, WM_CASHIER_ITEMS_LOADED, 0, (LPARAM)pResp);
            }
            else {
                delete pResp;
            }
            }).detach();
    }

    // =================================================================
    // Обработка загруженных товаров
    // =================================================================
    void onItemsLoaded(json* pResp) {
        if (!pResp) return;
        json response = *pResp;
        delete pResp;

        std::lock_guard<std::mutex> lock(m_dataMutex);
        m_items.clear();
        ListView_DeleteAllItems(m_hItemsList);

        if (response.contains("items") && response["items"].is_array()) {
            const auto& items = response["items"];
            for (size_t i = 0; i < items.size(); i++) {
                m_items.push_back(items[i]);

                int itemId = items[i].value("id", 0);
                int itemNumber = items[i].value("item_number", 0);
                std::wstring desc = utf8_to_wstring(items[i].value("description", ""));
                double price = items[i].value("estimated_price", 0.0);
                int qty = items[i].value("quantity", 0);
                int unsoldQty = items[i].value("unsold_quantity", 0);
                std::wstring condition = utf8_to_wstring(items[i].value("condition", ""));
                std::wstring note = utf8_to_wstring(items[i].value("note", ""));
                std::wstring status = utf8_to_wstring(items[i].value("status", ""));

                LVITEMW lvi = {};
                lvi.mask = LVIF_TEXT | LVIF_PARAM;
                lvi.iItem = (int)i;
                lvi.iSubItem = 0;
                lvi.lParam = itemId;

                std::wstring numStr = std::to_wstring(itemNumber);
                lvi.pszText = const_cast<LPWSTR>(numStr.c_str());
                ListView_InsertItem(m_hItemsList, &lvi);

                ListView_SetItemText(m_hItemsList, (int)i, 1, const_cast<LPWSTR>(desc.c_str()));
                wchar_t priceBuf[50];
                swprintf_s(priceBuf, L"%.2f", price);
                ListView_SetItemText(m_hItemsList, (int)i, 2, priceBuf);
                std::wstring qtyStr = std::to_wstring(qty);
                ListView_SetItemText(m_hItemsList, (int)i, 3, const_cast<LPWSTR>(qtyStr.c_str()));
                std::wstring unsoldStr = std::to_wstring(unsoldQty);
                ListView_SetItemText(m_hItemsList, (int)i, 4, const_cast<LPWSTR>(unsoldStr.c_str()));
                ListView_SetItemText(m_hItemsList, (int)i, 5, const_cast<LPWSTR>(condition.c_str()));
                ListView_SetItemText(m_hItemsList, (int)i, 6, const_cast<LPWSTR>(note.c_str()));
                ListView_SetItemText(m_hItemsList, (int)i, 7, const_cast<LPWSTR>(status.c_str()));

                // Автоматически выбираем товары со статусом 'pending' или 'expired'
                if (status == L"pending" || status == L"expired") {
                    ListView_SetCheckState(m_hItemsList, (int)i, TRUE);
                }
            }
        }
        SetWindowTextW(m_hStatusLabel,
            (L"Загружено товаров: " + std::to_wstring(m_items.size())).c_str());
        g_logger.info(L"CashierWindow: items loaded, count=" + std::to_wstring(m_items.size()));
    }

    // =================================================================
    // Получение выбранных товаров
    // =================================================================
    std::vector<int> getSelectedItemIds() {
        std::vector<int> selectedIds;
        int itemCount = ListView_GetItemCount(m_hItemsList);
        for (int i = 0; i < itemCount; i++) {
            if (ListView_GetCheckState(m_hItemsList, i)) {
                LVITEMW item = {};
                item.mask = LVIF_PARAM;
                item.iItem = i;
                item.iSubItem = 0;
                ListView_GetItem(m_hItemsList, &item);
                selectedIds.push_back(static_cast<int>(item.lParam));
            }
        }
        return selectedIds;
    }

    // =================================================================
    // Печать ценников (переиспользование PriceTagPrinter::printForReceipt)
    // =================================================================
    void onPrintPriceTags() {
        std::vector<int> selectedIds = getSelectedItemIds();
        if (selectedIds.empty()) {
            MessageBoxW(m_hWnd, L"Выберите товары", L"Внимание", MB_OK);
            return;
        }

        g_logger.info(L"CashierWindow: printing price tags for " +
            std::to_wstring(selectedIds.size()) + L" items");

        // Формируем ReceiptData для переиспользования существующего модуля
        ReceiptData data;
        data.clientId = m_currentClient.value("id", 0);
        data.clientFullName = utf8_to_wstring(m_currentClient.value("full_name", ""));
        data.appendixNumber = m_currentAppendix.value("appendix_number", 0LL);

        std::lock_guard<std::mutex> lock(m_dataMutex);
        for (int itemId : selectedIds) {
            for (const auto& item : m_items) {
                if (item.value("id", 0) == itemId) {
                    ReceiptItem ri;
                    ri.description = utf8_to_wstring(item.value("description", ""));
                    ri.characteristic = utf8_to_wstring(item.value("condition", ""));
                    ri.quantity = item.value("quantity", 1);
                    ri.price = item.value("estimated_price", 0.0);
                    ri.clientAmount = item.value("client_amount", 0.0);
                    ri.note = utf8_to_wstring(item.value("note", ""));
                    data.items.push_back(ri);
                    data.totalQty += ri.quantity;
                    data.totalValue += ri.price * ri.quantity;
                    data.totalClientAmount += ri.clientAmount;
                    break;
                }
            }
        }

        const std::wstring workerFio = g_authManager.getFullName();
        bool ok = PriceTagPrinter::printForReceipt(data, workerFio);
        if (!ok) {
            MessageBoxW(m_hWnd, L"Ошибка печати ценников", L"Ошибка", MB_OK);
        }
        else {
            SetWindowTextW(m_hStatusLabel, L"Ценники напечатаны");
        }
    }

    // =================================================================
    // Печать приложения к договору (переиспользование ReceiptPrinter::print)
    // =================================================================
    void onPrintAppendix() {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        if (m_items.empty()) {
            MessageBoxW(m_hWnd, L"Нет товаров для печати", L"Внимание", MB_OK);
            return;
        }

        g_logger.info(L"CashierWindow: printing appendix");

        ReceiptData data;
        data.clientId = m_currentClient.value("id", 0);
        data.clientFullName = utf8_to_wstring(m_currentClient.value("full_name", ""));
        data.appendixNumber = m_currentAppendix.value("appendix_number", 0LL);
        data.totalQty = 0;
        data.totalValue = 0.0;
        data.totalClientAmount = 0.0;

        for (const auto& item : m_items) {
            ReceiptItem ri;
            ri.description = utf8_to_wstring(item.value("description", ""));
            ri.characteristic = utf8_to_wstring(item.value("condition", ""));
            ri.quantity = item.value("quantity", 1);
            ri.price = item.value("estimated_price", 0.0);
            ri.clientAmount = item.value("client_amount", 0.0);
            ri.note = utf8_to_wstring(item.value("note", ""));
            data.items.push_back(ri);
            data.totalQty += ri.quantity;
            data.totalValue += ri.price * ri.quantity;
            data.totalClientAmount += ri.clientAmount;
        }

        bool ok = ReceiptPrinter::print(data);
        if (!ok) {
            MessageBoxW(m_hWnd, L"Ошибка печати приложения", L"Ошибка", MB_OK);
        }
        else {
            SetWindowTextW(m_hStatusLabel, L"Приложение напечатано");
        }
    }

    // =================================================================
    // Возврат товара / Расписка
    // =================================================================
    void onReturnItems(bool isReceipt) {
        std::vector<int> selectedIds = getSelectedItemIds();
        if (selectedIds.empty()) {
            MessageBoxW(m_hWnd, L"Выберите товары", L"Внимание", MB_OK);
            return;
        }

        int clientId = m_currentClient.value("id", 0);
        if (clientId <= 0) {
            MessageBoxW(m_hWnd, L"Комитент не выбран", L"Ошибка", MB_OK);
            return;
        }

        std::wstring docType = isReceipt ? L"receipt" : L"return";
        g_logger.info(L"CashierWindow: " + docType + L" for " +
            std::to_wstring(selectedIds.size()) + L" items");

        std::wstring authToken = g_authManager.getAuthToken();
        HWND hWndCopy = m_hWnd;
        bool receipt = isReceipt;

        std::thread([hWndCopy, clientId, selectedIds, authToken, receipt]() {
            // Шаг 1: Создаём документ на сервере для получения номера
            json docReq;
            docReq["client_id"] = clientId;
            docReq["doc_type"] = receipt ? "receipt" : "return";
            auto docResp = g_httpsClient.post(L"/api/v1/cashier/create_document", docReq, authToken);

            int docNumber = 0;
            if (docResp && docResp->contains("doc_number")) {
                docNumber = (*docResp)["doc_number"].get<int>();
            }

            // Шаг 2: Помечаем товары как возвращённые
            json markReq;
            markReq["client_id"] = clientId;
            markReq["item_ids"] = selectedIds;
            auto markResp = g_httpsClient.post(L"/api/v1/cashier/mark_returned", markReq, authToken);

            json* pResult = new json();
            (*pResult)["doc_number"] = docNumber;
            (*pResult)["type"] = receipt ? "receipt" : "return";
            // === НОВОЕ: Передаем список выбранных ID в UI-поток ===
            json ids_arr = json::array();
            for (int id : selectedIds) ids_arr.push_back(id);
            (*pResult)["selected_ids"] = ids_arr;

            if (IsWindow(hWndCopy)) {
                PostMessageW(hWndCopy, WM_CASHIER_OP_COMPLETE, 0, (LPARAM)pResult);
            }
            else {
                delete pResult;
            }
            }).detach();
    }

    // =================================================================
    // Утрата товара
    // =================================================================
    void onLostItems() {
        std::vector<int> selectedIds = getSelectedItemIds();
        if (selectedIds.empty()) {
            MessageBoxW(m_hWnd, L"Выберите товары", L"Внимание", MB_OK);
            return;
        }

        int clientId = m_currentClient.value("id", 0);
        if (clientId <= 0) {
            MessageBoxW(m_hWnd, L"Комитент не выбран", L"Ошибка", MB_OK);
            return;
        }

        g_logger.info(L"CashierWindow: lost items operation");

        std::wstring authToken = g_authManager.getAuthToken();
        HWND hWndCopy = m_hWnd;
        json clientData = m_currentClient;

        std::thread([hWndCopy, clientId, selectedIds, authToken, clientData]() {
            // Шаг 1: Создаём документ на сервере для получения номера
            json docReq;
            docReq["client_id"] = clientId;
            docReq["doc_type"] = "loss";
            auto docResp = g_httpsClient.post(L"/api/v1/cashier/create_document", docReq, authToken);

            int docNumber = 0;
            if (docResp && docResp->contains("doc_number")) {
                docNumber = (*docResp)["doc_number"].get<int>();
            }

            // Шаг 2: Помечаем товары как возмещённые
            json markReq;
            markReq["client_id"] = clientId;
            markReq["item_ids"] = selectedIds;
            auto markResp = g_httpsClient.post(L"/api/v1/cashier/mark_compensated", markReq, authToken);

            json* pResult = new json();
            (*pResult)["doc_number"] = docNumber;
            (*pResult)["type"] = "loss";

            // === НОВОЕ: Передаем список выбранных ID в UI-поток ===
            json ids_arr = json::array();
            for (int id : selectedIds) ids_arr.push_back(id);
            (*pResult)["selected_ids"] = ids_arr;

            if (IsWindow(hWndCopy)) {
                PostMessageW(hWndCopy, WM_CASHIER_OP_COMPLETE, 0, (LPARAM)pResult);
            }
            else {
                delete pResult;
            }
            }).detach();
    }

    // =================================================================
    // Выплата за проданные товары
    // =================================================================
    void onPaymentItems() {
        int clientId = m_currentClient.value("id", 0);
        if (clientId <= 0) {
            MessageBoxW(m_hWnd, L"Комитент не выбран", L"Ошибка", MB_OK);
            return;
        }

        g_logger.info(L"CashierWindow: loading sold items for payment");
        SetWindowTextW(m_hStatusLabel, L"Загрузка проданных товаров...");

        std::wstring authToken = g_authManager.getAuthToken();
        std::wstring path = L"/api/v1/cashier/sold_items?client_id=" + std::to_wstring(clientId);
        HWND hWndCopy = m_hWnd;

        std::thread([hWndCopy, path, authToken]() {
            auto response = g_httpsClient.get(path, authToken);
            json* pResp = new json(response.value_or(json()));
            if (IsWindow(hWndCopy)) {
                PostMessageW(hWndCopy, WM_CASHIER_SOLD_LOADED, 0, (LPARAM)pResp);
            }
            else {
                delete pResp;
            }
            }).detach();
    }

    // =================================================================
    // Просроченные товары (>15 суток)
    // =================================================================
    void onOverdueItems() {
        int clientId = m_currentClient.value("id", 0);
        if (clientId <= 0) {
            MessageBoxW(m_hWnd, L"Комитент не выбран", L"Ошибка", MB_OK);
            return;
        }

        g_logger.info(L"CashierWindow: loading overdue items");
        SetWindowTextW(m_hStatusLabel, L"Загрузка просроченных товаров...");

        std::wstring authToken = g_authManager.getAuthToken();
        std::wstring path = L"/api/v1/cashier/unsold_items?client_id=" + std::to_wstring(clientId);
        HWND hWndCopy = m_hWnd;

        std::thread([hWndCopy, path, authToken]() {
            auto response = g_httpsClient.get(path, authToken);
            json* pResp = new json(response.value_or(json()));
            if (IsWindow(hWndCopy)) {
                PostMessageW(hWndCopy, WM_CASHIER_OVERDUE_LOADED, 0, (LPARAM)pResp);
            }
            else {
                delete pResp;
            }
            }).detach();
    }

    // =================================================================
    // Обработка сканирования штрих-кода (сканер АТОЛ-77)
    // =================================================================
    void onScanBarcode() {
        wchar_t barcodeBuf[CashierConfig::MAX_SCAN_LENGTH];
        GetWindowTextW(m_hBarcodeEdit, barcodeBuf, CashierConfig::MAX_SCAN_LENGTH);
        std::wstring barcode = barcodeBuf;

        if (barcode.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Отсканируйте штрих-код");
            SetFocus(m_hBarcodeEdit);
            return;
        }

        g_logger.info(L"CashierWindow: barcode scanned: " + barcode);
        processScannedBarcode(barcode);
    }

    // =================================================================
    // Обработка отсканированного штрих-кода
    // =================================================================
    void processScannedBarcode(const std::wstring& barcode) {
        // Формат: LOSS=1;APP=...;ID=...;FIO=...;ITEMS=...;SUM=...
        std::map<std::wstring, std::wstring> fields;
        std::wstringstream ss(barcode);
        std::wstring field;
        while (std::getline(ss, field, L';')) {
            size_t eqPos = field.find(L'=');
            if (eqPos != std::wstring::npos) {
                std::wstring key = field.substr(0, eqPos);
                std::wstring value = field.substr(eqPos + 1);
                fields[key] = value;
            }
        }

        if (fields.count(L"LOSS") && fields[L"LOSS"] == L"1") {
            g_logger.info(L"CashierWindow: LOSS barcode detected");

            std::wstring appendixNumber = fields.count(L"APP") ? fields[L"APP"] : L"";
            std::wstring clientIdStr = fields.count(L"ID") ? fields[L"ID"] : L"";
            std::wstring fio = fields.count(L"FIO") ? fields[L"FIO"] : L"";
            std::wstring sumStr = fields.count(L"SUM") ? fields[L"SUM"] : L"0";

            int barcodeClientId = 0;
            try { barcodeClientId = std::stoi(clientIdStr); }
            catch (...) {
                SetWindowTextW(m_hStatusLabel, L"Ошибка: неверный формат штрих-кода");
                return;
            }

            double sum = 0.0;
            try { sum = std::stod(sumStr); }
            catch (...) {
                SetWindowTextW(m_hStatusLabel, L"Ошибка: неверная сумма в штрих-коде");
                return;
            }

            int currentClientId = m_currentClient.value("id", 0);
            if (barcodeClientId != currentClientId) {
                SetWindowTextW(m_hStatusLabel, L"Внимание: штрих-код для другого комитента");
                return;
            }

            std::wstring confirmMsg = L"Выплатить комитенту компенсацию?\n"
                L"Сумма: " + ReceiptUtils::formatMoney(sum) + L" руб.";
            int result = MessageBoxW(m_hWnd, confirmMsg.c_str(),
                L"Подтверждение выплаты", MB_YESNO | MB_ICONQUESTION);

            if (result == IDYES) {
                SetWindowTextW(m_hStatusLabel,
                    (L"Выплата " + ReceiptUtils::formatMoney(sum) + L" руб. выполнена").c_str());
                MessageBoxW(m_hWnd,
                    (L"Выплата компенсации " + ReceiptUtils::formatMoney(sum) +
                        L" руб. выполнена успешно.").c_str(),
                    L"Выплата выполнена", MB_OK | MB_ICONINFORMATION);
            }
        }
        else {
            SetWindowTextW(m_hStatusLabel, L"Неизвестный формат штрих-кода");
        }
    }

    // =================================================================
    // Выход
    // =================================================================
    void onLogout() {
        g_authManager.logout();
        g_logger.info(L"CashierWindow: user logged out");
        DestroyWindow(m_hWnd);
    }

    // =================================================================
    // Оконная процедура
    // =================================================================
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        CashierWindow* pThis = nullptr;

        if (msg == WM_CREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<CashierWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            pThis->m_hWnd = hWnd;
            pThis->createControls();
            return 0;
        }

        pThis = reinterpret_cast<CashierWindow*>(
            GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (!pThis) return DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);
            if (code == BN_CLICKED) {
                switch (id) {
                case IDC_CASH_SEARCH_BTN: pThis->onSearch();
                    break;
                case IDC_CASH_PRINT_TAG_BTN: pThis->onPrintPriceTags();
                    break;
                case IDC_CASH_PRINT_APP_BTN: pThis->onPrintAppendix();
                    break;
                case IDC_CASH_RETURN_BTN: pThis->onReturnItems(false);
                    break;
                case IDC_CASH_RECEIPT_BTN: pThis->onReturnItems(true);
                    break;
                case IDC_CASH_LOST_BTN: pThis->onLostItems();
                    break;
                case IDC_CASH_PAYMENT_BTN: pThis->onPaymentItems();
                    break;
                case IDC_CASH_OVERDUE_BTN: pThis->onOverdueItems();
                    break;
                case IDC_CASH_SCAN_BTN: pThis->onScanBarcode();
                    break;
                case IDC_CASH_LOGOUT_BTN: pThis->onLogout();
                    break;
                case IDC_CASH_SELECT_ALL_BTN: {
                    int count = ListView_GetItemCount(pThis->m_hItemsList);
                    for (int i = 0; i < count; i++) {
                        ListView_SetCheckState(pThis->m_hItemsList, i, TRUE);
                    }
                    break;
                }
                case IDC_CASH_DESELECT_ALL_BTN: {
                    int count = ListView_GetItemCount(pThis->m_hItemsList);
                    for (int i = 0; i < count; i++) {
                        ListView_SetCheckState(pThis->m_hItemsList, i, FALSE);
                    }
                    break;
                }
                }
            }
            // Обработка смены выбора приложения
            if (id == IDC_CASH_APPENDIX_COMBO && code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(pThis->m_hAppendixCombo, CB_GETCURSEL, 0, 0);
                // Проверяем, что индекс корректен и не выходит за границы сохраненного вектора
                if (sel != CB_ERR && sel >= 0 && sel < (int)pThis->m_appendices.size()) {

                    // Обновляем текущее приложение
                    pThis->m_currentAppendix = pThis->m_appendices[sel];

                    // БЕЗОПАСНОЕ извлечение ID:
                    int clientId = pThis->m_currentClient.is_object()
                        ? pThis->m_currentClient.value("id", 0) : 0;
                    if (clientId > 0) {
                        long long appendixId = pThis->m_currentAppendix.value("id", 0LL);
                        pThis->loadAppendixItems(appendixId, clientId);
                        g_logger.info(L"CashierWindow: appendix selected, index=" + std::to_wstring(sel) +
                            L", appendixId=" + std::to_wstring(appendixId));
                        // Загружаем товары выбранного приложения
                        pThis->loadAppendixItems(appendixId, clientId);
                    }
                }
                return 0;
            }
			break;
        }
        case WM_CASHIER_SEARCH_RESULT:
            pThis->onSearchResult(reinterpret_cast<json*>(lParam));
            return 0;

        case WM_CASHIER_APPENDICES_LOADED:
            pThis->onAppendicesLoaded(reinterpret_cast<json*>(lParam));
            return 0;

        case WM_CASHIER_ITEMS_LOADED:
            pThis->onItemsLoaded(reinterpret_cast<json*>(lParam));
            return 0;

        case WM_CASHIER_OP_COMPLETE: {
            json* pResult = reinterpret_cast<json*>(lParam);
            if (pResult) {
                int docNum = pResult->value("doc_number", 0);
                std::wstring type = utf8_to_wstring(pResult->value("type", std::string("return")));

                // 1. Извлекаем список ID выбранных товаров, переданный из фонового потока
                std::vector<int> selectedIds;
                if (pResult->contains("selected_ids") && (*pResult)["selected_ids"].is_array()) {
                    for (const auto& id_val : (*pResult)["selected_ids"]) {
                        if (id_val.is_number()) {
                            selectedIds.push_back(id_val.get<int>());
                        }
                    }
                }

                // Печатаем документ
                std::vector<ReceiptItem> docItems;
                int totalQty = 0;
                double totalValue = 0.0;
                std::lock_guard<std::mutex> lock(pThis->m_dataMutex);

                for (const auto& item : pThis->m_items) {
                    int itemId = item.value("id", 0);

                    // 2. ФИЛЬТРАЦИЯ: Если список выбранных ID не пуст, проверяем наличие текущего товара
                    if (!selectedIds.empty()) {
                        bool isSelected = false;
                        for (int selId : selectedIds) {
                            if (selId == itemId) {
                                isSelected = true;
                                break;
                            }
                        }
                        if (!isSelected) continue; // Пропускаем товар, если он не был выбран кассиром
                    }

                    ReceiptItem ri;
                    ri.description = utf8_to_wstring(item.value("description", ""));
                    ri.characteristic = utf8_to_wstring(item.value("condition", ""));
                    ri.quantity = item.value("quantity", 1);
                    ri.price = item.value("estimated_price", 0.0);
                    ri.note = utf8_to_wstring(item.value("note", ""));
                    docItems.push_back(ri);
                    totalQty += ri.quantity;
                    totalValue += ri.price * ri.quantity;
                }

                std::wstring clientFullName = utf8_to_wstring(pThis->m_currentClient.value("full_name", ""));
                std::wstring passSeries = utf8_to_wstring(pThis->m_currentClient.value("passport_series", ""));
                std::wstring passNumber = utf8_to_wstring(pThis->m_currentClient.value("passport_number", ""));

                CashierDocumentPrinter::DocType docType;
                if (type == L"receipt") docType = CashierDocumentPrinter::DocType::RECEIPT;
                else if (type == L"loss") docType = CashierDocumentPrinter::DocType::LOSS;
                else docType = CashierDocumentPrinter::DocType::RETURN;

                CashierDocumentPrinter::printDocument(docType, docNum,
                    clientFullName, passSeries, passNumber,
                    docItems, totalQty, totalValue);

                std::wstring msg = L"Документ N" + std::to_wstring(docNum) + L" оформлен";
                SetWindowTextW(pThis->m_hStatusLabel, msg.c_str());
                MessageBoxW(pThis->m_hWnd, msg.c_str(), L"Успех", MB_OK);
                delete pResult;
            }
            return 0;
        }

        case WM_CASHIER_SOLD_LOADED: {
            json* pResp = reinterpret_cast<json*>(lParam);
            if (pResp) {
                if (pResp->contains("sold_items")) {
                    std::lock_guard<std::mutex> lock(pThis->m_dataMutex);
                    pThis->m_soldItems = (*pResp)["sold_items"].get<std::vector<json>>();

                    std::wstring text = L"Проданные товары:\r\n";
                    double totalPay = 0.0;
                    for (const auto& si : pThis->m_soldItems) {
                        text += utf8_to_wstring(si.value("description", "")) +
                            L" | на руки: " +
                            std::to_wstring(si.value("client_amount", 0.0)) + L"\r\n";
                        totalPay += si.value("client_amount", 0.0);
                    }
                    text += L"\r\nИТОГО НА РУКИ: " + ReceiptUtils::formatMoney(totalPay) + L" руб";
                    SetWindowTextW(pThis->m_hResultEdit, text.c_str());
                }
                delete pResp;
            }
            return 0;
        }

        case WM_CASHIER_OVERDUE_LOADED: {
            json* pResp = reinterpret_cast<json*>(lParam);
            if (pResp) {
                if (pResp->contains("unsold_items")) {
                    std::lock_guard<std::mutex> lock(pThis->m_dataMutex);
                    pThis->m_overdueItems = (*pResp)["unsold_items"].get<std::vector<json>>();

                    std::wstring text = L"Просроченные товары (>15 дн):\r\n";
                    for (const auto& oi : pThis->m_overdueItems) {
                        text += utf8_to_wstring(oi.value("description", "")) +
                            L" | остаток: " +
                            std::to_wstring(oi.value("unsold_quantity", 0)) + L" шт\r\n";
                    }
                    SetWindowTextW(pThis->m_hResultEdit, text.c_str());
                }
                delete pResp;
            }
            return 0;
        }

        case WM_CASHIER_ERROR: {
            const wchar_t* errorMsg = reinterpret_cast<const wchar_t*>(lParam);
            SetWindowTextW(pThis->m_hStatusLabel, errorMsg);
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            if (pThis->m_hFont) DeleteObject(pThis->m_hFont);
            if (pThis->m_hFontBold) DeleteObject(pThis->m_hFontBold);
            if (pThis->m_hFontLarge) DeleteObject(pThis->m_hFontLarge);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

public:
    CashierWindow()
        : m_hWnd(nullptr), m_hIdEdit(nullptr), m_hLastNameEdit(nullptr),
        m_hFirstNameEdit(nullptr), m_hMiddleNameEdit(nullptr),
        m_hSearchBtn(nullptr), m_hClientInfo(nullptr), m_hAppendixCombo(nullptr),
        m_hItemsList(nullptr), m_hBarcodeEdit(nullptr),
        m_hPrintTagBtn(nullptr), m_hPrintAppBtn(nullptr),
        m_hReturnBtn(nullptr), m_hReceiptBtn(nullptr), m_hLostBtn(nullptr),
        m_hPaymentBtn(nullptr), m_hOverdueBtn(nullptr), m_hScanBtn(nullptr),
        m_hLogoutBtn(nullptr), m_hStatusLabel(nullptr), m_hResultEdit(nullptr),
        m_hSelectAllBtn(nullptr), m_hDeselectAllBtn(nullptr),
        m_hFont(nullptr), m_hFontBold(nullptr), m_hFontLarge(nullptr) {
        // НОВОЕ: Инициализируем пустыми объектами, чтобы избежать исключений 
        // при обращении к .value() или [] до загрузки данных с сервера.
        m_currentClient = json::object();
        m_currentAppendix = json::object();
        g_logger.info(L"CashierWindow: constructor");
    }

    ~CashierWindow() {
        g_logger.info(L"CashierWindow: destructor");
    }

    void show() {
        g_logger.info(L"CashierWindow::show() entered");

        static bool classRegistered = false;
        if (!classRegistered) {
            WNDCLASSEXW wcex = {};
            wcex.cbSize = sizeof(WNDCLASSEX);
            wcex.style = CS_HREDRAW | CS_VREDRAW;
            wcex.lpfnWndProc = WndProc;
            wcex.hInstance = g_hInstance;
            wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wcex.lpszClassName = CLASS_NAME;
            wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
            if (!RegisterClassExW(&wcex)) {
                g_logger.error(L"CashierWindow: RegisterClassExW failed");
                return;
            }
            classRegistered = true;
        }

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);

        m_hWnd = CreateWindowExW(
            WS_EX_WINDOWEDGE, CLASS_NAME,
            L"Кассир - Комиссионный магазин СОВЕТСКИЙ",
            WS_OVERLAPPEDWINDOW,
            0, 0, screenW, screenH,
            nullptr, nullptr, g_hInstance, this);

        if (!m_hWnd) {
            g_logger.error(L"CashierWindow: CreateWindowExW failed");
            return;
        }

        ShowWindow(m_hWnd, SW_SHOWMAXIMIZED);
        UpdateWindow(m_hWnd);
        g_logger.info(L"CashierWindow: window shown, entering message loop");

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (!IsWindow(m_hWnd)) break;
        }
        g_logger.info(L"CashierWindow: message loop ended");
    }
};