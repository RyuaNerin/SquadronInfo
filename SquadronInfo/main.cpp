#define _CRT_SECURE_NO_WARNINGS

#include "resource.h"

#include <iostream>
#include <cstddef>
#include <string>
#include <memory>
#include <map>

#include <Windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <winhttp.h>

#include <json/json.h>

#pragma comment(lib, "Winhttp.lib")

#define DEFAULT_SIGNATURE "BA01000000E8........4883380074..488B0D........"

inline uint32_t toUInt32(PBYTE ptr, size_t offset) { return *((uint32_t*)(ptr + offset)); }
inline uint64_t toUInt64(PBYTE ptr, size_t offset) { return *((uint64_t*)(ptr + offset)); }

std::wstring sf(const std::wstring fmt_str, ...)
{
    size_t final_n, n = fmt_str.size() * 2;
    std::unique_ptr<wchar_t[]> formatted;
    va_list ap;

    do
    {
        formatted.reset(new wchar_t[n]);
        wcscpy(&formatted[0], fmt_str.c_str());

        va_start(ap, fmt_str);
        final_n = _vsnwprintf_s(&formatted[0], n, n - 1, fmt_str.c_str(), ap);
        va_end(ap);

        if (final_n < 0 || final_n >= n)
            n += std::abs((int)(final_n - n + 1));
        else
            break;
    } while (true);

    return std::wstring(formatted.get());
}

void setConsoleSize(int32_t w, int32_t h)
{
    HWND console = GetConsoleWindow();

    LONG style = GetWindowLong(console, GWL_STYLE);
    style &= ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX);

    SetWindowLongPtr(console, GWL_STYLE, style);
    SetWindowPos(console, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);

    HANDLE stdOut = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(stdOut, &info);
    
    SMALL_RECT rect;
    rect.Top    = 0;
    rect.Left   = 0;
    rect.Right  = w > 0 ? w : info.srWindow.Right  - info.srWindow.Left + 1;
    rect.Bottom = h > 0 ? h : info.srWindow.Bottom - info.srWindow.Top  + 1;
    SetConsoleWindowInfo(stdOut, TRUE, &rect);

    GetConsoleScreenBufferInfo(stdOut, &info);

    COORD coord;
    coord.X = info.srWindow.Right  - info.srWindow.Left + 1;
    coord.Y = info.srWindow.Bottom - info.srWindow.Top  + 1;
    SetConsoleScreenBufferSize(stdOut, coord);
}

void clearConsole()
{
    HANDLE stdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO screen;

    GetConsoleScreenBufferInfo(stdOut, &screen);

    COORD topLeft = { 0, 0 };
    DWORD written;
    FillConsoleOutputCharacterA(stdOut, ' ', screen.dwSize.X * screen.dwSize.Y, topLeft, &written);
    FillConsoleOutputAttribute(stdOut, screen.wAttributes, screen.dwSize.X * screen.dwSize.Y, topLeft, &written);
    SetConsoleCursorPosition(stdOut, topLeft);
}

void setConsoleColors(WORD attr)
{
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFOEX csb = { 0, };
    csb.cbSize = sizeof(CONSOLE_SCREEN_BUFFER_INFOEX);

    GetConsoleScreenBufferInfoEx(console, &csb);
    csb.wAttributes = attr;
    SetConsoleScreenBufferInfoEx(console, &csb);
}

PBYTE getPointer(HANDLE hProcess, PBYTE address)
{
    int8_t buff[8] = { 0, };
    return ReadProcessMemory(hProcess, (LPCVOID)address, buff, 8, NULL) ? (PBYTE)toUInt64((PBYTE)buff, 0) : (PBYTE)NULL;
}

inline size_t getCharWidth(std::wstring string)
{
    size_t w = 0;

    for (wchar_t& c : string)
        w += (0 <= c && c < 256) ? 1 : 2;

    return w;
}
std::wstring PadLeft(std::wstring string, size_t width)
{
    width -= getCharWidth(string);
    if (width == 0) return string;

    std::wstring paddedString(width, L' ');
                 paddedString += string;

    return paddedString;
}
std::wstring PadCenter(std::wstring string, size_t width)
{
    width -= getCharWidth(string);
    if (width == 0) return string;

    size_t l = width / 2;

    std::wstring paddedString(width, L' ');
                 paddedString.insert(l, string);

    return paddedString;
}
std::wstring PadRight(std::wstring string, size_t width)
{
    width -= getCharWidth(string);
    if (width == 0) return string;

    std::wstring paddedString(string);
    paddedString.append(width, L' ');

    return paddedString;
}

