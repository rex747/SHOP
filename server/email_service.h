// server/email_service.h
#pragma once

#include <string>
#include <cstring>
#include <curl/curl.h>
#include "logger_server.h"

extern Logger g_serverLogger;

// Структура и callback для SMTP (обязательно)
struct UploadStatus {
    const char* readptr;
    size_t sizeleft;
};

static size_t payload_source(void* ptr, size_t size, size_t nmemb, void* userp) {
    struct UploadStatus* upload_ctx = static_cast<struct UploadStatus*>(userp);
    size_t buffer_size = size * nmemb;
    if (buffer_size == 0 || upload_ctx->sizeleft == 0) {
        g_serverLogger.debug("[EmailService::payload_source] Данные закончились");
        return 0;
    }
    size_t copy_this_much = std::min(upload_ctx->sizeleft, buffer_size);
    std::memcpy(ptr, upload_ctx->readptr, copy_this_much);
    upload_ctx->readptr += copy_this_much;
    upload_ctx->sizeleft -= copy_this_much;
    g_serverLogger.debug("[EmailService::payload_source] Отправлено " + std::to_string(copy_this_much) + " байт");
    return copy_this_much;
}

class EmailService {
public:
    static bool sendOTP(const std::string& email, const std::string& code) {
        g_serverLogger.info("[EmailService] === НАЧАЛО ОТПРАВКИ OTP ===");
        g_serverLogger.info("[EmailService] Целевой email: " + email);
        g_serverLogger.info("[EmailService] Код: " + code);

        CURL* curl = curl_easy_init();
        if (!curl) {
            g_serverLogger.error("[EmailService] curl_easy_init() FAILED!");
            return false;
        }
        g_serverLogger.info("[EmailService] curl_easy_init() OK");

        const std::string smtp_server = "smtps://smtp.yandex.ru:465";
        const std::string from_address = "rex747@yandex.ru";
        const std::string username = "rex747";

        // !!! ВАЖНО: замените YOUR_OAUTH_TOKEN на действительный OAuth-токен,
        // полученный через OAuth 2.0 (ClientID: 35ecdbd208a246349f60e064aabb0d04).
        // Токен должен иметь права на отправку почты.
        const std::string oauth_token = "8917023fff0a4cff8370a2635b0fa22d";

        g_serverLogger.info("[EmailService] SMTP: " + smtp_server + ", From: " + from_address);
        g_serverLogger.info("[EmailService] Используется аутентификация XOAUTH2 (токен получен)");

        // Корректное SMTP-сообщение
        std::string subject = "Subject: Ваш код верификации\r\n";
        std::string from_hdr = "From: " + from_address + "\r\n";
        std::string to_hdr = "To: " + email + "\r\n";
        std::string body = "\r\nВаш код: " + code + "\r\nДействителен 5 минут.\r\n";
        std::string message = subject + from_hdr + to_hdr + body;

        g_serverLogger.info("[EmailService] Payload size: " + std::to_string(message.length()) + " bytes");

        struct UploadStatus upload_ctx = { message.c_str(), message.length() };

        // Настройки curl для XOAUTH2
        curl_easy_setopt(curl, CURLOPT_URL, smtp_server.c_str());
        curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
        // Вместо пароля передаём OAuth-токен для механизма XOAUTH2
        curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, oauth_token.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from_address.c_str());

        struct curl_slist* recipients = curl_slist_append(nullptr, email.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
        curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);  // Максимум диагностики

        g_serverLogger.info("[EmailService] Запуск curl_easy_perform() с XOAUTH2...");
        CURLcode res = curl_easy_perform(curl);

        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

        if (res != CURLE_OK) {
            std::string err = curl_easy_strerror(res);
            g_serverLogger.error("[EmailService] SMTP FAILED! CURLcode=" + std::to_string(res) + ", msg=" + err);
            g_serverLogger.error("[EmailService] Response code: " + std::to_string(response_code));
            g_serverLogger.error("Проверьте: 1. OAuth-токен действителен. 2. Токен имеет права на отправку почты. 3. Сертификаты.");
            curl_slist_free_all(recipients);
            curl_easy_cleanup(curl);
            return false;
        }

        g_serverLogger.info("[EmailService] SUCCESS! OTP sent to " + email + " (code " + std::to_string(response_code) + ")");
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
        return true;
    }
};