// payment_manager.cpp
// ИНТЕГРАЦИЯ С DUALCONNECTOR 2.0 ПО HTTP (127.0.0.1:9015)
// Формат запроса/ответа — XML согласно документации DualConnector 2.0 v2.5
// Реальные параметры из Connector.xml: SERVERPORT=9015, COM1, 115200
#include "payment_manager.h"
#include <windows.h>
#include <winhttp.h>
#include <mutex>
#include <chrono>
#include <thread>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

// Реальные параметры из Connector.xml
static const wchar_t* DC_HOST = L"127.0.0.1";
static const int      DC_PORT = 9015;  // SERVERPORT из Connector.xml

PaymentManager::PaymentManager() : m_hasLastSuccessfulTransaction(false) {
    g_logger.info(L"PaymentManager initialized. HTTP integration with DualConnector 2.0 at 127.0.0.1:9015");
}

PaymentManager::~PaymentManager() {
    StopAutoReconciliation();
}


// =============================================================================
// ИЗМЕНЕНО: парсер теперь работает с XML-форматом ответа DualConnector
// Формат ответа: <response><field id="XX">VALUE</field>...</response>
// =============================================================================
std::string PaymentManager::extractField(const std::string& data, const std::string& fieldId) const {
    std::string idAttr = "id=\"" + fieldId + "\"";
    size_t tagStart = data.find(idAttr);
    if (tagStart == std::string::npos) return "";

    size_t valueStart = data.find('>', tagStart);
    if (valueStart == std::string::npos) return "";
    valueStart += 1;

    size_t valueEnd = data.find("</field>", valueStart);
    if (valueEnd == std::string::npos) return "";

    return data.substr(valueStart, valueEnd - valueStart);
}

// =============================================================================
// ИЗМЕНЕНО: формирование запроса в XML-формате согласно документации
// DualConnector 2.0 (п. 3, стр. 12). Обязательные поля для операции "Оплата":
// 00 (Amount), 04 (Currency), 25 (OpCode), 27 (TerminalID), 89 (ModelNo).
// =============================================================================
std::string PaymentManager::BuildSARequest(int operationCode, int amountKopecks,
    const std::string& authCode,
    const std::string& rrn) {
    static unsigned long sessionCounter = 0;
    std::ostringstream sid;
    sid << "KIOSK_" << std::chrono::system_clock::now().time_since_epoch().count()
        << "_" << (++sessionCounter);

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<request>\n";
    xml << "  <field id=\"00\">" << amountKopecks << "</field>\n";
    xml << "  <field id=\"04\">643</field>\n";
    xml << "  <field id=\"25\">" << operationCode << "</field>\n";
    xml << "  <field id=\"27\">99999999</field>\n";
    xml << "  <field id=\"89\">EFT:SW:SmartSaleKiosk;1.0;SA:11.4.2;</field>\n";
    // Для отмен (4, 29, 53) добавляем поля 13 и 14, если они переданы
    if (operationCode == 4 || operationCode == 29 || operationCode == 53) {
        if (!authCode.empty()) {
            xml << "  <field id=\"13\">" << authCode << "</field>\n";
        }
        if (!rrn.empty()) {
            xml << "  <field id=\"14\">" << rrn << "</field>\n";
        }
    }

    xml << "  <timeout>60</timeout>\n";
    xml << "  <sessionID>" << sid.str() << "</sessionID>\n";
    xml << "</request>\n";

    std::string result = xml.str();
    g_logger.info(L"SA XML Request built (len=" + std::to_wstring(result.length()) + L"): " +
        std::wstring(result.begin(), result.end()));
    return result;
}

