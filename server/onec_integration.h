// onec_integration.h
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <mutex>      // === ИЗМЕНЕНИЕ 1: добавлен для std::mutex, std::lock_guard ===
#include <thread>     // === ИЗМЕНЕНИЕ 1: добавлен для std::this_thread::sleep_for ===
#include <chrono>     // === ИЗМЕНЕНИЕ 1: добавлен для std::chrono::milliseconds ===
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include "database.h"
#include "config_server.h"
using json = nlohmann::json;

class OneCIntegration {
private:
    std::shared_ptr<Database> db_;

    // === ИЗМЕНЕНИЕ 2: мьютекс для гарантии строго однопоточного выполнения
    // синхронизации. Защищает от одновременного вызова syncData() из
    // фонового потока startOneCSyncMonitor и HTTP-обработчика
    // handleOneCSync (POST /api/v1/onec/sync).
    // Без этого мьютекса два параллельных вызова отправят одни и те же
    // товары в 1С дважды и создадут гонку в updateItemSyncStatus. ===
    std::mutex syncMutex_;

public:
    explicit OneCIntegration(std::shared_ptr<Database> db) : db_(std::move(db)) {}
    bool initialize() { return true; }

    void logSync(const std::string& syncType, int recordsCount,
        const std::string& status, const std::string& errorMessage) {
        db_->logSync(syncType, recordsCount, status, errorMessage);
    }

    bool syncData() {
        // === ИЗМЕНЕНИЕ 3: захват мьютекса на ВСЁ время выполнения
        // синхронизации. Если другой поток уже выполняет syncData(),
        // текущий вызов будет заблокирован до его завершения.
        // Это гарантирует, что пачки отправляются строго последовательно,
        // одна за другой, из единственного активного потока. ===
        std::lock_guard<std::mutex> lock(syncMutex_);

        try {
            // ШАГ 1: Синхронизация ТОВАРОВ из PostgreSQL в 1С
            auto items = db_->getUnsyncedItems();
            size_t syncedTotal = 0;
            size_t failedTotal = 0;

            if (!items.empty()) {
                const size_t BATCH_SIZE = 50;
                const size_t total = items.size();

                g_serverLogger.info("OneCIntegration::syncData: starting sync of " +
                    std::to_string(total) + " items to 1C");

                for (size_t batchStart = 0; batchStart < total; batchStart += BATCH_SIZE) {
                    size_t batchEnd = std::min(batchStart + BATCH_SIZE, total);
                    size_t batchSize = batchEnd - batchStart;

                    json syncPayload = json::array();
                    std::vector<int> batchItemIds;

                    for (size_t i = batchStart; i < batchEnd; ++i) {
                        const auto& item = items[i];
                        json entry;
                        entry["item_id"] = item["id"].get<int>();
                        entry["client_id"] = item["client_id"].get<int>();
                        entry["description"] = sanitizeUTF8(item["description"].get<std::string>());
                        entry["client_phone"] = sanitizeUTF8(item["client_phone"].get<std::string>());
                        entry["client_name"] = sanitizeUTF8(item["client_name"].get<std::string>());
                        entry["barcode"] = sanitizeUTF8(item.value("barcode", std::string("")));
                        entry["estimated_price"] = item["estimated_price"].get<double>();
                        entry["quantity"] = item.value("quantity", 1);
                        entry["sold_quantity"] = item.value("sold_quantity", 0);
                        entry["available_quantity"] = item.value("quantity", 1) - item.value("sold_quantity", 0);
                        entry["unit"] = "шт.";
                        entry["unit_code"] = "796";
                        entry["price"] = item["estimated_price"].get<double>();
                        entry["retail_price"] = item["estimated_price"].get<double>();
                        syncPayload.push_back(entry);
                        batchItemIds.push_back(item["id"].get<int>());
                    }

                    // ИСПРАВЛЕНО: Config::ONEC_URL теперь ".../kiosk_sync"
                    // Конкатенация "/items" дает корректный URL ".../kiosk_sync/items"
                    std::string url = std::string(Config::ONEC_URL) + "/items";

                    g_serverLogger.info("OneCIntegration::syncData: POST to 1C: " + url +
                        " (batch size: " + std::to_string(batchSize) + ")");

                    std::string postData = syncPayload.dump();
                    std::string response;

                    CURL* curl = curl_easy_init();
                    if (!curl) {
                        g_serverLogger.error("OneCIntegration::syncData: Failed to init curl for 1C POST request");
                        failedTotal += batchSize;
                        continue;
                    }

                    struct curl_slist* headers = nullptr;
                    headers = curl_slist_append(headers, "Content-Type: application/json");
                    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                    curl_easy_setopt(curl, CURLOPT_POST, 1L);
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)postData.size());
                    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
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
                        // ДЕТАЛЬНОЕ ЛОГГИРОВАНИЕ: фиксируем код ответа и URL
                        g_serverLogger.error("OneCIntegration::syncData: 1C POST failed: code=" +
                            std::to_string(http_code) + ", curl_error=" + std::to_string(res) +
                            ", url=" + url + ", response=" + response.substr(0, 200));
                        failedTotal += batchSize;
                        continue;
                    }

                    int batchSuccess = 0;
                    for (int itemId : batchItemIds) {
                        if (db_->updateItemSyncStatus(itemId, true)) {
                            batchSuccess++;
                        }
                    }
                    syncedTotal += batchSuccess;

                    // === ИЗМЕНЕНИЕ 4: пауза 1000 мс МЕЖДУ пачками.
                    // Пауза выполняется ТОЛЬКО если впереди есть ещё
                    // хотя бы одна пачка. После последней пачки пауза
                    // не нужна — это не «между», а «после». ===
                    if (batchEnd < total) {
                        g_serverLogger.info("OneCIntegration::syncData: batch sent, "
                            "pausing 1000 ms before next batch (batchStart=" +
                            std::to_string(batchStart) + ", batchEnd=" +
                            std::to_string(batchEnd) + ", total=" +
                            std::to_string(total) + ")");
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    }
                }

