// onec_integration.h
#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include "database.h"
#include "config_server.h"
#include "logger_server.h"

extern Logger g_serverLogger;

using json = nlohmann::json;

class OneCIntegration {
private:
    std::string m_baseUrl;
    std::string m_user;
    std::string m_password;

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    json sendRequest(const std::string& endpoint, const json& data, const std::string& method = "POST") {
        CURL* curl;
        CURLcode res;
        std::string readBuffer;

        curl = curl_easy_init();
        if (!curl) {
            g_serverLogger.error("Failed to initialize CURL");
            return json{ {"error", "CURL initialization failed"} };
        }

        std::string url = m_baseUrl + endpoint;
        std::string postData = data.dump();

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERNAME, m_user.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, m_password.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // For development
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            g_serverLogger.error(std::string("CURL error: ") + curl_easy_strerror(res));
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return json{ {"error", curl_easy_strerror(res)} };
        }

        long responseCode;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (responseCode != 200 && responseCode != 201) {
            g_serverLogger.error("1C HTTP error: " + std::to_string(responseCode));
            return json{ {"error", "HTTP " + std::to_string(responseCode)} };
        }

        try {
            return json::parse(readBuffer);
        }
        catch (const json::exception& e) {
            g_serverLogger.error(std::string("JSON parse error: ") + e.what());
            return json{ {"raw_response", readBuffer} };
        }
    }

    void logSync(const std::string& type, int count, const std::string& status,
        const std::string& error) {
        try {
            pqxx::work txn{ *g_dbConnection };
            int64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000;

            txn.exec(
                "INSERT INTO sync_log (sync_type, records_count, status, error_message, created_at) "
                "VALUES ($1, $2, $3, $4, $5)",
                pqxx::params{ type, count, status, error, now }
            );
            txn.commit();

        }
        catch (const std::exception& e) {
            g_serverLogger.error(std::string("logSync error: ") + e.what());
        }
    }

    

public:
    bool initialize() {
        m_baseUrl = Config::ONEC_URL;
        m_user = Config::ONEC_USER;
        m_password = Config::ONEC_PASSWORD;

        curl_global_init(CURL_GLOBAL_DEFAULT);

        g_serverLogger.info("1C integration initialized");
        return true;
    }

    bool syncData() {
        g_serverLogger.info("Starting 1C sync");

        // Get unsynced items
        auto items = Database::getUnsyncedItems();

        if (items.empty()) {
            g_serverLogger.info("No items to sync");
            return true;
        }

        // Prepare batch for 1C
        json batch;
        batch["items"] = json::array();

        for (const auto& item : items) {
            json j;
            j["external_id"] = item["id"];
            j["client_phone"] = item["client_phone"];
            j["client_name"] = item["client_name"];
            j["description"] = item["description"];
            j["estimated_price"] = item["estimated_price"];
            j["created_at"] = std::chrono::system_clock::now().time_since_epoch().count() / 1000;

            batch["items"].push_back(j);
        }

        // Send to 1C
        auto response = sendRequest("/items/batch", batch);

        if (response.contains("success") && response["success"].get<bool>()) {
            // Mark items as synced
            for (const auto& item : items) {
                Database::updateItemSyncStatus(item["id"].get<int>(), true);
            }

            g_serverLogger.info("Synced " + std::to_string(items.size()) + " items to 1C");

            // Log sync
            logSync("items", items.size(), "success", "");
            return true;
        }
        else {
            std::string error = response.contains("error") ? response["error"].get<std::string>() : "Unknown error";
            g_serverLogger.error("1C sync failed: " + error);

            logSync("items", items.size(), "failed", error);
            return false;
        }
    }

    bool pushClientData(const std::string& phone, const std::string& name,
        const std::string& email) {
        json data;
        data["phone"] = phone;
        data["name"] = name;
        data["email"] = email;

        auto response = sendRequest("/clients", data);

        return response.contains("success") && response["success"].get<bool>();
    }

    bool getSalesHistory(const std::string& phone) {
        json params;
        params["phone"] = phone;

        auto response = sendRequest("/sales/history", params, "GET");

        return response.contains("sales");
    }

    ~OneCIntegration() {
        curl_global_cleanup();
    }

};
