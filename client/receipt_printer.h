// receipt_printer.h

#pragma once
#include  <windows.h >
#include  <winspool.h >
#include  <string >
#include  <vector >
#include  <sstream >
#include  <iomanip >
#include  <chrono >
#include  <mutex >

#include  "queue_manager.h "
#include  "logger.h "
#pragma comment(lib, "winspool.lib")

extern Logger g_logger;

class MainWindow;

class ReceiptPrinter {
private:
    std::wstring m_printerName;
    bool m_initialized;
    std::mutex m_printMutex;
    friend class MainWindow;
    // Перекодирование строки талона в CP866 (DOS кириллица для принтера терминала).
     // CP866 поддерживается ВСЕМИ термопринтерами ESC/POS, включая китайские KPOS 80.
     // ПРИМЕЧАНИЕ: при GDI-печати этот метод больше не используется для формирования данных,
     // так как драйвер Windows сам конвертирует Unicode в нужную кодировку принтера.
     // Метод сохранён для обратной совместимости и возможного логгирования.
    std::string toPrinterEncoding(const std::wstring& content) {
        int ansiSize = WideCharToMultiByte(866, 0, content.c_str(),
            static_cast<int>(content.size()), nullptr, 0, nullptr, nullptr);
        if (ansiSize <= 0) {
            g_logger.error(L"ReceiptPrinter::toPrinterEncoding: WideCharToMultiByte(size) failed, err=" +
                std::to_wstring(GetLastError()));
            return std::string();
        }
        std::string ansi(static_cast<size_t>(ansiSize), '\0');
        WideCharToMultiByte(866, 0, content.c_str(),
            static_cast<int>(content.size()), &ansi[0], ansiSize, nullptr, nullptr);
        g_logger.info(L"ReceiptPrinter::toPrinterEncoding: converted " +
            std::to_wstring(content.size()) + L" wchar_t -> " +
            std::to_wstring(ansiSize) + L" bytes (CP866)");
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
        ss << L"   Комиссионный магазин\n";
        ss << L"         СОВЕТСКИЙ\n";
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
    // ВСЕ строки искусственно разбиты так, чтобы не превышать ~38 символов,
    // что гарантирует размещение в полезной области 60 мм (70 мм лента - 10 мм поля).
    std::wstring formatTicketWithExtra(const QueueTicket& ticket,
        int clientId,
        const std::wstring& clientName,
        const std::wstring& clientPhone) {
        std::wstringstream ss;
        ss << L"КОМИССИОННЫЙ МАГАЗИН\n";
        ss << L"       СОВЕТСКИЙ\n\n";
        ss << L"Комитент №" << clientId << L"\n";
        ss << L"Я, " << clientName << L"\n";
        ss << L"доверяю Вам оценить вещи на Ваше\n";
        ss << L"усмотрение, без согласования цен\n";
        ss << L"со мной. Согласен на обработку\n";
        ss << L"моих персональных данных.\n\n";
        ss << L"Условия:\n";
        ss << L"Комиссионный магазин принимает\n";
        ss << L"товар по услуге «Доверие», без\n";
        ss << L"проверки содержимого пакетов,\n";
        ss << L"коробок и иной упаковки.\n";
        ss << L"Магазин не несет ответственности\n";
        ss << L"за вложенные в пакет вещи, их\n";
        ss << L"наличие или отсутствие,\n";
        ss << L"сохранность пакета и его возврат.\n";
        ss << L"Вещи, которые товаровед не примет\n";
        ss << L"по состоянию, качеству или др.\n";
        ss << L"причинам, автоматически\n";
        ss << L"отправляются в категорию «на\n";
        ss << L"утилизацию» и выставляются за\n";
        ss << L"один рубль без составления\n";
        ss << L"перечня.\n\n";
        ss << L"Подписывая данный бланк, я\n";
        ss << L"подтверждаю, что ознакомлен с\n";
        ss << L"условиями и согласен с ними.\n";
        ss << L"Также я соглашаюсь с возможной\n";
        ss << L"утратой пакета или части его\n";
        ss << L"содержимого без претензий с\n";
        ss << L"моей стороны.\n\n";
        ss << L"Мой телефон: " << clientPhone << L"\n";
        ss << L"(телефон необходим для возможности\n";
        ss << L"осуществления удаленной проверки\n";
        ss << L"продаж)\n";
        ss << L"ВНИМАНИЕ – мы не звоним Вам!\n";
        ss << L"Если Вы сдаете вещи впервые, то\n";
        ss << L"заполните данные для регистрации:\n";
        ss << L"ПАСПОРТ:\n";
        ss << L"Серия _______ Номер__________\n";
        ss << L"Выдан _________ от __________\n";
        ss << L"Дата выдачи _________________\n";
        ss << L"Дата рождения _______________\n";
        ss << L"Проживаю ____________________\n";
        ss << L"\n================================\n";
        ss << L"Талон №: " << ticket.ticketNumber << L"\n";
        ss << L"Окно приема: " << ticket.windowNumber << L"\n";
        if (ticket.estimatedWaitTime > 0) {
            ss << L"Ожидание: " << ticket.estimatedWaitTime << L" мин\n";
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
    // Вспомогательный метод: разбиение wstring на строки по символу '\n' для GDI-печати.
    // Логика и математика не меняются — текст уже отформатирован в formatTicket / formatTicketWithExtra.
    std::vector<std::wstring> splitLines(const std::wstring& text) {
        std::vector<std::wstring> lines;
        std::wstringstream ss(text);
        std::wstring line;
        while (std::getline(ss, line, L'\n')) {
            lines.push_back(line);
        }
        return lines;
    }
    // Вспомогательный метод: GDI-печать готового wstring-контента на принтер.
    // Использует стандартный механизм Windows: CreateDC -> StartDoc -> StartPage -> TextOutW -> EndPage -> EndDoc -> DeleteDC.
    // Драйвер принтера САМ конвертирует Unicode в нужную кодовую таблицу и формирует ESC/POS или Raster-команды.
    bool printContentViaGDI(const std::wstring& content, const std::wstring& docName) {
        std::lock_guard<std::mutex> lock(m_printMutex);

        if (!m_initialized) {
            reDetectPrinter();
        }

        if (!m_initialized) {
            g_logger.error(L"ReceiptPrinter::printContentViaGDI: printer not initialized");
            return false;
        }
        g_logger.info(L"ReceiptPrinter::printContentViaGDI: start, docName=" + docName +
            L", printer=" + m_printerName);
        // ШАГ 1: Получение HDC принтера через CreateDC.
        // Драйвер сам применит все настройки страницы (ширина ленты, поля, ориентация).
        HDC hdc = CreateDCW(nullptr, m_printerName.c_str(), nullptr, nullptr);
        if (hdc == nullptr) {
            g_logger.error(L"ReceiptPrinter::printContentViaGDI: CreateDCW failed for printer '" +
                m_printerName + L"', err=" + std::to_wstring(GetLastError()));
            return false;
        }
        g_logger.info(L"ReceiptPrinter::printContentViaGDI: HDC created successfully");
        // ШАГ 2: Логгирование физических размеров страницы (для отладки).
        int widthPx = GetDeviceCaps(hdc, HORZRES);
        int heightPx = GetDeviceCaps(hdc, VERTRES);
        int widthMm = GetDeviceCaps(hdc, HORZSIZE);
        int heightMm = GetDeviceCaps(hdc, VERTSIZE);
        g_logger.info(L"ReceiptPrinter::printContentViaGDI: page size = " +
            std::to_wstring(widthPx) + L"x" + std::to_wstring(heightPx) + L" px, " +
            std::to_wstring(widthMm) + L"x" + std::to_wstring(heightMm) + L" mm");
        // ШАГ 3: Создание шрифта для печати.
        // Используем моноширинный шрифт Courier New — он гарантированно есть в Windows
        // и обеспечивает корректное выравнивание столбцов в талонах.
        // Размер шрифта подбирается так, чтобы ~38 символов помещались в ширину 80 мм ленты.
        // При ширине ~576 px (типично для 80 мм при 203 DPI) и 38 символах -> ~15 px на символ.
        // Для Courier New это соответствует высоте ~10-12 пунктов.
        HFONT hFont = CreateFontW(
            40,                             // высота шрифта в логических единицах (подбирается эмпирически)
            0,                              // средняя ширина
            0,                              // наклон
            0,                              // толщина
            FW_NORMAL,                      // насыщенность
            FALSE,                          // курсив
            FALSE,                          // подчёркивание
            FALSE,                          // зачёркивание
            DEFAULT_CHARSET,                // кодировка (драйвер сам выберет нужную)
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,
            FF_MODERN | FIXED_PITCH,        // моноширинный шрифт
            L"Courier New"
        );
        if (hFont == nullptr) {
            g_logger.error(L"ReceiptPrinter::printContentViaGDI: CreateFontW failed, err=" +
                std::to_wstring(GetLastError()));
            DeleteDC(hdc);
            return false;
        }
        g_logger.info(L"ReceiptPrinter::printContentViaGDI: normal font 'Courier New' (height=40) created");

        // создание жирного шрифта для выделения номера очереди
        HFONT hFontBold = CreateFontW(
            40,                             // высота шрифта увеличена в 2 раза
            0,                              // средняя ширина
            0,                              // наклон
            0,                              // толщина
            FW_BOLD,                        // жирная насыщенность для выделения
            FALSE,                          // курсив
            FALSE,                          // подчёркивание
            FALSE,                          // зачёркивание
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,
            FF_MODERN | FIXED_PITCH,
            L"Courier New"
        );
        if (hFontBold == nullptr) {
            g_logger.error(L"ReceiptPrinter::printContentViaGDI: CreateFontW (bold) failed, err=" +
                std::to_wstring(GetLastError()));
            DeleteObject(hFont);
            DeleteDC(hdc);
            return false;
        }
        g_logger.info(L"ReceiptPrinter::printContentViaGDI: bold font 'Courier New' (height=40, FW_BOLD) created");

        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        g_logger.info(L"ReceiptPrinter::printContentViaGDI: font 'Courier New' selected");

        // ШАГ 4: Начало документа.
        DOCINFOW di = {};
        di.cbSize = sizeof(DOCINFOW);
        di.lpszDocName = docName.c_str();
        di.lpszOutput = nullptr;
        di.lpszDatatype = nullptr;
        int docResult = StartDocW(hdc, &di);
        if (docResult <= 0) {
            g_logger.error(L"ReceiptPrinter::printContentViaGDI: StartDocW failed, err=" +
                std::to_wstring(GetLastError()));
            SelectObject(hdc, hOldFont);
            DeleteObject(hFontBold);
            DeleteObject(hFont);
            DeleteDC(hdc);
            return false;
        }
        g_logger.info(L"ReceiptPrinter::printContentViaGDI: StartDocW succeeded");
        // ШАГ 5: Начало страницы.
        if (StartPage(hdc) <= 0) {
            g_logger.error(L"ReceiptPrinter::printContentViaGDI: StartPage failed, err=" +
                std::to_wstring(GetLastError()));
            AbortDoc(hdc);
            SelectObject(hdc, hOldFont);
            DeleteObject(hFontBold);
            DeleteObject(hFont);
            DeleteDC(hdc);
            return false;
        }
        g_logger.info(L"ReceiptPrinter::printContentViaGDI: StartPage succeeded");
        // ШАГ 6: Разбиение текста на строки и вывод каждой через TextOutW.
        // TextOutW принимает Unicode (wchar_t), драйвер сам конвертирует в кодировку принтера.
        std::vector<std::wstring> lines = splitLines(content);
        int y = 0;
        int lineHeight = 44;  // межстрочный интервал (подбирается под размер шрифта 20 px)
        int linesPrinted = 0;
        for (const auto& line : lines) {
            // Проверка выхода за пределы страницы (для термопринтера с ограниченной длиной ленты).
            if (y + lineHeight > heightPx) {
                g_logger.warning(L"ReceiptPrinter::printContentViaGDI: page overflow at line " +
                    std::to_wstring(linesPrinted) + L", starting new page");
                EndPage(hdc);
                if (StartPage(hdc) <= 0) {
                    g_logger.error(L"ReceiptPrinter::printContentViaGDI: StartPage (2) failed");
                    break;
                }
                y = 0;
            }
            // Выделяем жирным строки с номером очереди ("Номер: " и "Талон №: ")
            bool useBold = (line.find(L"Номер: ") != std::wstring::npos) ||
                (line.find(L"Талон №: ") != std::wstring::npos);

            HFONT hCurrentFont = useBold ? hFontBold : hFont;
            HFONT hPrevFont = (HFONT)SelectObject(hdc, hCurrentFont);

            if (!line.empty()) {
                BOOL textOutResult = TextOutW(hdc, 0, y, line.c_str(), static_cast<int>(line.size()));
                if (!textOutResult) {
                    g_logger.error(L"ReceiptPrinter::printContentViaGDI: TextOutW failed at line " +
                        std::to_wstring(linesPrinted) + L", err=" + std::to_wstring(GetLastError()));
                }
            }
            y += lineHeight;
            ++linesPrinted;
        }
        g_logger.info(L"ReceiptPrinter::printContentViaGDI: printed " +
            std::to_wstring(linesPrinted) + L" lines, final Y=" + std::to_wstring(y));
        // ШАГ 7: Завершение страницы и документа.
        EndPage(hdc);
        int endDocResult = EndDoc(hdc);
        if (endDocResult <= 0) {
            g_logger.error(L"ReceiptPrinter::printContentViaGDI: EndDoc failed, err=" +
                std::to_wstring(GetLastError()));
        }
        else {
            g_logger.info(L"ReceiptPrinter::printContentViaGDI: EndDoc succeeded, document sent to spooler");
        }
        // ШАГ 8: Освобождение ресурсов GDI.
        SelectObject(hdc, hOldFont);
        DeleteObject(hFontBold);
        DeleteObject(hFont);
        DeleteDC(hdc);
        g_logger.info(L"ReceiptPrinter::printContentViaGDI: HDC and font released");
        return endDocResult > 0;
    }
public:
    ReceiptPrinter() : m_initialized(false) {
        DWORD size = 0;
        GetDefaultPrinterW(nullptr, &size);
        if (size > 0) {
            std::wstring printerName(size, L'\0');
            if (GetDefaultPrinterW(&printerName[0], &size)) {
                m_printerName = printerName.c_str();
                m_initialized = true;
            }
        }
        if (!m_initialized) {
            DWORD needed = 0;
            DWORD returned = 0;
            if (EnumPrintersW(PRINTER_ENUM_LOCAL, nullptr, 2, nullptr, 0, &needed, &returned) || GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                std::vector <BYTE > buffer(needed);
                if (EnumPrintersW(PRINTER_ENUM_LOCAL, nullptr, 2, buffer.data(), needed, &needed, &returned)) {
                    if (returned > 0) {
                        PRINTER_INFO_2W* printers = reinterpret_cast <PRINTER_INFO_2W*>(buffer.data());
                        m_printerName = printers[0].pPrinterName;
                        m_initialized = true;
                        g_logger.info(L"ReceiptPrinter: constructor: fallback terminal printer detected: " + m_printerName);
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
        //std::lock_guard<std::mutex> lock(m_printMutex); - у нас уже есть поток
        g_logger.info(L"ReceiptPrinter::printTicket: start, ticket=" + ticket.ticketNumber);
        //if (!m_initialized) {
        //    reDetectPrinter();
        //}
        //if (!m_initialized) {
        //    return saveTicketToFile(ticket, clientName, clientPhone);
        //}
        std::wstring content = formatTicket(ticket, clientName, clientPhone);
        g_logger.info(L"ReceiptPrinter::printTicket: content formatted, length=" +
            std::to_wstring(content.size()) + L" wchar_t");
        // ПЕРЕХОД НА GDI-ПЕЧАТЬ: вызываем универсальный метод printContentViaGDI.
        // Драйвер Windows сам конвертирует Unicode в нужную кодовую таблицу принтера
        // и сформирует корректные ESC/POS или Raster-команды.
        bool printed = printContentViaGDI(content, L"Ticket");
        if (printed) {
            g_logger.info(L"ReceiptPrinter::printTicket: ticket printed successfully via GDI: " +
                ticket.ticketNumber);
            return true;
        }
        else {
            g_logger.error(L"ReceiptPrinter::printTicket: GDI print failed, falling back to file: " +
                ticket.ticketNumber);
            return saveTicketToFile(ticket, clientName, clientPhone);
        }
    }
    bool printTicketWithExtraMessage(const QueueTicket& ticket,
        int clientId,
        const std::wstring& clientName,
        const std::wstring& clientPhone) {
        //std::lock_guard<std::mutex> lock(m_printMutex);
        g_logger.info(L"ReceiptPrinter::printTicketWithExtraMessage: start, ticket=" +
            ticket.ticketNumber + L", clientId=" + std::to_wstring(clientId));
        //if (!m_initialized) {
        //    reDetectPrinter();
        //}
        //if (!m_initialized) {
        //    return saveTicketToFileWithExtra(ticket, clientId, clientName, clientPhone);
        //}
        std::wstring content = formatTicketWithExtra(ticket, clientId, clientName, clientPhone);
        g_logger.info(L"ReceiptPrinter::printTicketWithExtraMessage: content formatted, length=" +
            std::to_wstring(content.size()) + L" wchar_t");
        // ПЕРЕХОД НА GDI-ПЕЧАТЬ: вызываем универсальный метод printContentViaGDI.
        // Драйвер Windows сам конвертирует Unicode в нужную кодовую таблицу принтера
        // и сформирует корректные ESC/POS или Raster-команды.
        bool printed = printContentViaGDI(content, L"TicketWithExtraMessage");
        if (printed) {
            g_logger.info(L"ReceiptPrinter::printTicketWithExtraMessage: ticket printed successfully via GDI: " +
                ticket.ticketNumber);
            return true;
        }
        else {
            g_logger.error(L"ReceiptPrinter::printTicketWithExtraMessage: GDI print failed, falling back to file: " +
                ticket.ticketNumber);
            return saveTicketToFileWithExtra(ticket, clientId, clientName, clientPhone);
        }
    }
};