void ShowConsoleCursor(bool showFlag)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo = { 0, };

    GetConsoleCursorInfo(out, &cursorInfo);
    cursorInfo.bVisible = showFlag;
    SetConsoleCursorInfo(out, &cursorInfo);
}

DWORD GetProcessByName()
{
    DWORD pid = 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W process = { 0, };
    process.dwSize = sizeof(process);

    if (Process32FirstW(snapshot, &process))
    {
        do
        {
            if (std::wcscmp(process.szExeFile, L"ffxiv_dx11.exe") == 0)
            {
                pid = process.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &process));
    }

    CloseHandle(snapshot);

    return pid;
}

bool getFFXIVModule(DWORD pid, LPCWSTR lpModuleName, PBYTE* modBaseAddr, DWORD* modBaseSize)
{
    bool res = false;

    MODULEENTRY32 snapEntry = { 0 };
    snapEntry.dwSize = sizeof(MODULEENTRY32);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (hSnapshot)
    {
        if (Module32First(hSnapshot, &snapEntry))
        {
            do
            {
                if (lstrcmpi(snapEntry.szModule, lpModuleName) == 0)
                {
                    *modBaseAddr = snapEntry.modBaseAddr;
                    *modBaseSize = snapEntry.modBaseSize;
                    res = true;
                    break;
                }
            } while (Module32Next(hSnapshot, &snapEntry));
        }
        CloseHandle(hSnapshot);
    }

    return res;
}

bool getHttp(LPCWSTR host, LPCWSTR path, std::string &body)
{
    bool res = false;

    BOOL      bResults = FALSE;
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;

    DWORD dwStatusCode;
    DWORD dwSize;
    DWORD dwRead;

    hSession = WinHttpOpen(L"FFXIV-Squadron", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession)
        bResults = WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);
    if (bResults)
        hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hConnect)
        hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (hRequest)
        bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, NULL);
    if (bResults)
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (bResults)
    {
        dwSize = sizeof(dwStatusCode);
        bResults = WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
    }
    if (bResults)
        bResults = dwStatusCode == 200;
    if (bResults)
    {
        size_t dwOffset;
        do
        {
            dwSize = 0;
            bResults = WinHttpQueryDataAvailable(hRequest, &dwSize);
            if (!bResults || dwSize == 0)
                break;

            while (dwSize > 0)
            {
                dwOffset = body.size();
                body.resize(dwOffset + dwSize);

                bResults = WinHttpReadData(hRequest, &body[dwOffset], dwSize, &dwRead);
                if (!bResults || dwRead == 0)
                    break;

                body.resize(dwOffset + dwRead);

                dwSize -= dwRead;
            }
        } while (true);

        res = true;
    }
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);

    return res;
}

bool checkLatestRelease()
{
    std::string body;
    if (getHttp(L"api.github.com", L"/repos/RyuaNerin/SquadronInfo/releases/latest", body))
    {
        Json::Reader jsonReader;
        Json::Value json;

        if (jsonReader.parse(body, json))
        {
            std::string tag_name = json["tag_name"].asString();
            if (tag_name.compare(PROC_VERSION_STR) != 0)
            {
                return true;
            }
        }
    }

    return false;
}

uint16_t hex2dec(const char *hex)
{
    uint16_t val = 0;

    if (hex[0] == '.' && hex[1] == '.')
        return UINT16_MAX;

         if (hex[0] >= '0' && hex[0] <= '9') val = (hex[0] - '0' +  0) << 4;
    else if (hex[0] >= 'a' && hex[0] <= 'f') val = (hex[0] - 'a' + 10) << 4;
    else if (hex[0] >= 'A' && hex[0] <= 'F') val = (hex[0] - 'A' + 10) << 4;

         if (hex[1] >= '0' && hex[1] <= '9') val |= hex[1] - '0' +  0;
    else if (hex[1] >= 'a' && hex[1] <= 'f') val |= hex[1] - 'a' + 10;
    else if (hex[1] >= 'A' && hex[1] <= 'F') val |= hex[1] - 'A' + 10;

    return val;
}

typedef struct _signature
{
    uint16_t *sig;
    size_t    len;
} Signature;

const Signature strToPtr(std::string str)
{
    const char* cstr = str.c_str();

    Signature sig;
    sig.len = str.size() / 2;
    sig.sig = new uint16_t[sig.len];

    for (int i = 0; i < sig.len; ++i)
        sig.sig[i] = hex2dec(cstr + i * 2);

    return sig;
}

