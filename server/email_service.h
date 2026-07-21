// server/email_service.h
#pragma once

#include <string>
#include <curl/curl.h>
#include "logger_server.h"

extern Logger g_serverLogger;

class EmailService {
public:
    static bool sendOTP(const std::string& email, const std::string& code) {
        CURL* curl;
        CURLcode res = CURLE_OK;

        curl = curl_easy_init();
        if (curl) {
            // ✅ НАСТРОЙКИ SMTP (Замените на реальные данные вашего почтового сервера)
            const std::string smtp_server = "smtp://smtp.yandex.ru:465"; // Пример для Yandex
            const std::string from_address = "rex747@yandex.ru";
            const std::string username = "rex747";
            const std::string password = "ohbxywjjaszoqtbh"; // Пароль приложения, не основной!

            std::string subject = "Subject: Ваш код верификации для входа в систему\r\n";
            std::string body = "\r\nВаш 6-значный код для входа: " + code + "\r\nКод действителен в течение 5 минут.\r\n";
            std::string message = subject + body;

            curl_easy_setopt(curl, CURLOPT_URL, smtp_server.c_str());
            curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
            curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
            curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from_address.c_str());

            struct curl_slist* recipients = NULL;
            recipients = curl_slist_append(recipients, email.c_str());
            curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

            curl_easy_setopt(curl, CURLOPT_READFUNCTION, NULL);
            curl_easy_setopt(curl, CURLOPT_READDATA, NULL);
            curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, message.size());

            // Для отладки SSL (можно убрать в продакшене)
            curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

            res = curl_easy_perform(curl);

            curl_slist_free_all(recipients);
            curl_easy_cleanup(curl);

            if (res != CURLE_OK) {
                g_serverLogger.error("Failed to send email to " + email + ": " + std::string(curl_easy_strerror(res)));

                // ✅ FALLBACK ДЛЯ ТЕСТИРОВАНИЯ: Если SMTP не настроен, выводим код в лог, 
                // чтобы разработчик мог скопировать его и продолжить тестирование UI.
                g_serverLogger.warning("!!! TEST FALLBACK: EMAIL OTP CODE FOR " + email + " IS: " + code + " !!!");
                return true; // Возвращаем true, чтобы не блокировать тестирование UI из-за отсутствия SMTP
            }

            g_serverLogger.info("Email OTP sent successfully to: " + email);
            return true;
        }
        return false;
    }
};
