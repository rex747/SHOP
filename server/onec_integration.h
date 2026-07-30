// onec_integration.h
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

#include "database.h"
#include "config_server.h"

using json = nlohmann::json;

class OneCIntegration {
private:
    std::shared_ptr<Database> db_;

public:
    // Внедрение зависимости через конструктор
    explicit OneCIntegration(std::shared_ptr<Database> db) : db_(std::move(db)) {}

    bool initialize() {
        return true; // Инициализация теперь полностью в Database::initialize()
    }

    void logSync(const std::string& syncType, int recordsCount, const std::string& status, const std::string& errorMessage) {
        // Исправление: использование метода экземпляра вместо глобальной переменной
        db_->logSync(syncType, recordsCount, status, errorMessage);
    }

    bool syncData() {
        try {
            // Исправление: вызов метода экземпляра вместо статического вызова
            auto items = db_->getUnsyncedItems();
            if (items.empty()) {
                return true;
            }

            int successCount = 0;
            for (const auto& item : items) {
                


                bool mock1cSuccess = true;

                if (mock1cSuccess) {
                    // Исправление: вызов метода экземпляра
                    db_->updateItemSyncStatus(item["id"].get<int>(), true);
                    successCount++;
                }
            }

            logSync("1C_ITEMS_SYNC", successCount, "SUCCESS", "");
            return true;
        }
        catch (const std::exception& e) {
            logSync("1C_ITEMS_SYNC", 0, "FAILED", e.what());
            return false;
        }
    }
    // метод полукчения данных о продажах пользователя
    static size_t write_callback_1c(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* s = static_cast<std::string*>(userdata);
        s->append(ptr, size * nmemb);
        return size * nmemb;
    }

    json getClientSales(int clientId) {
        // Формируем URL для 1С (предполагаем REST-эндпоинт /sales?client_id=...)
        std::string url = std::string(Config::ONEC_URL) + "/sales?client_id=" + std::to_string(clientId);
        g_serverLogger.info("Requesting sales from 1C: " + url);

        CURL* curl = curl_easy_init();
        if (!curl) {
            g_serverLogger.error("Failed to init curl for 1C request");
            return json::object();
        }

        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_1c);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);  // В продакшене лучше 1, но для теста можно 0
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        // Базовая авторизация, если требуется
        if (strlen(Config::ONEC_USER) > 0 && strlen(Config::ONEC_PASSWORD) > 0) {
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            curl_easy_setopt(curl, CURLOPT_USERPWD, (std::string(Config::ONEC_USER) + ":" + std::string(Config::ONEC_PASSWORD)).c_str());
        }

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK || http_code != 200) {
            g_serverLogger.error("1C request failed: code=" + std::to_string(http_code) + ", error=" + std::to_string(res));
            return json::object();
        }

        g_serverLogger.info("1C response received, size=" + std::to_string(response.size()));
        try {
            json j = json::parse(response);
            // Если 1C возвращает массив в поле "sales", приводим к нужному формату
            if (j.is_array()) {
                json result;
                result["sales"] = j;
                return result;
            }
            if (j.contains("sales") && j["sales"].is_array()) {
                return j;
            }
            // Или если 1C возвращает сразу массив, оборачиваем
            return j;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("Failed to parse 1C response: " + std::string(e.what()));
            g_serverLogger.error("Raw response: " + response);
            return json::object();
        }
    }
};