Signature getSignature()
{
    std::string body;

    #ifndef _DEBUG
    if (!getHttp(L"raw.githubusercontent.com", L"/RyuaNerin/SquadronInfo/master/signature.txt", body))
    #endif
        body.append(DEFAULT_SIGNATURE);

    return strToPtr(body);
}

size_t findArray(const uint8_t* buff, const Signature sig, size_t len);
PBYTE scanFromSignature(const PBYTE baseAddr, const DWORD len, const HANDLE hProcess, const Signature sig)
{
    uint8_t buff[2048];

          PBYTE curPtr = (PBYTE)baseAddr;
    const PBYTE maxPtr = (PBYTE)baseAddr + len;

    size_t index;
    size_t read;
    size_t nSize = sizeof(buff);

    while (curPtr < maxPtr)
    {
        if (curPtr + sizeof(buff) > maxPtr)
            nSize = maxPtr - curPtr;

        if (nSize < sig.len)
            break;

        if (ReadProcessMemory(hProcess, (LPCVOID)curPtr, (LPVOID)buff, nSize, &read))
        {
            index = findArray(buff, sig, read);

            if (index != -1)
                return curPtr + index + sig.len + toUInt32((PBYTE)buff, index + sig.len - 4);

            curPtr += read - sig.len + 1;
        }
        else
        {
            curPtr += 1;
        }
    }

    return 0;
}

size_t findArray(const uint8_t* buff, const Signature sig, size_t len)
{
    len -= sig.len;

    size_t i, j;
    for (i = 0; i < len; i++)
    {
        for (j = 0; j < sig.len; ++j)
        {
            if ((sig.sig[j] != UINT16_MAX) && (buff[i + j] != (uint8_t)sig.sig[j]))
            {
                break;
            }
        }

        if (j == sig.len)
            return i;
    }

    return -1;
}

typedef struct _squadronInfo
{
    /* 00 - 07 */ uint8_t  __unknown0[8];
    /* 08 - 08 */ uint8_t  race;
    /* 09 - 09 */ uint8_t  woman;
    /* 10 - 10 */ uint8_t  job;
    /* 11 - 11 */ uint8_t  level;
    /* 12 - 15 */ uint32_t exp;
    /* 16 - 31 */ uint8_t  __unknown1[16];
    /* 32 - 32 */ uint8_t  trait;
    /* 33 - 33 */ uint8_t  tcond;
    /* 34 - 34 */ uint8_t  tvalue;
    /* 35 - 35 */ uint8_t  trait2;
    /* 36 - 36 */ uint8_t  tcond2;
    /* 37 - 37 */ uint8_t  tvalue2;
    /* 38 - 39 */ uint8_t  __unknown2[2];
} SquadronInfo;

std::map<uint8_t, std::wstring> races =
{
    { 1, L"휴런"   },
    { 2, L"엘레젠" },
    { 3, L"라라펠" },
    { 4, L"미코테" },
    { 5, L"루가딘" },
    { 6, L"아우라" }
};

// 
std::map<uint8_t, std::wstring> jobs =
{
    { 1, L"검술사" },
    { 2, L"격투사" },
    { 3, L"도끼술사" },
    { 4, L"창술사" },
    { 5, L"궁술사" },
    { 6, L"환술사" },
    { 7, L"주술사" },
    { 26, L"비술사" },
    { 29, L"쌍검사" },
};

