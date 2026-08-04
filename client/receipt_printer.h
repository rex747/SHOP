// receipt_printer.h
// Печать талонов на принтере, подключённом к терминалу (киоск самообслуживания).
// ПРОДАКШН-ВЕРСИЯ. Архитектура и логика не меняются:
//  - печать идёт на принтер терминала (принтер по умолчанию ОС киоска);
//  - добавлено перекодирование UTF-16 -> CP1251 (термопринтеры ESC/POS принимают
//    однобайтовую кириллицу; запись wchar_t давала "мусор"/пустой чек);
//  - добавлено повторное обнаружение принтера непосредственно при печати
//    (принтер терминала может инициализироваться позже старта приложения);
//  - исправлен блок EnumPrinters конструктора (ранее выполнялся с испорченным size);
//  - добавлено полное логгирование каждого шага печати и кодов ошибок GetLastError();
//  - формат бланка "НА ДОВЕРИИ" (formatTicketWithExtra) соответствует строго
//    утверждённому юридическому тексту из бизнес-требования.
#pragma once
#include <windows.h>
#include <winspool.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <chrono>
#include "queue_manager.h"
#include "logger.h"
#pragma comment(lib, "winspool.lib")  // для линковки

extern Logger g_logger; // глобальный логгер терминала (определён в main.cpp)

class ReceiptPrinter {
private:
    std::wstring m_printerName;
    bool m_initialized;

    HDC openPrinter() {
        DOCINFO di = { sizeof(DOCINFO), L"Ticket Print Job", nullptr };
        HDC hdc = CreateDC(L"RAW", m_printerName.c_str(), nullptr, nullptr);
        return hdc;
    }

    // Перекодирование строки талона в CP1251 (кириллица для принтера терминала).
    std::string toPrinterEncoding(const std::wstring& content) {
        int ansiSize = WideCharToMultiByte(1251, 0, content.c_str(),
            static_cast<int>(content.size()), nullptr, 0, nullptr, nullptr);
        if (ansiSize <= 0) {
            g_logger.error(L"ReceiptPrinter::toPrinterEncoding: WideCharToMultiByte(size) failed, err=" +
                std::to_wstring(GetLastError()));
            return std::string();
        }
        std::string ansi(static_cast<size_t>(ansiSize), '\0');
        WideCharToMultiByte(1251, 0, content.c_str(),
            static_cast<int>(content.size()), &ansi[0], ansiSize, nullptr, nullptr);
        g_logger.info(L"ReceiptPrinter::toPrinterEncoding: converted " +
            std::to_wstring(content.size()) + L" wchar_t -> " +
            std::to_wstring(ansiSize) + L" bytes (CP1251)");
        return ansi;
    }

    // Повторное обнаружение принтера терминала (вызывается только если m_initialized == false).
    void reDetectPrinter() {
        DWORD size = 0;
        GetDefaultPrinterW(nullptr, &size);
        if (size > 0) {
            std::wstring printerName(size, L'\0');
            if (GetDefaultPrinterW(&printerName[0], &size)) {
                m_printerName = printerName.c_str();
                m_initialized = true;
                g_logger.info(L"ReceiptPrinter::reDetectPrinter: terminal printer found: " + m_printerName);
                return;
            }
        }
        g_logger.warning(L"ReceiptPrinter::reDetectPrinter: default printer still not found, err=" +
            std::to_wstring(GetLastError()));
    }

