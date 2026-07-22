// server/email_service.h
#pragma once

#include <string>
#include <cstring>      // Для std::memcpy
#include <curl/curl.h>
#include "logger_server.h"

extern Logger g_serverLogger;

// -----------------------------------------------------------------------------
// Структура и callback-функция для передачи данных в libcurl (ТРЕБОВАНИЕ SMTP)
// libcurl не может использовать POSTFIELDS для SMTP, ему нужен поток чтения.
// -----------------------------------------------------------------------------
struct UploadStatus {
    const char* readptr;
    size_t sizeleft;
};

static size_t payload_source(void* ptr, size_t size, size_t nmemb, void* userp) {
    struct UploadStatus* upload_ctx = static_cast<struct UploadStatus*>(userp);
    size_t buffer_size = size * nmemb;

    if (buffer_size == 0 || upload_ctx->sizeleft == 0) {
        return 0; // Данные закончились
    }

    size_t copy_this_much = upload_ctx->sizeleft;
    if (copy_this_much > buffer_size) {
        copy_this_much = buffer_size;
    }

    std::memcpy(ptr, upload_ctx->readptr, copy_this_much);
    upload_ctx->readptr += copy_this_much;
    upload_ctx->sizeleft -= copy_this_much;

    return copy_this_much;
}

// -----------------------------------------------------------------------------
// Класс сервиса отправки электронной почты
// -----------------------------------------------------------------------------
class EmailService {
public:
    static bool sendOTP(const std::string& email, const std::string& code) {
        g_serverLogger.info("[EmailService] === НАЧАЛО ОТПРАВКИ OTP ===");
        g_serverLogger.info("[EmailService] Целевой email: " + email);
        g_serverLogger.info("[EmailService] Код для отправки: " + code);

        CURL* curl = curl_easy_init();
        if (!curl) {
            g_serverLogger.error("[EmailService] КРИТИЧЕСКАЯ ОШИБКА: curl_easy_init() вернул nullptr! libcurl не инициализирован.");
            return false;
        }
        g_serverLogger.info("[EmailService] curl_easy_init() успешен. HANDLE создан.");

        // ИСПРАВЛЕНИЕ: Используем smtps:// для порта 465 (Implicit SSL/TLS с самого начала)
        const std::string smtp_server = "smtps://smtp.yandex.ru:465";
        const std::string from_address = "rex747@yandex.ru";
        const std::string username = "rex747";
        const std::string password = "ohbxywjjaszoqtbh"; // Пароль приложения Яндекс

        g_serverLogger.info("[EmailService] SMTP сервер: " + smtp_server);
        g_serverLogger.info("[EmailService] Отправитель (MAIL FROM): " + from_address);
        g_serverLogger.info("[EmailService] Имя пользователя (AUTH): " + username);
        g_serverLogger.info("[EmailService] Длина пароля приложения: " + std::to_string(password.length()) + " символов");

        // Формирование корректного SMTP-сообщения с заголовками и телом
        // RFC 5322 требует: заголовки, пустая строка, тело
        std::string subject = "Subject: Ваш код верификации для входа в систему\r\n";
        std::string body = "\r\nВаш 6-значный код для входа: " + code + "\r\nКод действителен в течение 5 минут.\r\n";
        std::string message = subject + body;

        g_serverLogger.info("[EmailService] Подготовка payload завершена.");
        g_serverLogger.info("[EmailService] Размер SMTP-сообщения: " + std::to_string(message.length()) + " байт");
        g_serverLogger.info("[EmailService] Содержимое сообщения (escaped): " + subject + "\\r\\n" + body);

        // ИСПРАВЛЕНИЕ: Инициализация контекста для callback-чтения
        struct UploadStatus upload_ctx;
        upload_ctx.readptr = message.c_str();
        upload_ctx.sizeleft = message.length();

        g_serverLogger.info("[EmailService] UploadStatus инициализирован. readptr=" +
            std::to_string(reinterpret_cast<uintptr_t>(upload_ctx.readptr)) +
            ", sizeleft=" + std::to_string(upload_ctx.sizeleft));

        // Настройка базовых параметров libcurl
        curl_easy_setopt(curl, CURLOPT_URL, smtp_server.c_str());
        g_serverLogger.info("[EmailService] CURLOPT_URL установлен: " + smtp_server);

        curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
        g_serverLogger.info("[EmailService] CURLOPT_USERNAME установлен.");

        curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
        g_serverLogger.info("[EmailService] CURLOPT_PASSWORD установлен.");

        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from_address.c_str());
        g_serverLogger.info("[EmailService] CURLOPT_MAIL_FROM установлен: " + from_address);

        // Настройка получателя
        struct curl_slist* recipients = NULL;
        recipients = curl_slist_append(recipients, email.c_str());
        if (!recipients) {
            g_serverLogger.error("[EmailService] КРИТИЧЕСКАЯ ОШИБКА: curl_slist_append() вернул nullptr! Не удалось добавить получателя: " + email);
            curl_easy_cleanup(curl);
            return false;
        }
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
        g_serverLogger.info("[EmailService] Получатель добавлен в список RCPT: " + email);