// gcarmyexpeditiontraitcond.exh_ko
std::map<uint8_t, std::wstring> trait_cond =
{
    {  0, L"" },
    {  1, L"소대 임무 참가 -> " },
    {  2, L"권장 레벨 충족 -> " },
    {  3, L"레벨 50 이상 -> " },
    {  4, L"파티에 휴런족 -> " },
    {  5, L"파티에 엘레젠 -> " },
    {  6, L"파티에 미코테족 -> " },
    {  7, L"파티에 라라펠족 -> " },
    {  8, L"파티에 루가딘족 -> " },
    {  9, L"파티에 아우라족 -> " },
    { 10, L"파티에 검술사 -> " },
    { 11, L"파티에 도끼술사 -> " },
    { 12, L"파티에 궁술사 -> " },
    { 13, L"파티에 창술사 -> " },
    { 14, L"파티에 쌍검사 -> " },
    { 15, L"파티에 격투사 -> " },
    { 16, L"파티에 환술사 -> " },
    { 17, L"파티에 주술사 -> " },
    { 18, L"파티에 비술사 -> " },
    { 19, L"같은 종족 -> " },
    { 20, L"같은 종족X -> " },
    { 21, L"같은 클래스 -> " },
    { 22, L"같은 클래스X -> " },
    { 23, L"종족이 각기 다름 -> " },
    { 24, L"클래스 각기 다름 -> " },
    { 25, L"같은 종족 x3 -> " },
    { 26, L"같은 클래스 x3 -> " },
    /*
    { 1, L"소대 임무 참가 시" },
    { 2, L"임무 권장 레벨 충족 시" },
    { 3, L"레벨 50 이상일 때" },
    { 4, L"파티에 휴런족이 있을 때" },
    { 5, L"파티에 엘레젠족이 있을 때" },
    { 6, L"파티에 미코테족이 있을 때" },
    { 7, L"파티에 라라펠족이 있을 때" },
    { 8, L"파티에 루가딘족이 있을 때" },
    { 9, L"파티에 아우라족이 있을 때" },
    { 10, L"파티에 검술사가 있을 때" },
    { 11, L"파티에 도끼술사가 있을 때" },
    { 12, L"파티에 궁술사가 있을 때" },
    { 13, L"파티에 창술사가 있을 때" },
    { 14, L"파티에 쌍검사가 있을 때" },
    { 15, L"파티에 격투사가 있을 때" },
    { 16, L"파티에 환술사가 있을 때" },
    { 17, L"파티에 주술사가 있을 때" },
    { 18, L"파티에 비술사가 있을 때" },
    { 19, L"파티에 자신과 같은 종족이 있을 때" },
    { 20, L"파티에 자신과 같은 종족이 없을 때" },
    { 21, L"파티에 자신과 같은 클래스가 있을 때" },
    { 22, L"파티에 자신과 같은 클래스가 없을 때" },
    { 23, L"파티원의 종족이 각기 다를 때" },
    { 24, L"파티원의 클래스가 각기 다를 때" },
    { 25, L"파티에 같은 종족이 3명 이상일 때" },
    { 26, L"파티에 같은 클래스가 3명 이상일 때" },
    */

};

typedef struct _trait
{
    int8_t       v[10];
    std::wstring s;
} TRAIT;