                db_->logSync("1C_ITEMS_SYNC", static_cast<int>(syncedTotal),
                    (failedTotal == 0) ? "SUCCESS" : "PARTIAL",
                    (failedTotal > 0) ? ("Failed: " + std::to_string(failedTotal)) : "");
            }

            // ШАГ 2: ОБРАТНАЯ СИНХРОНИЗАЦИЯ ПРОДАЖ из 1С в PostgreSQL
            syncSalesFrom1C();

            g_serverLogger.info("OneCIntegration::syncData: FINISHED - items synced=" +
                std::to_string(syncedTotal) + ", items failed=" + std::to_string(failedTotal));
            return (failedTotal == 0);
        }
        catch (const std::exception& e) {
            db_->logSync("1C_ITEMS_SYNC", 0, "FAILED", e.what());
            g_serverLogger.error("OneCIntegration::syncData exception: " + std::string(e.what()));
            return false;
        }
    }

    void syncSalesFrom1C() {
        g_serverLogger.info("syncSalesFrom1C: starting reverse sync of sales from 1C to PostgreSQL");
        try {
            json clients = db_->getAllClients(true);
            if (!clients.is_array() || clients.empty()) {
                g_serverLogger.info("syncSalesFrom1C: no clients found");
                return;
            }
            int totalSalesSynced = 0;
            int totalSalesSkipped = 0;
            int totalSalesFailed = 0;

            for (const auto& client : clients) {
                if (!client.contains("id") || !client["id"].is_number()) {
                    continue;
                }
                int clientId = client["id"].get<int>();

                // Получаем продажи из 1С для конкретного комитента/киоска
                json salesData = getClientSales(clientId);
                if (salesData.empty() || !salesData.contains("sales") || !salesData["sales"].is_array()) {
                    continue;
                }
                const auto& salesArray = salesData["sales"];
                if (salesArray.empty()) {
                    continue;
                }

                g_serverLogger.info("syncSalesFrom1C: client " + std::to_string(clientId) +
                    " has " + std::to_string(salesArray.size()) + " sales in 1C");

                for (const auto& sale : salesArray) {
                    std::string sku = sale.value("sku", "");
                    std::string receiptNo = sale.value("receipt_no", "");
                    double price = sale.value("price", 0.0);

                    if (sku.empty() || receiptNo.empty()) {
                        totalSalesSkipped++;
                        continue;
                    }

                    json itemData = db_->findItemByBarcode(sku);
                    if (itemData.contains("error")) {
                        g_serverLogger.warning("syncSalesFrom1C: item not found for sku=" + sku);
                        totalSalesSkipped++;
                        continue;
                    }

                    int itemId = itemData["id"].get<int>();
                    // ИСПРАВЛЕНИЕ: idempotency_key строится от ФИЗИЧЕСКОЙ продажи (receipt + sku),
                    // без client_id итерации внешнего цикла. Одна и та же продажа,
                    // возвращённая 1С при переборе любого клиента, всегда даёт ОДИН И ТОТ ЖЕ ключ,
                    // что гарантирует срабатывание проверки на дубликат в registerItemSale
                    // на следующем цикле синхронизации.
                    std::string idempotencyKey = "1C_" + receiptNo + "_" + sku;

                    json saleResult = db_->registerItemSale(
                        itemId,
                        price,
                        "1C_SYNC",
                        receiptNo,
                        sku,
                        idempotencyKey
                    );

                    if (saleResult.value("success", false)) {
                        if (saleResult.value("duplicate", false)) {
                            totalSalesSkipped++;
                        }
                        else {
                            totalSalesSynced++;
                            g_serverLogger.info("syncSalesFrom1C: registered sale for itemId=" +
                                std::to_string(itemId) + ", receipt=" + receiptNo);
                        }
                    }
                    else {
                        // ========================================================================
                        // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ ЛОГИКИ СИНХРОНИЗАЦИИ
                        // ========================================================================
                        // Если регистрация отклонена из-за отсутствия остатка ("No available units"),
                        // это НЕ является ошибкой синхронизации. Это означает, что товар уже был 
                        // списан ранее (через эту же синхронизацию в прошлом цикле, через терминал 
                        // самообслуживания или вручную). 
                        // Мы переводим это состояние из категории FAILED в категорию SKIPPED,
                        // чтобы остановить спам в логах и корректно подсчитать статистику.
                        // Бизнес-логика и математика остатков при этом остаются неизменными.
                        // ========================================================================
                        std::string errorMsg = saleResult.value("error", "unknown");
                        if (errorMsg == "No available units" || errorMsg == "Item is not available for sale") {
                            totalSalesSkipped++;
                            g_serverLogger.info("syncSalesFrom1C: skipped historical sale for itemId=" +
                                std::to_string(itemId) + " (already processed or out of stock). Reason: " + errorMsg);
                        }
                        else {
                            totalSalesFailed++;
                            g_serverLogger.warning("syncSalesFrom1C: failed to register sale for itemId=" +
                                std::to_string(itemId) + ", error=" + errorMsg);
                        }
                    }
                }
            }

            g_serverLogger.info("syncSalesFrom1C: completed - synced=" +
                std::to_string(totalSalesSynced) +
                ", skipped=" + std::to_string(totalSalesSkipped) +
                ", failed=" + std::to_string(totalSalesFailed));

            db_->logSync("1C_SALES_SYNC", totalSalesSynced,
                (totalSalesFailed == 0) ? "SUCCESS" : "PARTIAL",
                (totalSalesFailed > 0) ? ("Failed: " + std::to_string(totalSalesFailed)) : "");
        }
        catch (const std::exception& e) {
            g_serverLogger.error("syncSalesFrom1C exception: " + std::string(e.what()));
            db_->logSync("1C_SALES_SYNC", 0, "FAILED", e.what());
        }
    }

    static size_t write_callback_1c(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* s = static_cast<std::string*>(userdata);
        s->append(ptr, size * nmemb);
        return size * nmemb;
    }

    json getClientSales(int clientId) {
        // ИСПРАВЛЕНО: формирование корректного URL ".../kiosk_sync/sales?client_id=..."
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
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        if (strlen(Config::ONEC_USER) > 0 && strlen(Config::ONEC_PASSWORD) > 0) {
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            curl_easy_setopt(curl, CURLOPT_USERPWD,
                (std::string(Config::ONEC_USER) + ":" + std::string(Config::ONEC_PASSWORD)).c_str());
        }

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK || http_code != 200) {
            // ДЕТАЛЬНОЕ ЛОГГИРОВАНИЕ: показываем URL и первые 200 символов ответа
            g_serverLogger.error("1C request failed: code=" + std::to_string(http_code) +
                ", error=" + std::to_string(res) + ", url=" + url +
                ", response=" + (response.size() > 200 ? response.substr(0, 200) : response));
            return json::object();
        }

        try {
            json j = json::parse(response);
            if (j.is_array()) {
                json result;
                result["sales"] = j;
                return result;
            }
            if (j.contains("sales") && j["sales"].is_array()) {
                return j;
            }
            return j;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("Failed to parse 1C response: " + std::string(e.what()) +
                ", response=" + (response.size() > 200 ? response.substr(0, 200) : response));
            return json::object();
        }
    }
};