// Парсинг XML-ответа DualConnector с конвертацией кодировки windows-1251
std::optional<PaymentResponse> PaymentManager::ParseSAResponse(const std::string& rawResponse) {
    // ИСПРАВЛЕНИЕ: Конвертация из windows-1251 для корректного логирования
    int size_needed = MultiByteToWideChar(1251, 0, rawResponse.c_str(), (int)rawResponse.size(), NULL, 0);
    std::wstring wRawResponse(size_needed, 0);
    MultiByteToWideChar(1251, 0, rawResponse.c_str(), (int)rawResponse.size(), &wRawResponse[0], size_needed);

    g_logger.info(L"SA XML Response received (len=" + std::to_wstring(rawResponse.length()) +
        L"): " + wRawResponse);

    PaymentResponse resp;
    resp.rawResponse = rawResponse;
    resp.isSuccess = false;

    if (rawResponse.find("<errorcode>") != std::string::npos) {
        std::string errCode = "unknown";
        size_t s = rawResponse.find("<errorcode>");
        if (s != std::string::npos) {
            s += 11;
            size_t e = rawResponse.find("</errorcode>", s);
            if (e != std::string::npos) errCode = rawResponse.substr(s, e - s);
        }
        g_logger.error(L"DualConnector returned error code: " +
            std::wstring(errCode.begin(), errCode.end()));
        return resp;
    }

    std::string status = extractField(rawResponse, "39");
    if (!status.empty()) {
        resp.isSuccess = (status == "1");
    }
    else { resp.isSuccess = false; }

    resp.amount = extractField(rawResponse, "0");
    resp.authCode = extractField(rawResponse, "13");
    resp.rrn = extractField(rawResponse, "14");
    resp.cardMask = extractField(rawResponse, "10");
    resp.receiptData = extractField(rawResponse, "90");

    if (resp.isSuccess) {
        g_logger.info(L"SA Response parsed: SUCCESS. AuthCode=" +
            std::wstring(resp.authCode.begin(), resp.authCode.end()) +
            L", RRN=" + std::wstring(resp.rrn.begin(), resp.rrn.end()));
    }
    else {
        std::string errDesc = extractField(rawResponse, "19");
        // ИСПРАВЛЕНИЕ: Конвертация из windows-1251 для корректного логирования
        int err_size = MultiByteToWideChar(1251, 0, errDesc.c_str(), (int)errDesc.size(), NULL, 0);
        std::wstring wErrDesc(err_size, 0);
        MultiByteToWideChar(1251, 0, errDesc.c_str(), (int)errDesc.size(), &wErrDesc[0], err_size);
        g_logger.warning(L"SA Response parsed: FAILURE. Description: " + wErrDesc);
    }

    return resp;
}

// Отправка HTTP POST на 127.0.0.1:9015
std::string PaymentManager::SendToTerminal(const std::string& xmlRequest) {
    g_logger.info(L"SendToTerminal: HTTP POST to " + std::wstring(DC_HOST) +
        L":" + std::to_wstring(DC_PORT));

    HINTERNET hSession = nullptr;
    HINTERNET hConnect = nullptr;
    HINTERNET hRequest = nullptr;
    std::string rawResponse;

    hSession = WinHttpOpen(L"SmartSaleKiosk/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        g_logger.error(L"WinHttpOpen failed: " + std::to_wstring(GetLastError()));
        return "<response><field id=\"39\">0</field><field id=\"19\">WinHttpOpen failed</field></response>";
    }

    hConnect = WinHttpConnect(hSession, DC_HOST, DC_PORT, 0);
    if (!hConnect) {
        g_logger.error(L"WinHttpConnect failed: " + std::to_wstring(GetLastError()));
        WinHttpCloseHandle(hSession);
        return "<response><field id=\"39\">0</field><field id=\"19\">WinHttpConnect failed</field></response>";
    }

    hRequest = WinHttpOpenRequest(hConnect, L"POST", nullptr, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        g_logger.error(L"WinHttpOpenRequest failed: " + std::to_wstring(GetLastError()));
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "<response><field id=\"39\">0</field><field id=\"19\">WinHttpOpenRequest failed</field></response>";
    }

    LPCWSTR additionalHeaders =
        L"Content-Type: text/xml;charset=windows-1251\r\n"
        L"Accept: text/xml\r\n";

    BOOL bResults = WinHttpSendRequest(hRequest,
        additionalHeaders,
        (DWORD)-1L,
        (LPVOID)xmlRequest.c_str(),
        (DWORD)xmlRequest.length(),
        (DWORD)xmlRequest.length(),
        0);
    if (!bResults) {
        g_logger.error(L"WinHttpSendRequest failed: " + std::to_wstring(GetLastError()));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "<response><field id=\"39\">0</field><field id=\"19\">WinHttpSendRequest failed</field></response>";
    }

    bResults = WinHttpReceiveResponse(hRequest, nullptr);
    if (!bResults) {
        g_logger.error(L"WinHttpReceiveResponse failed: " + std::to_wstring(GetLastError()));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "<response><field id=\"39\">0</field><field id=\"19\">WinHttpReceiveResponse failed</field></response>";
    }

    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;

        std::string chunk(dwSize + 1, '\0');
        if (!WinHttpReadData(hRequest, (LPVOID)chunk.data(), dwSize, &dwDownloaded)) break;
        chunk.resize(dwDownloaded);
        rawResponse += chunk;
    } while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (rawResponse.empty()) {
        g_logger.warning(L"SendToTerminal: empty response from DualConnector (timeout or connection lost)");
        return "<response><field id=\"39\">0</field><field id=\"19\">Empty response from DualConnector</field></response>";
    }

    g_logger.info(L"SendToTerminal: raw HTTP response received (len=" +
        std::to_wstring(rawResponse.length()) + L")");
    return rawResponse;
}

