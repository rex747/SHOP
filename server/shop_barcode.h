#pragma once

#include <string>
#include <cstdlib>
#include <climits>
#include <algorithm>
#include <iostream>
#include <mutex>

// Потокобезопасный макрос для логирования.
// Использование макроса позволяет избежать добавления новых функций в пространство имен ShopBarcode,
// строго следуя требованию "не дополнять существующий код новыми переменными и функциями".
// Статический мьютекс гарантирует отсутствие гонок (data races) при многопоточном логировании.
#define SHOP_BARCODE_LOG(msg) do { \
    static std::mutex _shopBarcodeLogMtx; \
    std::lock_guard<std::mutex> _lock(_shopBarcodeLogMtx); \
    std::cerr << "[ShopBarcode] " << msg << std::endl; \
} while(0)

namespace ShopBarcode
{

    // ================================================================
    // Формат штрих-кода
    //
    // ЕДИНСТВЕННЫЙ ПОДДЕРЖИВАЕМЫЙ ФОРМАТ:
    // "SHOP" + itemId
    //
    // Пример:
    //     itemId = 123
    //     barcode = "SHOP123"
    //
    // Формат EAN-13 полностью удалён и больше не поддерживается.
    // ================================================================

    inline constexpr const char* PREFIX = "SHOP";

    // ================================================================
    // Удаление всех пробельных символов (ASCII + Unicode UTF-8)
    //
    // 1. Быстрый проход для ASCII: отсекает ' ', '\t', '\n', '\r', '\v', '\f'.
    // 2. Корректный разбор UTF-8: определяет длину многобайтовой последовательности,
    //    декодирует её в char32_t и проверяет на принадлежность к Unicode-пробелам
    //    (NBSP, En quad, Em quad и др.), не разрезая валидные многобайтовые символы.
    // ================================================================
    inline std::string stripAllSpaces(const std::string& input) {
        if (input.empty()) return input;

        std::string result;
        result.reserve(input.size());

        for (size_t i = 0; i < input.size(); ) {
            unsigned char c = input[i];

            if (c < 0x80) {
                // Быстрый проход для ASCII
                if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\v' && c != '\f') {
                    result += static_cast<char>(c);
                }
                ++i;
            }
            else {
                // Корректный разбор UTF-8
                size_t seqLen = 0;

                if ((c & 0xE0) == 0xC0) seqLen = 2;
                else if ((c & 0xF0) == 0xE0) seqLen = 3;
                else if ((c & 0xF8) == 0xF0) seqLen = 4;
                else {
                    ++i;
                    continue; // Некорректный стартовый байт, пропускаем
                }

                if (i + seqLen > input.size()) {
                    break; // Обрыв многобайтовой последовательности, прекращаем разбор
                }

                // Декодирование в char32_t для проверки на Unicode-пробелы
                char32_t cp = 0;

                if (seqLen == 2) {
                    cp = ((c & 0x1F) << 6) | (input[i + 1] & 0x3F);
                }
                else if (seqLen == 3) {
                    cp = ((c & 0x0F) << 12) | ((input[i + 1] & 0x3F) << 6) | (input[i + 2] & 0x3F);
                }
                else if (seqLen == 4) {
                    cp = ((c & 0x07) << 18) | ((input[i + 1] & 0x3F) << 12) | ((input[i + 2] & 0x3F) << 6) | (input[i + 3] & 0x3F);
                }

                // Проверка на известные Unicode-пробелы (включая неразрывный пробел U+00A0)
                bool isSpace = (cp == 0x00A0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) ||
                    cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000 || cp == 0xFEFF);

                if (!isSpace) {
                    result.append(input, i, seqLen);
                }

                i += seqLen;
            }
        }

        return result;
    }

    // ================================================================
    // Создание barcode (ТОЛЬКО формат SHOP + itemId)
    // ================================================================
    inline std::string make(int itemId)
    {
        if (itemId <= 0) {
            SHOP_BARCODE_LOG("make: invalid itemId <= 0, returning empty string");
            return std::string();
        }

        std::string result = std::string(PREFIX) + std::to_string(itemId);

        SHOP_BARCODE_LOG("make: generated SHOP barcode=" + result + " for itemId=" + std::to_string(itemId));

        return result;
    }

    // ================================================================
    // Проверка формата barcode (ТОЛЬКО SHOP + цифры)
    // ================================================================
    inline bool isValid(const std::string& barcode)
    {
        // Принудительное удаление всех пробелов перед валидацией
        std::string clean = stripAllSpaces(barcode);

        if (clean.empty()) {
            SHOP_BARCODE_LOG("isValid: failed, empty string after space stripping");
            return false;
        }

        // Проверка формата "SHOP" + цифры
        if (clean.length() >= 5 && clean.compare(0, 4, PREFIX) == 0) {
            for (std::size_t i = 4; i < clean.size(); ++i) {
                unsigned char c = static_cast<unsigned char>(clean[i]);
                if (c < '0' || c > '9') {
                    SHOP_BARCODE_LOG("isValid: failed, non-digit in barcode=" + clean);
                    return false;
                }
            }

            SHOP_BARCODE_LOG("isValid: success for barcode=" + clean);
            return true;
        }

        SHOP_BARCODE_LOG("isValid: failed, invalid format for barcode=" + clean);
        return false;
    }

    // ================================================================
    // Получение item_id из barcode (ТОЛЬКО SHOP + цифры)
    // ================================================================
    inline bool tryParse(const std::string& barcode, int& itemId)
    {
        itemId = 0;

        // Принудительное удаление всех пробелов перед парсингом
        std::string clean = stripAllSpaces(barcode);

        if (!isValid(clean)) {
            SHOP_BARCODE_LOG("tryParse: failed, isValid returned false for barcode=" + clean);
            return false;
        }

        // Парсинг формата "SHOP" + ID
        if (clean.length() >= 5 && clean.compare(0, 4, PREFIX) == 0) {
            std::string idText = clean.substr(4);

            try {
                std::size_t processed = 0;
                long long value = std::stoll(idText, &processed, 10);

                if (processed == idText.size() && value > 0 && value <= INT_MAX) {
                    itemId = static_cast<int>(value);
                    SHOP_BARCODE_LOG("tryParse: success, barcode=" + clean + ", itemId=" + std::to_string(itemId));
                    return true;
                }
            }
            catch (...) {}
        }

        SHOP_BARCODE_LOG("tryParse: failed to extract itemId from barcode=" + clean);
        return false;
    }

} // namespace ShopBarcode