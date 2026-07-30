#pragma once
#include <string>
#include <windows.h>
#include <sstream>      
#include <iomanip>      

inline std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    // Без -1 → размер БЕЗ завершающего нуля
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
        static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) return {};
    std::string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
        static_cast<int>(wstr.size()), result.data(), size_needed, nullptr, nullptr);
    return result;
}

inline std::wstring utf8_to_wstring(const char* utf8str) {
    if (!utf8str || !*utf8str) return {};
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8str,
        static_cast<int>(strlen(utf8str)),
        nullptr, 0);
    std::wstring result(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8str,
        static_cast<int>(strlen(utf8str)),
        result.data(), size_needed);
    return result;
}

inline std::wstring utf8_to_wstring(const std::string& utf8str) {
    if (utf8str.empty()) return {};
    return utf8_to_wstring(utf8str.c_str());
}

inline std::wstring to_wstring(double val) {
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(2) << val;
    return ss.str();
}
