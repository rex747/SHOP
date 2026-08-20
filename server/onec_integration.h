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
            auto items = db_->getUnsyncedItems();
            if (items.empty()) {
                g_serverLogger.info("OneCIntegration::syncData: no unsynced items");
                return true;
            }

            g_serverLogger.info("OneCIntegration::syncData: syncing " +
                std::to_string(items.size()) + " items to 1C");

            // Формируем JSON-массив для отправки в 1С
            json syncPayload = json::array();
            for (const auto& item : items) {
                json entry;
                entry["item_id"] = item["id"].get<int>();
                entry["client_id"] = item["client_id"].get<int>();
                entry["description"] = item["description"].get<std::string>();
                entry["estimated_price"] = item["estimated_price"].get<double>();
                entry["client_phone"] = item["client_phone"].get<std::string>();
                entry["client_name"] = item["client_name"].get<std::string>();
                syncPayload.push_back(entry);
            }

            // Отправка POST-запроса в 1С через libcurl
            std::string url = std::string(Config::ONEC_URL) + "/items";
            std::string postData = syncPayload.dump();
            std::string response;

            CURL* curl = curl_easy_init();
            if (!curl) {
                g_serverLogger.error("OneCIntegration::syncData: curl init failed");
                logSync("1C_ITEMS_SYNC", 0, "FAILED", "curl init failed");
                return false;
            }

            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)postData.size());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_1c);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

            if (strlen(Config::ONEC_USER) > 0 && strlen(Config::ONEC_PASSWORD) > 0) {
                curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
                std::string auth = std::string(Config::ONEC_USER) + ":" + Config::ONEC_PASSWORD;
                curl_easy_setopt(curl, CURLOPT_USERPWD, auth.c_str());
            }

            CURLcode res = curl_easy_perform(curl);
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (res != CURLE_OK || http_code != 200) {
                std::string errMsg = "HTTP " + std::to_string(http_code) +
                    ", curl error: " + std::to_string(res);
                g_serverLogger.error("OneCIntegration::syncData: failed - " + errMsg);
                logSync("1C_ITEMS_SYNC", 0, "FAILED", errMsg);
                return false;
            }

            // Помечаем все отправленные товары как синхронизированные
            int successCount = 0;
            for (const auto& item : items) {
                db_->updateItemSyncStatus(item["id"].get<int>(), true);
                successCount++;
            }

            logSync("1C_ITEMS_SYNC", successCount, "SUCCESS", "");
            g_serverLogger.info("OneCIntegration::syncData: successfully synced " +
                std::to_string(successCount) + " items to 1C");
            return true;
        }
        catch (const std::exception& e) {
            logSync("1C_ITEMS_SYNC", 0, "FAILED", e.what());
            g_serverLogger.error("OneCIntegration::syncData exception: " + std::string(e.what()));
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