    // Формат стандартного талона
    std::wstring formatTicket(const QueueTicket& ticket,
        const std::wstring& clientName,
        const std::wstring& clientPhone) {
        std::wstringstream ss;
        ss << L"================================\n";
        ss << L"   ДОБРО - Комиссионный магазин\n";
        ss << L"================================\n\n";
        ss << L"ТАЛОН ОЧЕРЕДИ\n";
        ss << L"Номер: " << ticket.ticketNumber << L"\n\n";
        ss << L"Клиент: " << clientName << L"\n";
        ss << L"Телефон: " << clientPhone << L"\n\n";
        ss << L"Тип очереди: ";
        switch (ticket.type) {
        case QueueType::GENERAL:
            ss << L"Общая очередь\n";
            break;
        case QueueType::FIRST_TIME:
            ss << L"Первый раз\n";
            break;
        case QueueType::EXTRA_20:
            ss << L"+20 позиций\n";
            break;
        case QueueType::TRUST:
            ss << L"На доверии\n";
            break;
        case QueueType::PAID:
            ss << L"Платный прием\n";
            break;
        case QueueType::EXPENSIVE:
            ss << L"Дорогой товар\n";
            break;
        }
        ss << L"Количество товаров: " << ticket.itemsCount << L"\n";
        ss << L"Ваша позиция: " << ticket.position << L"\n";
        ss << L"Окно приема: " << ticket.windowNumber << L"\n";
        if (ticket.estimatedWaitTime > 0) {
            ss << L"Примерное время ожидания: "
                << ticket.estimatedWaitTime << L" мин\n";
        }
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        struct tm timeinfo;
        localtime_s(&timeinfo, &time_t_now);
        ss << L"\nДата: " << std::put_time(&timeinfo, L"%d.%m.%Y %H:%M") << L"\n";
        ss << L"\n================================\n";
        ss << L"Сохраните этот талон!\n";
        ss << L"================================\n\n";
        return ss.str();
    }

    // Формат талона для услуги "НА ДОВЕРИИ" - строго утверждённый юридический бланк.
    // Тот же текст выводится в превью над кнопкой "Печать талона"
    // в showTrustAcceptanceWindow() (единое бизнес-требование).
    std::wstring formatTicketWithExtra(const QueueTicket& ticket,
        int clientId,
        const std::wstring& clientName,
        const std::wstring& clientPhone) {
        std::wstringstream ss;
        ss << L"КОМИССИОННЫЙ МАГАЗИН\n";
        ss << L"ДОБРО\n\n";
        ss << L"Комитент №" << clientId << L"\n";
        ss << L"Я, " << clientName << L" доверяю Вам оценить вещи на Ваше усмотрение, без согласования цен со мной. Согласен на обработку моих персональных данных.\n";
        ss << L"Условия:\n";
        ss << L"Комиссионный магазин принимает товар по услуге «Доверие»,\n";
        ss << L"без проверки содержимого пакетов, коробок и иной упаковки.\n";
        ss << L"Магазин не несет ответственности за вложенные в пакет вещи,\n";
        ss << L"их наличие или отсутствие, сохранность пакета и его возврат.\n";
        ss << L"Вещи, которые товаровед не примет по состоянию, качеству или др. причинам,\n";
        ss << L"автоматически отправляются в категорию «на утилизацию» и выставляются\n";
        ss << L"за один рубль без составления перечня.\n\n";
        ss << L"Подписывая данный бланк, я подтверждаю, что ознакомлен \n";
        ss << L"с условиями и согласен с ними. Также я соглашаюсь с возможной \n";
        ss << L"утратой пакета или части его содержимого без претензий с моей стороны.\n\n";
        ss << L"Мой телефон: " << clientPhone << L"\n";
        ss << L"(телефон необходим для возможности осуществления удаленной проверки продаж)\n";
        ss << L"ВНИМАНИЕ – мы не звоним Вам!\n";
        ss << L"Если Вы сдаете вещи впервые, то заполните данные для регистрации:\n";
        ss << L"ПАСПОРТ:\n";
        ss << L"Серия _______________ Номер_______________\n";
        ss << L"Выдан _____________ от ___________________\n";
        ss << L"Дата выдачи ______________________________\n";
        ss << L"Дата рождения ____________________________\n";
        ss << L"Проживаю ________________________________\n";

        // Служебная часть талона электронной очереди (необходима для работы системы)
        ss << L"\n================================\n";
        ss << L"Талон №: " << ticket.ticketNumber << L"\n";
        ss << L"Окно приема: " << ticket.windowNumber << L"\n";
        if (ticket.estimatedWaitTime > 0) {
            ss << L"Примерное время ожидания: "
                << ticket.estimatedWaitTime << L" мин\n";
        }
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        struct tm timeinfo;
        localtime_s(&timeinfo, &time_t_now);
        ss << L"Дата: " << std::put_time(&timeinfo, L"%d.%m.%Y %H:%M") << L"\n";
        ss << L"================================\n";
        ss << L"Сохраните этот талон!\n";
        ss << L"================================\n\n";
        return ss.str();
    }

