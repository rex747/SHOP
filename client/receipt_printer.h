// receipt_printer.h
#pragma once

#include <windows.h>
#include <winspool.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include "queue_manager.h"

#pragma comment(lib, "winspool.lib")  // для линковки

class ReceiptPrinter {
private:
    std::wstring m_printerName;
    bool m_initialized;

    HDC openPrinter() {
        DOCINFO di = { sizeof(DOCINFO), L"Ticket Print Job", nullptr };
        HDC hdc = CreateDC(L"RAW", m_printerName.c_str(), nullptr, nullptr);
        return hdc;
    }
    // Формат талона
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

    // Формат талона с дополнительным текстом
    std::wstring formatTicketWithExtra(const QueueTicket& ticket,
        const std::wstring& clientName,
        const std::wstring& clientPhone,
        const std::wstring& extraMessage) {
        std::wstringstream ss;
        // базовое форматирование как в formatTicket
        ss << L"================================\n";
        ss << L"   ДОБРО - Комиссионный магазин\n";
        ss << L"================================\n\n";
        ss << L"ТАЛОН ОЧЕРЕДИ (НА ДОВЕРИИ)\n"; // уточнение
        ss << L"Номер: " << ticket.ticketNumber << L"\n\n";
        ss << L"Клиент: " << clientName << L"\n";
        ss << L"Телефон: " << clientPhone << L"\n\n";
        ss << L"Окно приема: " << ticket.windowNumber << L"\n";
        ss << L"\n" << extraMessage << L"\n\n";
        
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


    bool saveTicketToFile(const QueueTicket& ticket,
        const std::wstring& clientName,
        const std::wstring& clientPhone) {
        std::wstring content = formatTicket(ticket, clientName, clientPhone);

        std::wstring filename = L"ticket_" + ticket.ticketNumber + L".txt";
        HANDLE hFile = CreateFileW(filename.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }

        DWORD bytesWritten;
        WriteFile(hFile, content.c_str(),
            static_cast<DWORD>(content.size() * sizeof(wchar_t)),
            &bytesWritten, nullptr);

        CloseHandle(hFile);
        return true;
    }

    bool saveTicketToFileWithExtra(const QueueTicket& ticket,
        const std::wstring& clientName, 
        const std::wstring& clientPhone, 
        const std::wstring& extraMessage) {

        std::wstring content = formatTicketWithExtra(ticket, clientName, clientPhone, extraMessage);

        std::wstring filename = L"ticketWithExtra_" + ticket.ticketNumber + L".txt";
        HANDLE hFile = CreateFileW(filename.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }

        DWORD bytesWritten;
        WriteFile(hFile, content.c_str(),
            static_cast<DWORD>(content.size() * sizeof(wchar_t)),
            &bytesWritten, nullptr);

        CloseHandle(hFile);
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

        if (size > 0) {
            std::vector<BYTE> buffer(size);
            if (EnumPrinters(PRINTER_ENUM_LOCAL, nullptr, 2, buffer.data(), size, &size, nullptr)) {
                PRINTER_INFO_2* printers = (PRINTER_INFO_2*)buffer.data();
                if (size > 0) {
                    m_printerName = printers[0].pPrinterName;
                    m_initialized = true;
                }
            }
        }
    }

    bool printTicket(const QueueTicket& ticket, const std::wstring& clientName,
        const std::wstring& clientPhone) {
        if (!m_initialized) {
            return saveTicketToFile(ticket, clientName, clientPhone);
        }

        std::wstring content = formatTicket(ticket, clientName, clientPhone);

        HANDLE hPrinter;
        if (!OpenPrinter(const_cast<LPWSTR>(m_printerName.c_str()), &hPrinter, nullptr)) {
            return false;
        }

        DOCINFOW di = { sizeof(DOCINFOW), L"Ticket", nullptr };
        // Исправленный вызов StartDocPrinter:
        StartDocPrinter(hPrinter, 1, (LPBYTE)&di);   // <-- исправлено
        StartPagePrinter(hPrinter);

        DWORD bytesWritten;
        WritePrinter(hPrinter, (LPVOID)content.c_str(),
            static_cast<DWORD>(content.size() * sizeof(wchar_t)), &bytesWritten);

        EndPagePrinter(hPrinter);
        EndDocPrinter(hPrinter);
        ClosePrinter(hPrinter);

        return true;
    }

    // Печать талона с дополнительной надписью
    bool printTicketWithExtraMessage(const QueueTicket& ticket,
        const std::wstring& clientName,
        const std::wstring& clientPhone,
        const std::wstring& extraMessage) {
        if (!m_initialized) {
            return saveTicketToFileWithExtra(ticket, clientName, clientPhone, extraMessage);
        }

        std::wstring content = formatTicketWithExtra(ticket, clientName, clientPhone, extraMessage);
        HANDLE hPrinter;
        if (!OpenPrinter(const_cast<LPWSTR>(m_printerName.c_str()), &hPrinter, nullptr)) {
            return false;
        }

        DOCINFOW di = { sizeof(DOCINFOW), L"TicketWithExtraMessage", nullptr };
        // Исправленный вызов StartDocPrinter:
        StartDocPrinter(hPrinter, 1, (LPBYTE)&di);   // <-- исправлено
        StartPagePrinter(hPrinter);

        DWORD bytesWritten;
        WritePrinter(hPrinter, (LPVOID)content.c_str(),
            static_cast<DWORD>(content.size() * sizeof(wchar_t)), &bytesWritten);

        EndPagePrinter(hPrinter);
        EndDocPrinter(hPrinter);
        ClosePrinter(hPrinter);

        return true;
    }

};
