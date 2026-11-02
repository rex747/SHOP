// payment_manager.h
#pragma once
#include <string>
#include <optional>
#include <mutex>
#include <tuple>
#include "logger.h"

extern Logger g_logger;

struct PaymentResponse {
    bool isSuccess;
    std::string amount;
    std::string authCode;
    std::string rrn;
    std::string cardMask;
    std::string receiptData; // Поле 90 для печати
    std::string rawResponse;
};

class PaymentManager {
private:
    mutable std::mutex m_mutex;

    // Данные последней успешной операции для отмены и аварийной отмены
    std::string m_lastAuthCode;
    std::string m_lastRRN;
    std::string m_lastAmount;
    bool m_hasLastSuccessfulTransaction;

    // Переменные для автоматической сверки итогов
    std::atomic<bool> m_reconciliationRunning{ false };
    std::thread m_reconciliationThread;
    std::string m_lastReconciliationDate; // Формат "YYYY-MM-DD"

    // Внутренние методы формирования и отправки запросов
    std::string BuildSARequest(int operationCode, int amountKopecks, const std::string& authCode = "", const std::string& rrn = "");
    std::optional<PaymentResponse> ParseSAResponse(const std::string& rawResponse);
    std::string SendToTerminal(const std::string& request);

    // Метод фонового потока для проверки времени
    void ReconciliationWorker();

public:
    PaymentManager();
    ~PaymentManager(); // Гарантирует корректную остановку потока при выходе

    // =====================================================================
    // ИСПРАВЛЕНИЕ E0265: Метод extractField перемещен в public секцию.
    // Это позволяет main_window.h извлекать описание ошибки (поле 19) 
    // из сырого ответа терминала для отображения пользователю.
    // Метод потокобезопасен (не изменяет состояние объекта, помечен const).
    // =====================================================================
    std::string extractField(const std::string& data, const std::string& fieldId) const;

    // Получение данных последней успешной транзакции (потокбезопасно)
    std::tuple<bool, std::string, std::string, std::string> GetLastTransactionData() const;

    // Поле 25 = 1 (Оплата)
    std::optional<PaymentResponse> ProcessPayment(int amountKopecks);

    // Поле 25 = 4 (Отмена)
    std::optional<PaymentResponse> ProcessCancellation(int amountKopecks, const std::string& authCode, const std::string& rrn);

    // Поле 25 = 53 (Аварийная отмена)
    std::optional<PaymentResponse> ProcessEmergencyCancellation();

    // Автоматическая сверка итогов (OpCode = 59)
    std::optional<PaymentResponse> ProcessDailyReconciliation();
    void StartAutoReconciliation();
    void StopAutoReconciliation();

};

extern PaymentManager g_paymentManager;