// gcarmyexpeditiontrait.exh_ko
const TRAIT trait[] =
{
    { {  0,  0,  0,  0,  0 }, L"" },
    { { 10, 10, 15, 15, 20 }, L"신체능력 +%d%%" },
    { { 10, 10, 15, 15, 20 }, L"정신력 +%d%%" },
    { { 10, 10, 15, 15, 20 }, L"전술 숙련도 +%d%%" },
    { {  3,  3,  3,  3,  5 }, L"파티 신체능력 +%d%%" },
    { {  3,  3,  3,  3,  5 }, L"파티 정신력 +%d%%" },
    { {  3,  3,  3,  3,  5 }, L"파티 전술 숙련도 +%d%%" },
    { { 10, 10, 20, 20, 30 }, L"경험치 +%d%%" },
    { {  5,  5, 10, 10, 15 }, L"파티 경험치 +%d%%" },
    { { 10, 20, 30, 40, 50 }, L"파티 징크스 획득 +%d%%" },
    { { 10, 20, 30, 40, 50 }, L"방어 교본 입수 (%d%%)" },
    { { 10, 20, 30, 40, 50 }, L"마법 교본 입수 (%d%%)" },
    { { 10, 20, 30, 40, 50 }, L"공격 교본 입수 (%d%%)" },
    { { 10, 20, 30, 40, 50 }, L"길 (%d%%)" },
    { { 10, 20, 30, 40, 50 }, L"군표 (%d%%)" },
    { { 10, 20, 30, 40, 50 }, L"MGP (%d%%)" },
    { { 10, 20, 30, 40, 50 }, L"방어 마테 (%d%%)" },
    { { 10, 20, 30, 40, 50 }, L"물공 마테 (%d%%)" },
    { { 10, 20, 30, 40, 50 }, L"마법 마테 (%d%%)" },
    { { 10, 20, 30, 40, 50 }, L"채집 마테 (%d%%)" },
    { { 10, 20, 30, 40, 50 }, L"제작 마테 (%d%%)" },
    { { 10, 20, 30, 40, 50 }, L"클러스터 (%d%%)" },
    { { 10, 20, 30, 40, 50 }, L"채집가 화폐 (%d%%)" },
    { { 10, 20, 30, 40, 50 }, L"제작자 화폐 (%d%%)" },
    /*    
    { { 10,100,10,100,15,100,15,100,20,100 }, L"신체능력 +%d%%" },
    { { 10,100,10,100,15,100,15,100,20,100 }, L"정신력 +%d%%" },
    { { 10,100,10,100,15,100,15,100,20,100 }, L"전술 숙련도 +%d%%" },
    { { 3,100,3,100,3,100,3,100,5,100 }, L"파티원의 신체능력 +%d%%" },
    { { 3,100,3,100,3,100,3,100,5,100 }, L"파티원의 정신력 +%d%%" },
    { { 3,100,3,100,3,100,3,100,5,100 }, L"파티원의 전술 숙련도 +%d%%" },
    { { 10,100,10,100,20,100,20,100,30,100 }, L"획득 경험치 +%d%%" },
    { { 5,100,5,100,10,100,10,100,15,100 }, L"파티원의 획득 경험치 +%d%%" },
    { { 10,100,20,100,30,100,40,100,50,100 }, L"파티원의 징크스 획득률 +%d%%" },
    { { 10,0,20,0,30,0,40,0,50 }, L"%d%% 확률로 '전투기술 교본: 방어' 입수" },
    { { 10,0,20,0,30,0,40,0,50 }, L"%d%% 확률로 '전투기술 교본: 마법' 입수" },
    { { 10,0,20,0,30,0,40,0,50 }, L"%d%% 확률로 '전투기술 교본: 공격' 입수" },
    { { 10,0,20,0,30,0,40,0,50 }, L"%d%% 확률로 길 입수" },
    { { 10,0,20,0,30,0,40,0,50 }, L"%d%% 확률로 군표 입수" },
    { { 10,0,20,0,30,0,40,0,50 }, L"%d%% 확률로 MGP 입수" },
    { { 10,0,20,0,30,0,40,0,50 }, L"%d%% 확률로 방어용 마테리아 입수" },
    { { 10,0,20,0,30,0,40,0,50 }, L"%d%% 확률로 물리 공격용 마테리아 입수" },
    { { 10,0,20,0,30,0,40,0,50 }, L"%d%% 확률로 마법사용 마테리아 입수" },
    { { 10,0,20,0,30,0,40,0,50 }, L"%d%% 확률로 채집가용 마테리아 입수" },
    { { 10,0,20,0,30,0,40,0,50 }, L"%d%% 확률로 제작자용 마테리아 입수" },
    { { 10,0,20,0,30,0,40,0,50 }, L"%d%% 확률로 클러스터 입수" },
    { { 10,0,20,0,30,0,40,0,50 }, L"%d%% 확률로 채집가용 화폐 입수" },
    { { 10,0,20,0,30,0,40,0,50 }, L"%d%% 확률로 제작자용 화폐 입수" },
    */
};

const int8_t trait_value[] =
{
    5,
    10,
    15,
    30
};

const int16_t maxExp[] = {
    1000,  1100,  1200,  1300,  1400,
    1500,  1600,  1700,  1800,  2000,
    2200,  2400,  2600,  2800,  3000,
    3200,  3400,  3600,  3800,  4100,
    4400,  4700,  5000,  5300,  5600,
    5900,  6200,  6500,  6800,  7200,
    7600,  8000,  8400,  8800,  9200,
    9600, 10000, 10400, 11000, 12000,
    13000, 14000, 15000, 16000, 18000,
    20000, 22000, 24000, 26000,     0
};

void wcoutError()
{
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_INTENSITY | BACKGROUND_BLUE | BACKGROUND_GREEN | BACKGROUND_RED | BACKGROUND_INTENSITY); 
    std::wcout << L"  문제가 발견했습니다." << std::endl
               << L"  다음 문제 중 하나일 수 있습니다." << std::endl
               << std::endl
               << L"  1) 파이널 판타지 14 가 실행중이 아닙니다." << std::endl
               << L"  2) dx11 (64 bit) 만 지원합니다." << std::endl
               << L"  3) 지원하지 않는 클라이언트 버전일 수 있습니다." << std::endl
               << L"  4) 소대 막사로 이동하여 소대 정보창을 열어야 합니다." << std::endl;
        
        std::getwchar();
}
void wcoutHeader()
{
    std::wcout << std::endl
               << L"  RyuaNerin 이 제작하였습니다." << std::endl
               << L"  한국서버 파이널 판타지 14 3.45 패치를 지원합니다." << std::endl
               << L"  최신 릴리즈는 github.com/RyuaNerin/SquadronInfo" << std::endl
               << L"    에서 받으실 수 있습니다" << std::endl
               << std::endl;
}