    bool saveTicketToFile(const QueueTicket& ticket,
        const std::wstring& clientName,
        const std::wstring& clientPhone) {
        std::wstring content = formatTicket(ticket, clientName, clientPhone);
        std::wstring filename = L"ticket_" + ticket.ticketNumber + L".txt";
        HANDLE hFile = CreateFileW(filename.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            g_logger.error(L"ReceiptPrinter::saveTicketToFile: CreateFileW failed for " +
                filename + L", err=" + std::to_wstring(GetLastError()));
            return false;
        }
        DWORD bytesWritten;
        WriteFile(hFile, content.c_str(),
            static_cast<DWORD>(content.size() * sizeof(wchar_t)),
            &bytesWritten, nullptr);
        CloseHandle(hFile);
        g_logger.warning(L"ReceiptPrinter::saveTicketToFile: ticket saved to file (degradation): " + filename);
        return true;
    }

    bool saveTicketToFileWithExtra(const QueueTicket& ticket,
        int clientId,
        const std::wstring& clientName,
        const std::wstring& clientPhone) {
        std::wstring content = formatTicketWithExtra(ticket, clientId, clientName, clientPhone);
        std::wstring filename = L"ticketWithExtra_" + ticket.ticketNumber + L".txt";
        HANDLE hFile = CreateFileW(filename.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            g_logger.error(L"ReceiptPrinter::saveTicketToFileWithExtra: CreateFileW failed for " +
                filename + L", err=" + std::to_wstring(GetLastError()));
            return false;
        }
        DWORD bytesWritten;
        WriteFile(hFile, content.c_str(),
            static_cast<DWORD>(content.size() * sizeof(wchar_t)),
            &bytesWritten, nullptr);
        CloseHandle(hFile);
        g_logger.warning(L"ReceiptPrinter::saveTicketToFileWithExtra: ticket saved to file (degradation): " + filename);
        return true;
    }

public:
    ReceiptPrinter() : m_initialized(false) {
        // Find default printer or specific printer
        DWORD size = 0;
        GetDefaultPrinterW(nullptr, &size);
        if (size > 0) {
            std::wstring printerName(size, L'\0');
            if (GetDefaultPrinterW(&printerName[0], &size)) {
                m_printerName = printerName.c_str();
                m_initialized = true;
            }
        }
        // ИСПРАВЛЕНО: перебор локальных принтеров только если дефолтный не найден
        // (ранее блок выполнялся с уже изменённым size и мог затереть имя принтера).
        if (!m_initialized) {
            DWORD needed = 0;
            if (EnumPrinters(PRINTER_ENUM_LOCAL, nullptr, 2, nullptr, 0, &needed, nullptr) && needed > 0) {
                std::vector<BYTE> buffer(needed);
                if (EnumPrinters(PRINTER_ENUM_LOCAL, nullptr, 2, buffer.data(), needed, &needed, nullptr)) {
                    PRINTER_INFO_2* printers = (PRINTER_INFO_2*)buffer.data();
                    if (needed > 0) {
                        m_printerName = printers[0].pPrinterName;
                        m_initialized = true;
                    }
                }
            }
        }
        if (m_initialized) {
            g_logger.info(L"ReceiptPrinter: constructor: terminal printer detected: " + m_printerName);
        }
        else {
            g_logger.warning(L"ReceiptPrinter: constructor: no printer detected at startup (will re-detect at print time)");
        }
    }