        // ИСПРАВЛЕНИЕ: Использование READFUNCTION вместо POSTFIELDS (КРИТИЧЕСКИ ВАЖНО для SMTP)
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
        curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        g_serverLogger.info("[EmailService] CURLOPT_READFUNCTION, CURLOPT_READDATA, CURLOPT_UPLOAD установлены.");

        // УДАЛЕНО: CURLOPT_POSTFIELDS и CURLOPT_POSTFIELDSIZE (они вызывают CURLE_READ_ERROR в SMTP)

        // Настройки SSL/TLS для безопасного соединения
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        g_serverLogger.info("[EmailService] CURLOPT_USE_SSL = CURLUSESSL_ALL");

        // ИСПРАВЛЕНИЕ: Принудительно задаем TLS 1.2 или выше для совместимости с Яндекс.SMTP
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
        g_serverLogger.info("[EmailService] CURLOPT_SSLVERSION = CURL_SSLVERSION_TLSv1_2");

        // ИСПРАВЛЕНИЕ: Включаем верификацию сертификата для реальной отправки
        // (если требуется тестирование с самоподписанными сертификатами — временно 0L, но для продакшена должно быть 1L)
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        g_serverLogger.info("[EmailService] CURLOPT_SSL_VERIFYPEER = 1L, CURLOPT_SSL_VERIFYHOST = 2L");

        // ИСПРАВЛЕНИЕ: Устанавливаем таймауты соединения и передачи
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);  // 10 секунд на установку TCP-соединения
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);         // 30 секунд на всю операцию
        g_serverLogger.info("[EmailService] CURLOPT_CONNECTTIMEOUT = 10s, CURLOPT_TIMEOUT = 30s");

        // Включение подробного логирования libcurl (выводится в stderr)
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        g_serverLogger.info("[EmailService] CURLOPT_VERBOSE = 1L (подробный вывод libcurl в stderr)");

        g_serverLogger.info("[EmailService] === ЗАПУСК curl_easy_perform() ===");
        CURLcode res = curl_easy_perform(curl);

        // ИСПРАВЛЕНИЕ: Получаем детальную диагностику результата
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        char* effective_url = nullptr;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url);

        g_serverLogger.info("[EmailService] curl_easy_perform() завершен. Код возврата CURLcode: " + std::to_string(res));

        if (res != CURLE_OK) {
            std::string err_msg = curl_easy_strerror(res);
            g_serverLogger.error("[EmailService] ============================================");
            g_serverLogger.error("[EmailService] ОШИБКА ОТПРАВКИ SMTP: curl_easy_perform() НЕ УСПЕШЕН");
            g_serverLogger.error("[EmailService] CURLcode: " + std::to_string(res));
            g_serverLogger.error("[EmailService] Описание ошибки: " + err_msg);
            g_serverLogger.error("[EmailService] Эффективный URL: " + std::string(effective_url ? effective_url : "N/A"));
            g_serverLogger.error("[EmailService] ============================================");
            g_serverLogger.error("[EmailService] Возможные причины:");
            g_serverLogger.error("  1. Неверный пароль приложения Яндекс (проверьте в настройках безопасности Яндекс.ID)");
            g_serverLogger.error("  2. Двухфакторная аутентификация отключена — для пароля приложения требуется 2FA");
            g_serverLogger.error("  3. Брандмауэр или NAT блокирует исходящее соединение на порт 465");
            g_serverLogger.error("  4. Сетевой таймаут — сервер недоступен или задержка превышает 30 секунд");
            g_serverLogger.error("  5. Несовместимость версии TLS (сервер требует TLS 1.2+, клиент использует старую)");
            g_serverLogger.error("  6. Сертификат smtp.yandex.ru не прошел верификацию (CA-certificates не установлены)");
            g_serverLogger.error("[EmailService] ============================================");

            // ИСПРАВЛЕНИЕ: УБРАН TEST FALLBACK. При ошибке SMTP возвращаем false.
            // Вызывающий код ОБЯЗАН обработать false и показать пользователю сообщение об ошибке.
            g_serverLogger.error("[EmailService] Отправка ОТМЕНЕНА. Возвращаем false.");

            curl_slist_free_all(recipients);
            curl_easy_cleanup(curl);
            g_serverLogger.info("[EmailService] Ресурсы curl освобождены.");
            return false;
        }

        // Успешное выполнение
        g_serverLogger.info("[EmailService] ============================================");
        g_serverLogger.info("[EmailService] Email OTP УСПЕШНО отправлен на: " + email);
        g_serverLogger.info("[EmailService] SMTP response code: " + std::to_string(http_code));
        g_serverLogger.info("[EmailService] ============================================");

        // Очистка ресурсов
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
        g_serverLogger.info("[EmailService] Ресурсы curl освобождены.");
        g_serverLogger.info("[EmailService] === ЗАВЕРШЕНИЕ ОТПРАВКИ OTP ===");

        return true;
    }
};