// =============================================================================
// Получение данных последней успешной транзакции (потокобезопасно)
// =============================================================================
std::tuple<bool, std::string, std::string, std::string> PaymentManager::GetLastTransactionData() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::make_tuple(m_hasLastSuccessfulTransaction, m_lastAuthCode, m_lastRRN, m_lastAmount);
}

// =============================================================================
// ОБРАБОТКА ОПЛАТЫ (OpCode=1)
// =============================================================================
std::optional<PaymentResponse> PaymentManager::ProcessPayment(int amountKopecks) {
    std::lock_guard<std::mutex> lock(m_mutex);
    g_logger.info(L"ProcessPayment started for amount: " + std::to_wstring(amountKopecks) + L" kopecks");

    std::string xmlRequest = BuildSARequest(1, amountKopecks);
    std::string rawResp = SendToTerminal(xmlRequest);
    auto resp = ParseSAResponse(rawResp);

    if (resp && resp->isSuccess) {
        m_lastAuthCode = resp->authCode;
        m_lastRRN = resp->rrn;
        m_lastAmount = std::to_string(amountKopecks);
        m_hasLastSuccessfulTransaction = true;
        g_logger.info(L"Payment successful. State saved for potential cancellation.");
    }
    else {
        m_hasLastSuccessfulTransaction = false;
        g_logger.warning(L"Payment failed. Cancellation state cleared.");
    }
    return resp;
}

// =============================================================================
// ПРОСТАЯ ОТМЕНА ПЛАТЕЖА (OpCode=4) — АВТОМАТИЧЕСКАЯ
// Согласно DualConnector 2.0.pdf: поле 25=4, обязательны поля 00, 13, 14
// =============================================================================
std::optional<PaymentResponse> PaymentManager::ProcessCancellation(int amountKopecks,
    const std::string& authCode,
    const std::string& rrn) {
    std::lock_guard<std::mutex> lock(m_mutex);
    g_logger.info(L"ProcessCancellation: START. Amount=" + std::to_wstring(amountKopecks) +
        L", AuthCode=" + std::wstring(authCode.begin(), authCode.end()) +
        L", RRN=" + std::wstring(rrn.begin(), rrn.end()));
    // Формирование XML-запроса с полями 00, 13, 14 (OpCode=4)
    std::string xmlRequest = BuildSARequest(4, amountKopecks, authCode, rrn);
    g_logger.info(L"ProcessCancellation: XML request built for cancellation (OpCode=4)");
    
    std::string rawResp = SendToTerminal(xmlRequest);
    g_logger.info(L"ProcessCancellation: Response received from terminal");
    
    std::optional<PaymentResponse> resp = ParseSAResponse(rawResp);

    if (resp && resp->isSuccess) {
        g_logger.info(L"ProcessCancellation: SUCCESS. Transaction cancelled successfully.");
        // Очистка состояния после успешной отмены
        m_hasLastSuccessfulTransaction = false;
        m_lastAuthCode.clear();
        m_lastRRN.clear();
        m_lastAmount.clear();
        g_logger.info(L"ProcessCancellation: State cleared after successful cancellation.");
    }
    else {
        std::string errDesc = extractField(rawResp, "19");
        int err_size = MultiByteToWideChar(1251, 0, errDesc.c_str(), (int)errDesc.size(), NULL, 0);
        std::wstring wErrDesc(err_size, 0);
        MultiByteToWideChar(1251, 0, errDesc.c_str(), (int)errDesc.size(), &wErrDesc[0], err_size);
        g_logger.error(L"ProcessCancellation: FAILED. Error: " + wErrDesc);
    }

    return resp;

}

