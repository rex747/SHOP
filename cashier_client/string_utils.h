#pragma once
#include <string>
#include <windows.h>

inline std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
        static_cast<int>(wstr.size()),
        nullptr, 0, nullptr, nullptr);
    std::string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
        static_cast<int>(wstr.size()),
        &result[0], size_needed, nullptr, nullptr);   // &result[0] Ц неконстантный указатель
    return result;
}

inline std::wstring utf8_to_wstring(const std::string& utf8str) {
    if (utf8str.empty()) return {};
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8str.c_str(),
        static_cast<int>(utf8str.size()),
        nullptr, 0);
    std::wstring result(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8str.c_str(),
        static_cast<int>(utf8str.size()),
        &result[0], size_needed);   // &result[0] Ц неконстантный указатель
    return result;
}

// ѕерегрузка дл€ const char*
inline std::wstring utf8_to_wstring(const char* utf8str) {
    if (!utf8str || !*utf8str) return {};
    std::string str(utf8str);
    return utf8_to_wstring(str);
}