void wmain()
{
#define WMAIN_ERROR { wcoutError(); return; }
#define WMAIN_CLEAR { clearConsole(); wcoutHeader(); }
    
    std::wcout.imbue(std::locale("kor"));

    setConsoleSize(102, 31);
    setConsoleColors(BACKGROUND_BLUE | BACKGROUND_GREEN | BACKGROUND_RED | BACKGROUND_INTENSITY);
    SetConsoleTitleW(L"파이널 판타지 14 소대 정보 뷰어");
    ShowConsoleCursor(false);
    ShowScrollBar(GetConsoleWindow(), SB_BOTH, FALSE);

#ifndef _DEBUG
    if (checkLatestRelease())
    {
        ShellExecute(NULL, NULL, L"\"https://github.com/RyuaNerin/SquadronInfo/releases/latest\"", NULL, NULL, SW_SHOWNORMAL);
        return;
    }
#endif
    WMAIN_CLEAR;

    const Signature sig = getSignature();

    DWORD gameProcessId = GetProcessByName();
    if (gameProcessId == NULL)
        WMAIN_ERROR;

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, false, gameProcessId);
    if (hProcess == NULL)
        WMAIN_ERROR;

    PBYTE baseModuleAddr;
    DWORD baseModuleSize;
    
    if (!getFFXIVModule(gameProcessId, L"ffxiv_dx11.exe", &baseModuleAddr, &baseModuleSize))
        WMAIN_ERROR;

    PBYTE ptr = scanFromSignature(baseModuleAddr, baseModuleSize, hProcess, sig);
    if (ptr == 0)
        WMAIN_ERROR;

    ptr = getPointer(hProcess, ptr);
    ptr = getPointer(hProcess, ptr);
    
    size_t i;

    uint8_t sq[6];
    uint8_t buff[320];
    SquadronInfo *si;
    do
    {
        WMAIN_CLEAR;

        if (FALSE == ReadProcessMemory(hProcess, ptr, buff, sizeof(buff), NULL))
            WMAIN_ERROR;

        if (FALSE == ReadProcessMemory(hProcess, ptr + 326, sq, sizeof(sq), NULL))
            WMAIN_ERROR;
        
        si = (SquadronInfo*)buff;

        std::wcout     << L"  소대 종합능력 : " << sq[0] << " / " << sq[2] << " / " << sq[4] << std::endl;

        //                  0         1         2         3         4         5         6         7         8         9         10
        //                  123456789.123456789.123456789.123456789.123456789.123456789.123456789.123456789.123456789.123456789.12
        //                    | 1234 | 1234 | 12345 / 12345 | 12345678 | 123456 | 1234 | 소대 임무 참가 -> 파티 징크스 획득 +20% |
        //                    | 1234 | 1234 | 12345 / 12345 | 12345678 | 123456 | 1234 | 123456789.123456789.123456789.123456789 |
        std::wcout     << L"  ----------------------------------------------------------------------------------------------------" << std::endl
                       << L"  | 번호 | 레벨 |    경험치     |  클래스  |  종족  | 성별 | 행운의 법칙                             |" << std::endl;

        for (i = 0; i < 8; ++i)
        {
            std::wcout << L"  |------|------|---------------|----------|--------|------|-----------------------------------------|" << std::endl
                       << L"  |    "
                       << std::to_wstring(i)                                    << L" | "
                       << PadLeft(std::to_wstring(si[i].level), 4)              << L" | "
                       << PadLeft(std::to_wstring(si[i].exp),   5)              << L" / "
                       << PadLeft(std::to_wstring(maxExp[si[i].level - 1]), 5)  << L" | "
                       << PadCenter(jobs[si[i].job], 8)                         << L" | "
                       << PadCenter(races[si[i].race], 6)                       << L" | "
                       << (si[i].woman ? L" 여 " : L" 남 ")                     << L" | ";

            if (si[i].tcond)
                std::wcout << PadRight(trait_cond[si[i].tcond] + sf(trait[si[i].trait].s, trait[si[i].trait].v[si[i].tvalue]), 39) << L" |" << std::endl;
            else
                std::wcout << "                                        |" << std::endl;
        }

        std::wcout     << L"  ----------------------------------------------------------------------------------------------------" << std::endl
                       << std::endl
                       << L"  엔터 키를 누르면 갱신, 다른 키를 누르면 종료됩니다." << std::endl;
    } while (std::getchar() == L'\n');
}