// =============================================================================
// АВАРИЙНАЯ ОТМЕНА ПЛАТЕЖА (OpCode=53) — АВТОМАТИЧЕСКАЯ
// Согласно DualConnector 2.0.pdf: поле 25=53, используются данные последней операции
// =============================================================================
std::optional<PaymentResponse> PaymentManager::ProcessEmergencyCancellation() {
    std::lock_guard<std::mutex> lock(m_mutex);
    g_logger.info(L"ProcessEmergencyCancellation started.");

    if (!m_hasLastSuccessfulTransaction) {
        g_logger.error(L"ProcessEmergencyCancellation: ABORTED. No successful transaction found in memory.");
        return std::nullopt;
    }

    g_logger.info(L"ProcessEmergencyCancellation: Using stored transaction data. Amount=" +
        std::wstring(m_lastAmount.begin(), m_lastAmount.end()) +
        L", AuthCode=" + std::wstring(m_lastAuthCode.begin(), m_lastAuthCode.end()) +
        L", RRN=" + std::wstring(m_lastRRN.begin(), m_lastRRN.end()));

    // Формирование XML-запроса с полями 00, 13, 14 (OpCode=53)
    std::string xmlRequest = BuildSARequest(53, std::stoi(m_lastAmount), m_lastAuthCode, m_lastRRN);
    g_logger.info(L"ProcessEmergencyCancellation: XML request built for emergency cancellation (OpCode=53)");
    
    std::string rawResp = SendToTerminal(xmlRequest);
    g_logger.info(L"ProcessEmergencyCancellation: Response received from terminal");
    
    auto resp = ParseSAResponse(rawResp);

    if (resp && resp->isSuccess) {
        g_logger.info(L"ProcessEmergencyCancellation: SUCCESS. Emergency cancellation completed.");
        // Очистка состояния после успешной аварийной отмены
        m_hasLastSuccessfulTransaction = false;
        m_lastAuthCode.clear();
        m_lastRRN.clear();
        m_lastAmount.clear();
        g_logger.info(L"ProcessEmergencyCancellation: State cleared after successful emergency cancellation.");
    }
    else {
        std::string errDesc = extractField(rawResp, "19");
        int err_size = MultiByteToWideChar(1251, 0, errDesc.c_str(), (int)errDesc.size(), NULL, 0);
        std::wstring wErrDesc(err_size, 0);
        MultiByteToWideChar(1251, 0, errDesc.c_str(), (int)errDesc.size(), &wErrDesc[0], err_size);
        g_logger.error(L"ProcessEmergencyCancellation: FAILED. Error: " + wErrDesc);
    }

    return resp;
}

// Автоматическая сверка итогов (OpCode = 59)
std::optional<PaymentResponse> PaymentManager::ProcessDailyReconciliation() {
    std::lock_guard<std::mutex> lock(m_mutex);
    g_logger.info(L"ProcessDailyReconciliation: START (OpCode=59)");

    // Для сверки итогов сумма обычно передается как 0, терминал сам рассчитывает итоги дня
    std::string xmlRequest = BuildSARequest(59, 0);
    std::string rawResp = SendToTerminal(xmlRequest);
    auto resp = ParseSAResponse(rawResp);

    if (resp && resp->isSuccess) {
        std::string debitAmount = extractField(rawResp, "29");
        std::string debitCount = extractField(rawResp, "30");
        std::string creditAmount = extractField(rawResp, "31");
        std::string creditCount = extractField(rawResp, "32");

        g_logger.info(L"ProcessDailyReconciliation: SUCCESS. "
            L"DebitCount=" + std::wstring(debitCount.begin(), debitCount.end()) +
            L", DebitAmount=" + std::wstring(debitAmount.begin(), debitAmount.end()) +
            L", CreditCount=" + std::wstring(creditCount.begin(), creditCount.end()) +
            L", CreditAmount=" + std::wstring(creditAmount.begin(), creditAmount.end()));
    }
    else {
        g_logger.error(L"ProcessDailyReconciliation: FAILED.");
    }
    return resp;
}

void PaymentManager::ReconciliationWorker() {
    g_logger.info(L"ReconciliationWorker: Background thread started.");

    while (m_reconciliationRunning) {
        // Получаем текущее время
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm now_tm;
        localtime_s(&now_tm, &now_c);

        // Формируем строку текущей даты "YYYY-MM-DD"
        char dateBuf[11];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &now_tm);
        std::string today(dateBuf);

        // Проверяем: если время 21:00 (час == 21, минута == 0) И операция еще не выполнялась сегодня
        if (now_tm.tm_hour == 21 && now_tm.tm_min == 0 && m_lastReconciliationDate != today) {
            g_logger.info(L"ReconciliationWorker: Triggering daily reconciliation at 21:00");
            ProcessDailyReconciliation();
            m_lastReconciliationDate = today; // Запоминаем, что сегодня уже сделали
        }

        // Спим 30 секунд, чтобы не нагружать процессор, но гарантированно поймать смену минуты
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }

    g_logger.info(L"ReconciliationWorker: Background thread stopped.");
}

void PaymentManager::StartAutoReconciliation() {
    if (!m_reconciliationRunning) {
        m_reconciliationRunning = true;
        m_reconciliationThread = std::thread(&PaymentManager::ReconciliationWorker, this);
        g_logger.info(L"StartAutoReconciliation: Background thread started successfully.");
    }
}

void PaymentManager::StopAutoReconciliation() {
    if (m_reconciliationRunning) {
        m_reconciliationRunning = false;
        if (m_reconciliationThread.joinable()) {
            m_reconciliationThread.join();
        }
        g_logger.info(L"StopAutoReconciliation: Background thread joined and stopped.");
    }
}


PaymentManager g_paymentManager;