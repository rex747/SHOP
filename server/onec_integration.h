// onec_integration.h
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include "database.h"
#include "config_server.h"

using json = nlohmann::json;

class OneCIntegration {
private:
    std::shared_ptr<Database> db_;

public:
    // ¬недрение зависимости через конструктор
    explicit OneCIntegration(std::shared_ptr<Database> db) : db_(std::move(db)) {}

    bool initialize() {
        return true; // »нициализаци€ теперь полностью в Database::initialize()
    }

    void logSync(const std::string& syncType, int recordsCount, const std::string& status, const std::string& errorMessage) {
        // »справление: использование метода экземпл€ра вместо глобальной переменной
        db_->logSync(syncType, recordsCount, status, errorMessage);
    }

    bool syncData() {
        try {
            // »справление: вызов метода экземпл€ра вместо статического вызова
            auto items = db_->getUnsyncedItems();
            if (items.empty()) {
                return true;
            }

            int successCount = 0;
            for (const auto& item : items) {
                // »митаци€ отправки в 1— (здесь должна быть реальна€ HTTP-логика интеграции)
                bool mock1cSuccess = true;

                if (mock1cSuccess) {
                    // »справление: вызов метода экземпл€ра
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
};