    bool printTicket(const QueueTicket& ticket, const std::wstring& clientName,
        const std::wstring& clientPhone) {
        g_logger.info(L"ReceiptPrinter::printTicket: start, ticket=" + ticket.ticketNumber);
        if (!m_initialized) {
            reDetectPrinter(); // принтер терминала мог появиться после старта
        }
        if (!m_initialized) {
            return saveTicketToFile(ticket, clientName, clientPhone);
        }
        std::wstring content = formatTicket(ticket, clientName, clientPhone);
        std::string ansi = toPrinterEncoding(content);
        if (ansi.empty()) {
            return saveTicketToFile(ticket, clientName, clientPhone);
        }
        HANDLE hPrinter;
        if (!OpenPrinter(const_cast<LPWSTR>(m_printerName.c_str()), &hPrinter, nullptr)) {
            g_logger.error(L"ReceiptPrinter::printTicket: OpenPrinter('" + m_printerName +
                L"') failed, err=" + std::to_wstring(GetLastError()));
            return saveTicketToFile(ticket, clientName, clientPhone);
        }
        DOCINFOW di = { sizeof(DOCINFOW), L"Ticket", nullptr };
        StartDocPrinter(hPrinter, 1, (LPBYTE)&di);
        StartPagePrinter(hPrinter);
        DWORD bytesWritten = 0;
        BOOL wr = WritePrinter(hPrinter, (LPVOID)ansi.c_str(),
            static_cast<DWORD>(ansi.size()), &bytesWritten);
        if (!wr || bytesWritten != ansi.size()) {
            g_logger.error(L"ReceiptPrinter::printTicket: WritePrinter failed/incomplete, err=" +
                std::to_wstring(GetLastError()) + L", written=" + std::to_wstring(bytesWritten) +
                L" of " + std::to_wstring(ansi.size()));
        }
        else {
            g_logger.info(L"ReceiptPrinter::printTicket: sent " + std::to_wstring(bytesWritten) +
                L" bytes to terminal printer '" + m_printerName + L"'");
        }
        EndPagePrinter(hPrinter);
        EndDocPrinter(hPrinter);
        ClosePrinter(hPrinter);
        return true;
    }

    // Печать талона услуги "НА ДОВЕРИИ" (бланк доверия + служебная часть)
    bool printTicketWithExtraMessage(const QueueTicket& ticket,
        int clientId,
        const std::wstring& clientName,
        const std::wstring& clientPhone) {
        g_logger.info(L"ReceiptPrinter::printTicketWithExtraMessage: start, ticket=" +
            ticket.ticketNumber + L", clientId=" + std::to_wstring(clientId));
        if (!m_initialized) {
            reDetectPrinter(); // принтер терминала мог появиться после старта
        }
        if (!m_initialized) {
            return saveTicketToFileWithExtra(ticket, clientId, clientName, clientPhone);
        }
        std::wstring content = formatTicketWithExtra(ticket, clientId, clientName, clientPhone);
        std::string ansi = toPrinterEncoding(content);
        if (ansi.empty()) {
            return saveTicketToFileWithExtra(ticket, clientId, clientName, clientPhone);
        }
        HANDLE hPrinter;
        if (!OpenPrinter(const_cast<LPWSTR>(m_printerName.c_str()), &hPrinter, nullptr)) {
            g_logger.error(L"ReceiptPrinter::printTicketWithExtraMessage: OpenPrinter('" +
                m_printerName + L"') failed, err=" + std::to_wstring(GetLastError()));
            return saveTicketToFileWithExtra(ticket, clientId, clientName, clientPhone);
        }
        DOCINFOW di = { sizeof(DOCINFOW), L"TicketWithExtraMessage", nullptr };
        StartDocPrinter(hPrinter, 1, (LPBYTE)&di);
        StartPagePrinter(hPrinter);
        DWORD bytesWritten = 0;
        BOOL wr = WritePrinter(hPrinter, (LPVOID)ansi.c_str(),
            static_cast<DWORD>(ansi.size()), &bytesWritten);
        if (!wr || bytesWritten != ansi.size()) {
            g_logger.error(L"ReceiptPrinter::printTicketWithExtraMessage: WritePrinter failed/incomplete, err=" +
                std::to_wstring(GetLastError()) + L", written=" + std::to_wstring(bytesWritten) +
                L" of " + std::to_wstring(ansi.size()));
        }
        else {
            g_logger.info(L"ReceiptPrinter::printTicketWithExtraMessage: sent " +
                std::to_wstring(bytesWritten) + L" bytes to terminal printer '" + m_printerName + L"'");
        }
        EndPagePrinter(hPrinter);
        EndDocPrinter(hPrinter);
        ClosePrinter(hPrinter);
        return true;
    }
};