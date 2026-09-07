#pragma once

#include <string>
#include <cstdlib>
#include <climits>

namespace ShopBarcode
{
    // ================================================================
    // Формат внутреннего штрих-кода SHOP
    //
    //     SHOP + ID товара
    //
    // Примеры:
    //
    //     item_id = 1      -> SHOP1
    //     item_id = 123    -> SHOP123
    //     item_id = 45678  -> SHOP45678
    //
    // Штрих-код НЕ содержит:
    //     - ФИО комитента;
    //     - цену;
    //     - название товара;
    //     - номер приложения.
    //
    // Все эти данные находятся в БД и определяются через items.id.
    // ================================================================

    inline constexpr const char* PREFIX = "SHOP";


    // ================================================================
    // Создание barcode по ID товара
    // ================================================================

    inline std::string make(int itemId)
    {
        if (itemId <= 0)
            return std::string();

        return std::string(PREFIX) +
            std::to_string(itemId);
    }


    // ================================================================
    // Проверка формата barcode
    //
    // Допустимо:
    //
    //     SHOP1
    //     SHOP123
    //     SHOP45678
    //
    // Недопустимо:
    //
    //     SHOP
    //     SHOPABC
    //     shop123
    //     SHOP-123
    //     SHOP 123
    // ================================================================

    inline bool isValid(const std::string& barcode)
    {
        // Минимальная длина:
        //
        // SHOP + хотя бы одна цифра
        //
        if (barcode.size() < 5)
            return false;

        if (barcode.compare(
            0,
            4,
            PREFIX) != 0)
        {
            return false;
        }

        for (std::size_t i = 4;
            i < barcode.size();
            ++i)
        {
            const unsigned char c =
                static_cast<unsigned char>(
                    barcode[i]);

            if (c < '0' || c > '9')
                return false;
        }

        return true;
    }


    // ================================================================
    // Получение item_id из barcode
    //
    // SHOP123 -> 123
    // ================================================================

    inline bool tryParse(
        const std::string& barcode,
        int& itemId)
    {
        itemId = 0;

        if (!isValid(barcode))
            return false;

        const std::string idText =
            barcode.substr(4);

        if (idText.empty())
            return false;

        try
        {
            std::size_t processed = 0;

            const long long value =
                std::stoll(
                    idText,
                    &processed,
                    10);

            // Все символы должны быть обработаны.
            if (processed != idText.size())
                return false;

            if (value <= 0)
                return false;

            if (value > INT_MAX)
                return false;

            itemId =
                static_cast<int>(value);

            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}
