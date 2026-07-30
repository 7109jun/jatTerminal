// ============================================================================
// jat Terminal v2.0.0
// 괄호형 명령어 UX 터미널 시스템 - Win32 Native / GDI+ 커스텀 렌더링
// 팔레트: 초록(테두리/글로우) / 검정(배경) / 회색(타이틀바) / 흰색(텍스트)
// ============================================================================
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <winsvc.h>
#include <bcrypt.h>
#include <wininet.h>
#include <urlmon.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>
#include <chrono>
#include <cwctype>
#include <cstdio>
#include <cstdint>
#include <iomanip>
#include <algorithm>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace fs = std::filesystem;
using namespace Gdiplus;

// ----------------------------------------------------------------------------
// 상수 / 팔레트
// ----------------------------------------------------------------------------
static const int WIN_W = 1000;
static const int WIN_H = 650;
static const int MARGIN = 18;
static const int BORDER_T = 7;
static const int RADIUS = 26;
static const int TITLE_H = 46;
static const int INPUT_H = 34;
static const int PADDING = 14;
static const UINT WM_JAT_REFRESH = WM_APP + 1;

static Color COL_GREEN      (255, 120, 255, 175);
static Color COL_GRAY_TITLE (255,  55,  55,  55);
static Color COL_GRAY_C1    (255, 150, 150, 150);
static Color COL_GRAY_C2    (255,  95,  95,  95);
static Color COL_GRAY_C3    (255,  45,  45,  45);
static Color COL_BLACK_BG   (255,  10,  10,  10);
static Color COL_WHITE      (255, 235, 235, 235);
static Color COL_GRAY_TEXT  (255, 150, 150, 150);

// ----------------------------------------------------------------------------
// 전역 상태
// ----------------------------------------------------------------------------
static HWND g_hwnd = nullptr;
static ULONG_PTR g_gdiToken = 0;
static Image* g_logo = nullptr;
static CRITICAL_SECTION g_cs;

struct LogLine { std::wstring text; int color; }; // 0=white 1=green 2=gray 3=error(white)
static std::vector<LogLine> g_logLines;
static std::wstring g_input;
static std::vector<std::wstring> g_history;
static int g_historyIndex = -1;
static int g_scrollOffset = 0;
static bool g_cursorVisible = true;
static std::map<std::wstring, std::vector<std::wstring>> g_macros; // Macro / Alias 저장소

// ----------------------------------------------------------------------------
// 문자열 유틸
// ----------------------------------------------------------------------------
static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::wstring ToLowerW(std::wstring s) { for (auto& c : s) c = towlower(c); return s; }

static std::string WStringToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], sz, nullptr, nullptr);
    return out;
}
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(sz, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], sz);
    return out;
}
static std::wstring AcpToW(const std::string& s) {
    if (s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(sz, 0);
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &out[0], sz);
    return out;
}

static void Log(const std::wstring& text, int colorType) {
    EnterCriticalSection(&g_cs);
    std::wstringstream ss(text);
    std::wstring line;
    bool any = false;
    while (std::getline(ss, line)) { g_logLines.push_back({ line, colorType }); any = true; }
    if (!any) g_logLines.push_back({ L"", colorType });
    if (g_logLines.size() > 4000) g_logLines.erase(g_logLines.begin(), g_logLines.begin() + 1000);
    g_scrollOffset = 0;
    LeaveCriticalSection(&g_cs);
}

// ----------------------------------------------------------------------------
// 명령어 파서 (괄호형 UX)
// ----------------------------------------------------------------------------
struct ParsedCommand {
    bool valid = false, dryRun = false, silent = false;
    std::wstring name;
    std::vector<std::wstring> args;
    std::wstring error;
};

static std::vector<std::wstring> SplitTopLevel(const std::wstring& s, wchar_t delim) {
    std::vector<std::wstring> res;
    int depthParen = 0, depthBrack = 0;
    bool inQuote = false;
    std::wstring cur;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'"' && (i == 0 || s[i - 1] != L'\\')) inQuote = !inQuote;
        if (!inQuote) {
            if (c == L'(') depthParen++;
            else if (c == L')') depthParen--;
            else if (c == L'[') depthBrack++;
            else if (c == L']') depthBrack--;
        }
        if (c == delim && !inQuote && depthParen == 0 && depthBrack == 0) { res.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!Trim(cur).empty() || !res.empty()) res.push_back(cur);
    return res;
}

static ParsedCommand ParseCommand(std::wstring s) {
    ParsedCommand pc;
    s = Trim(s);
    while (!s.empty() && (s[0] == L'?' || s[0] == L'$')) {
        if (s[0] == L'?') pc.dryRun = true; else pc.silent = true;
        s = Trim(s.substr(1));
    }
    size_t p = s.find(L'(');
    if (p == std::wstring::npos || s.empty() || s.back() != L')') {
        pc.error = L"괄호 형식이 아닙니다. 예) Create(\"이름\",\"내용\",\"txt\")";
        return pc;
    }
    std::wstring name = Trim(s.substr(0, p));
    if (name.empty()) { pc.error = L"명령어 이름이 없습니다."; return pc; }
    std::wstring inner = s.substr(p + 1, s.size() - p - 2);
    auto rawArgs = SplitTopLevel(inner, L',');
    for (auto& a : rawArgs) {
        std::wstring t = Trim(a);
        if (t.size() >= 2 && t.front() == L'"' && t.back() == L'"') {
            std::wstring content = t.substr(1, t.size() - 2);
            std::wstring out;
            for (size_t i = 0; i < content.size(); ++i) {
                if (content[i] == L'\\' && i + 1 < content.size()) {
                    wchar_t nx = content[i + 1];
                    if (nx == L'n') { out += L'\n'; i++; continue; }
                    if (nx == L'"') { out += L'"'; i++; continue; }
                    if (nx == L'\\') { out += L'\\'; i++; continue; }
                }
                out += content[i];
            }
            pc.args.push_back(out);
        } else pc.args.push_back(t);
    }
    pc.name = name;
    pc.valid = true;
    return pc;
}

static std::vector<std::wstring> ParseBracketList(const std::wstring& raw) {
    std::wstring t = Trim(raw);
    if (t.size() >= 2 && t.front() == L'[' && t.back() == L']') t = t.substr(1, t.size() - 2);
    return SplitTopLevel(t, L',');
}

static void ExecuteLine(const std::wstring& raw); // 전방 선언

// ----------------------------------------------------------------------------
// 격리 휴지통 (Remove - Lv.2, 30일 유예)
// ----------------------------------------------------------------------------
static fs::path TrashDir() {
    fs::path d = fs::current_path() / L".jat_trash";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

static void PurgeOldQuarantine() {
    fs::path trash = fs::current_path() / L".jat_trash";
    std::error_code ec;
    if (!fs::exists(trash, ec)) return;
    long long now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (auto& entry : fs::directory_iterator(trash, ec)) {
        if (entry.path().extension() == L".meta") continue;
        fs::path meta = entry.path(); meta += L".meta";
        if (!fs::exists(meta, ec)) continue;
        std::wifstream mf(meta);
        std::wstring origPath; long long ts = 0;
        std::getline(mf, origPath); mf >> ts; mf.close();
        if (now - ts > 30LL * 24 * 3600) { fs::remove_all(entry.path(), ec); fs::remove(meta, ec); }
    }
}

// ----------------------------------------------------------------------------
// SHA-256 (BCrypt) 유틸 - Checksum / HashCompare / DuplicateFinder 공용
// ----------------------------------------------------------------------------
static std::wstring Sha256File(const fs::path& p) {
    BCRYPT_ALG_HANDLE hAlg = nullptr; BCRYPT_HASH_HANDLE hHash = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return L"";
    DWORD hashObjLen = 0, cbData = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjLen, sizeof(DWORD), &cbData, 0);
    std::vector<BYTE> hashObj(hashObjLen);
    DWORD hashLen = 0;
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(DWORD), &cbData, 0);
    std::vector<BYTE> hashVal(hashLen);
    BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjLen, nullptr, 0, 0);
    std::ifstream f(p, std::ios::binary);
    std::vector<char> buf(65536);
    while (f) {
        f.read(buf.data(), (std::streamsize)buf.size());
        std::streamsize n = f.gcount();
        if (n > 0) BCryptHashData(hHash, (PUCHAR)buf.data(), (ULONG)n, 0);
    }
    BCryptFinishHash(hHash, hashVal.data(), hashLen, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    std::wstringstream ss;
    for (auto b : hashVal) ss << std::hex << std::setw(2) << std::setfill(L'0') << (int)b;
    return ss.str();
}

static bool DeriveKey(const std::wstring& password, const BYTE* salt, DWORD saltLen, BYTE* keyOut, DWORD keyLen) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) return false;
    std::string pwUtf8 = WStringToUtf8(password);
    NTSTATUS st = BCryptDeriveKeyPBKDF2(hAlg, (PUCHAR)pwUtf8.data(), (ULONG)pwUtf8.size(), (PUCHAR)salt, saltLen, 100000, keyOut, keyLen, 0);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return st == 0;
}

static bool IsProbablyText(const std::vector<char>& data) {
    size_t n = (std::min)(data.size(), (size_t)4096);
    for (size_t i = 0; i < n; i++) if (data[i] == 0) return false;
    return true;
}

// ----------------------------------------------------------------------------
// CRC32 (Compress/Extract 공용)
// ----------------------------------------------------------------------------
static unsigned long g_crcTable[256];
static bool g_crcInit = false;
static void InitCrc32Table() {
    for (unsigned long i = 0; i < 256; i++) {
        unsigned long c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
        g_crcTable[i] = c;
    }
    g_crcInit = true;
}
static unsigned long Crc32(const unsigned char* data, size_t len) {
    if (!g_crcInit) InitCrc32Table();
    unsigned long c = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) c = g_crcTable[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFUL;
}

// ============================================================================
// [파일 제어] Create / Update / Copy / Move / Rename / Open / Duplicate
// ============================================================================
static void CmdCreateUpdate(std::vector<std::wstring>& args, bool dryRun, bool isUpdate) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: " + std::wstring(isUpdate ? L"Update" : L"Create") + L"(\"파일명\",\"내용\",\"확장자\")", 3); return; }
    std::wstring full = args[0];
    if (args.size() >= 3 && !args[2].empty()) {
        std::wstring ext = args[2]; if (ext[0] != L'.') ext = L"." + ext;
        full = args[0] + ext;
    }
    fs::path p(full);
    if (isUpdate && !fs::exists(p)) { Log(L"[ERROR] 파일이 존재하지 않습니다: " + full + L" (Update는 기존 파일만)", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 파일 " + std::wstring(isUpdate ? L"갱신" : L"생성") + L" 예정: " + full, 2); return; }
    std::ofstream ofs(p, std::ios::binary);
    if (!ofs) { Log(L"[ERROR] 파일을 열 수 없습니다: " + full, 3); return; }
    std::string utf8 = WStringToUtf8(args[1]);
    ofs.write(utf8.data(), (std::streamsize)utf8.size());
    ofs.close();
    Log(L"완료: " + full + L" (" + std::to_wstring(utf8.size()) + L" bytes)", 1);
}

static void CmdCopy(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Copy(\"출발지\",\"목적지\")", 3); return; }
    fs::path src(args[0]), dst(args[1]);
    std::error_code ec;
    if (!fs::exists(src, ec)) { Log(L"[ERROR] 원본이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 복사 예정: " + args[0] + L" -> " + args[1], 2); return; }
    if (fs::is_directory(src, ec)) fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    else fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) { Log(L"[ERROR] 복사 실패: " + AcpToW(ec.message()), 3); return; }
    Log(L"완료: 복사됨 -> " + args[1], 1);
}

static void CmdMove(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Move(\"출발지\",\"목적지\")", 3); return; }
    fs::path src(args[0]), dst(args[1]);
    std::error_code ec;
    if (!fs::exists(src, ec)) { Log(L"[ERROR] 원본이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 이동 예정: " + args[0] + L" -> " + args[1], 2); return; }
    fs::rename(src, dst, ec);
    if (ec) {
        ec.clear();
        if (fs::is_directory(src)) fs::copy(src, dst, fs::copy_options::recursive, ec);
        else fs::copy_file(src, dst, ec);
        if (!ec) fs::remove_all(src, ec);
    }
    if (ec) { Log(L"[ERROR] 이동 실패: " + AcpToW(ec.message()), 3); return; }
    Log(L"완료: 이동됨 -> " + args[1], 1);
}

static void CmdRename(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Rename(\"기존\",\"새이름\")", 3); return; }
    fs::path src(args[0]);
    std::error_code ec;
    if (!fs::exists(src, ec)) { Log(L"[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    fs::path dst = src.parent_path() / args[1];
    if (dryRun) { Log(L"[DRY-RUN] 이름 변경 예정: " + args[0] + L" -> " + args[1], 2); return; }
    fs::rename(src, dst, ec);
    if (ec) { Log(L"[ERROR] 이름 변경 실패: " + AcpToW(ec.message()), 3); return; }
    Log(L"완료: 이름 변경됨 -> " + args[1], 1);
}

static void CmdOpen(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Open(\"파일/경로\")", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 열기 예정: " + args[0], 2); return; }
    HINSTANCE r = ShellExecuteW(nullptr, L"open", args[0].c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) { Log(L"[ERROR] 열기 실패: " + args[0], 3); return; }
    Log(L"완료: 열림 -> " + args[0], 1);
}

static void CmdDuplicate(std::vector<std::wstring>& args, bool dryRun) { CmdCopy(args, dryRun); }

// ============================================================================
// [3단계 삭제] Delete / Remove / Erase
// ============================================================================
static void CmdDelete(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Delete(\"파일명\")", 3); return; }
    fs::path p(args[0]);
    std::error_code ec;
    if (!fs::exists(p, ec)) { Log(L"[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 휴지통 이동 예정 (Lv.1): " + args[0], 2); return; }
    std::wstring full = fs::absolute(p).wstring();
    full.push_back(L'\0'); full.push_back(L'\0');
    SHFILEOPSTRUCTW op{}; op.wFunc = FO_DELETE; op.pFrom = full.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    int r = SHFileOperationW(&op);
    if (r != 0) { Log(L"[ERROR] 삭제 실패 (코드 " + std::to_wstring(r) + L")", 3); return; }
    Log(L"완료: 휴지통으로 이동됨 (Lv.1) -> " + args[0], 1);
}

static void CmdRemove(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Remove(\"파일명\")", 3); return; }
    fs::path p(args[0]);
    std::error_code ec;
    if (!fs::exists(p, ec)) { Log(L"[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 30일 유예 격리 예정 (Lv.2): " + args[0], 2); return; }
    fs::path trash = TrashDir();
    long long epoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::wstring uniqueName = std::to_wstring(epoch) + L"_" + p.filename().wstring();
    fs::path dest = trash / uniqueName;
    fs::path absOrig = fs::absolute(p);
    fs::rename(p, dest, ec);
    if (ec) {
        ec.clear();
        if (fs::is_directory(p)) fs::copy(p, dest, fs::copy_options::recursive, ec);
        else fs::copy_file(p, dest, ec);
        if (!ec) fs::remove_all(p, ec);
    }
    if (ec) { Log(L"[ERROR] 격리 실패: " + AcpToW(ec.message()), 3); return; }
    std::wofstream meta(trash / (uniqueName + L".meta"));
    meta << absOrig.wstring() << L"\n" << epoch;
    meta.close();
    Log(L"완료: 30일 유예 격리됨 (Lv.2) -> .jat_trash/" + uniqueName, 1);
}

static void CmdErase(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Erase(\"파일명\")", 3); return; }
    fs::path p(args[0]);
    std::error_code ec;
    if (!fs::exists(p, ec)) { Log(L"[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 영구 파괴 예정 (Lv.3, 복구불가): " + args[0], 2); return; }
    uintmax_t n = fs::remove_all(p, ec);
    if (ec) { Log(L"[ERROR] 파괴 실패: " + AcpToW(ec.message()), 3); return; }
    Log(L"완료: 영구 파괴됨 (Lv.3) -> " + args[0] + L" (" + std::to_wstring(n) + L"개 항목)", 1);
}

// ============================================================================
// [정보 탐색] Info / Tree / Size / Find / Search / Replace / Analyze / DuplicateFinder / EmptyFinder
// ============================================================================
static void CmdInfo(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Info(\"파일명\")", 3); return; }
    fs::path p(args[0]);
    std::error_code ec;
    if (!fs::exists(p, ec)) { Log(L"[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 정보 조회 예정: " + args[0], 2); return; }
    bool isDir = fs::is_directory(p, ec);
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    std::wstring msg = L"종류: " + std::wstring(isDir ? L"폴더" : L"파일");
    if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) {
        if (!isDir) {
            ULARGE_INTEGER sz; sz.HighPart = fad.nFileSizeHigh; sz.LowPart = fad.nFileSizeLow;
            msg += L"\n크기: " + std::to_wstring(sz.QuadPart) + L" bytes";
        }
        SYSTEMTIME st{};
        FileTimeToSystemTime(&fad.ftLastWriteTime, &st);
        wchar_t tbuf[64];
        swprintf(tbuf, 64, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        msg += L"\n수정일: " + std::wstring(tbuf);
        bool ro = (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        msg += L"\n잠금(읽기전용): " + std::wstring(ro ? L"예" : L"아니오");
    }
    Log(msg, 1);
}

static void TreeRecurse(const fs::path& p, int depth, std::wstring& out) {
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(p, fs::directory_options::skip_permission_denied, ec)) {
        out += std::wstring((size_t)depth * 2, L' ') + (entry.is_directory() ? L"[D] " : L"    ") + entry.path().filename().wstring() + L"\n";
        if (entry.is_directory()) TreeRecurse(entry.path(), depth + 1, out);
    }
}
static void CmdTree(std::vector<std::wstring>& args, bool dryRun) {
    fs::path p = args.empty() ? fs::current_path() : fs::path(args[0]);
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_directory(p, ec)) { Log(L"[ERROR] 폴더가 아닙니다: " + p.wstring(), 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 트리 조회 예정: " + p.wstring(), 2); return; }
    std::wstring out = p.wstring() + L"\n";
    TreeRecurse(p, 1, out);
    Log(out, 1);
}

static uintmax_t SizeRecurse(const fs::path& p) {
    uintmax_t total = 0; std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2; if (!it->is_directory(e2)) total += it->file_size(e2);
    }
    return total;
}
static void CmdSize(std::vector<std::wstring>& args, bool dryRun) {
    fs::path p = args.empty() ? fs::current_path() : fs::path(args[0]);
    std::error_code ec;
    if (!fs::exists(p, ec)) { Log(L"[ERROR] 대상이 없습니다: " + p.wstring(), 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 용량 계산 예정: " + p.wstring(), 2); return; }
    uintmax_t sz = fs::is_directory(p, ec) ? SizeRecurse(p) : fs::file_size(p, ec);
    wchar_t buf[128];
    swprintf(buf, 128, L"용량: %.2f MB (%llu bytes)", sz / 1048576.0, (unsigned long long)sz);
    Log(buf, 1);
}

static void CmdFind(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Find(\"검색어\",\"경로\")", 3); return; }
    std::wstring query = ToLowerW(args[0]);
    fs::path root = args.size() >= 2 ? fs::path(args[1]) : fs::current_path();
    if (dryRun) { Log(L"[DRY-RUN] 검색 예정: '" + args[0] + L"' in " + root.wstring(), 2); return; }
    std::wstring out; int count = 0; std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::wstring fname = ToLowerW(it->path().filename().wstring());
        if (fname.find(query) != std::wstring::npos) { out += it->path().wstring() + L"\n"; count++; }
    }
    Log(count == 0 ? L"검색 결과 없음: " + args[0] : (L"검색 결과 " + std::to_wstring(count) + L"건:\n" + out), count == 0 ? 2 : 1);
}

static void CmdSearch(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Search(\"검색어\",\"경로\")", 3); return; }
    std::wstring query = args[0];
    fs::path root = args.size() >= 2 ? fs::path(args[1]) : fs::current_path();
    if (dryRun) { Log(L"[DRY-RUN] 내용 검색 예정: '" + query + L"' in " + root.wstring(), 2); return; }
    std::string qUtf8 = WStringToUtf8(query);
    std::wstring out; int fileHits = 0; std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2;
        if (it->is_directory(e2)) continue;
        if (it->file_size(e2) > 5 * 1024 * 1024) continue;
        std::ifstream f(it->path(), std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (!IsProbablyText(data)) continue;
        std::string content(data.begin(), data.end());
        if (content.find(qUtf8) != std::string::npos) { out += it->path().wstring() + L"\n"; fileHits++; }
    }
    Log(fileHits == 0 ? L"검색 결과 없음." : (L"내용 검색 결과 " + std::to_wstring(fileHits) + L"개 파일:\n" + out), 1);
}

static void CmdReplace(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Replace(\"찾을내용\",\"바꿀내용\",\"경로\")", 3); return; }
    std::string findUtf8 = WStringToUtf8(args[0]), replUtf8 = WStringToUtf8(args[1]);
    fs::path root = args.size() >= 3 ? fs::path(args[2]) : fs::current_path();
    std::vector<fs::path> targets; std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2;
        if (it->is_directory(e2) || it->file_size(e2) > 5 * 1024 * 1024) continue;
        std::ifstream f(it->path(), std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (!IsProbablyText(data)) continue;
        std::string content(data.begin(), data.end());
        if (content.find(findUtf8) != std::string::npos) targets.push_back(it->path());
    }
    if (dryRun) {
        std::wstring out = L"[DRY-RUN] " + std::to_wstring(targets.size()) + L"개 파일에서 치환 예정:\n";
        for (auto& t : targets) out += t.wstring() + L"\n";
        Log(out, 2); return;
    }
    int count = 0;
    for (auto& t : targets) {
        std::ifstream f(t, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
        size_t pos = 0;
        while ((pos = content.find(findUtf8, pos)) != std::string::npos) { content.replace(pos, findUtf8.size(), replUtf8); pos += replUtf8.size(); }
        std::ofstream ofs(t, std::ios::binary);
        ofs.write(content.data(), (std::streamsize)content.size());
        count++;
    }
    Log(L"완료: " + std::to_wstring(count) + L"개 파일 치환됨", 1);
}

static void CmdAnalyze(std::vector<std::wstring>& args, bool dryRun) {
    fs::path root = args.empty() ? fs::current_path() : fs::path(args[0]);
    std::error_code ec;
    if (!fs::exists(root, ec)) { Log(L"[ERROR] 대상이 없습니다", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 분석 예정: " + root.wstring(), 2); return; }
    std::map<std::wstring, std::pair<int, uintmax_t>> stats;
    int totalFiles = 0; uintmax_t totalSize = 0;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2;
        if (it->is_directory(e2)) continue;
        std::wstring ext = ToLowerW(it->path().extension().wstring());
        if (ext.empty()) ext = L"(없음)";
        uintmax_t sz = it->file_size(e2);
        stats[ext].first++; stats[ext].second += sz;
        totalFiles++; totalSize += sz;
    }
    std::wstring out = L"=== 분석 결과: " + root.wstring() + L" ===\n";
    out += L"총 파일 수: " + std::to_wstring(totalFiles) + L", 총 용량: " + std::to_wstring(totalSize / 1048576) + L" MB\n";
    for (auto& kv : stats) out += kv.first + L" : " + std::to_wstring(kv.second.first) + L"개, " + std::to_wstring(kv.second.second / 1024) + L" KB\n";
    Log(out, 1);
}

static void CmdDuplicateFinder(std::vector<std::wstring>& args, bool dryRun) {
    fs::path root = args.empty() ? fs::current_path() : fs::path(args[0]);
    std::error_code ec;
    if (!fs::exists(root, ec)) { Log(L"[ERROR] 대상이 없습니다", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 중복 파일 검색 예정: " + root.wstring(), 2); return; }
    std::map<std::wstring, std::vector<std::wstring>> byHash;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2;
        if (it->is_directory(e2) || it->file_size(e2) == 0) continue;
        std::wstring h = Sha256File(it->path());
        byHash[h].push_back(it->path().wstring());
    }
    std::wstring out; int groups = 0;
    for (auto& kv : byHash) {
        if (kv.second.size() > 1) {
            groups++;
            out += L"[중복그룹 " + std::to_wstring(groups) + L"]\n";
            for (auto& f : kv.second) out += L"  " + f + L"\n";
        }
    }
    Log(groups == 0 ? L"중복 파일 없음." : out, 1);
}

static void CmdEmptyFinder(std::vector<std::wstring>& args, bool dryRun) {
    fs::path root = args.empty() ? fs::current_path() : fs::path(args[0]);
    std::error_code ec;
    if (!fs::exists(root, ec)) { Log(L"[ERROR] 대상이 없습니다", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 빈 폴더 검색 예정: " + root.wstring(), 2); return; }
    std::wstring out; int count = 0;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2;
        if (it->is_directory(e2) && fs::is_empty(it->path(), e2)) { out += it->path().wstring() + L"\n"; count++; }
    }
    Log(count == 0 ? L"빈 폴더 없음." : (L"빈 폴더 " + std::to_wstring(count) + L"개:\n" + out), 1);
}

// ============================================================================
// [자동화] Clean / Compress / Extract / Watch / Schedule / Macro / Batch
// ============================================================================
static void CmdClean(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Clean(\"확장자\",\"경로\")", 3); return; }
    std::wstring ext = args[0]; if (!ext.empty() && ext[0] != L'.') ext = L"." + ext;
    ext = ToLowerW(ext);
    fs::path root = args.size() >= 2 ? fs::path(args[1]) : fs::current_path();
    std::vector<fs::path> targets; std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2;
        if (!it->is_directory(e2) && ToLowerW(it->path().extension().wstring()) == ext) targets.push_back(it->path());
    }
    if (dryRun) {
        std::wstring out = L"[DRY-RUN] " + std::to_wstring(targets.size()) + L"개 파일 삭제 예정:\n";
        for (auto& t : targets) out += t.wstring() + L"\n";
        Log(out, 2); return;
    }
    int count = 0;
    for (auto& t : targets) {
        std::wstring full = fs::absolute(t).wstring();
        full.push_back(L'\0'); full.push_back(L'\0');
        SHFILEOPSTRUCTW op{}; op.wFunc = FO_DELETE; op.pFrom = full.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
        if (SHFileOperationW(&op) == 0) count++;
    }
    Log(L"완료: " + std::to_wstring(count) + L"개 파일 정리(휴지통 이동) -> *" + ext, 1);
}

static void CmdCompress(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Compress(\"폴더\",\"파일.zip\")", 3); return; }
    fs::path src(args[0]);
    std::error_code ec;
    if (!fs::exists(src, ec)) { Log(L"[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 압축 예정: " + args[0] + L" -> " + args[1], 2); return; }
    std::vector<fs::path> files;
    bool isDir = fs::is_directory(src, ec);
    if (isDir) {
        for (auto it = fs::recursive_directory_iterator(src, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
            std::error_code e2; if (!it->is_directory(e2)) files.push_back(it->path());
        }
    } else files.push_back(src);

    std::ofstream ofs(args[1], std::ios::binary);
    struct CentralEntry { std::string name; uint32_t crc; uint32_t size; uint32_t offset; };
    std::vector<CentralEntry> centrals;
    for (auto& f : files) {
        std::wstring rel = isDir ? fs::relative(f, src).wstring() : f.filename().wstring();
        for (auto& c : rel) if (c == L'\\') c = L'/';
        std::string nameUtf8 = WStringToUtf8(rel);
        std::ifstream inf(f, std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(inf)), std::istreambuf_iterator<char>());
        uint32_t crc = Crc32((const unsigned char*)data.data(), data.size());
        uint32_t offset = (uint32_t)ofs.tellp();
        uint32_t sig = 0x04034b50; uint16_t v = 20, fl = 0, meth = 0, mt = 0, md = 0;
        uint16_t nameLen = (uint16_t)nameUtf8.size(), extraLen = 0;
        uint32_t sz = (uint32_t)data.size();
        ofs.write((char*)&sig, 4); ofs.write((char*)&v, 2); ofs.write((char*)&fl, 2); ofs.write((char*)&meth, 2);
        ofs.write((char*)&mt, 2); ofs.write((char*)&md, 2);
        ofs.write((char*)&crc, 4);
        ofs.write((char*)&sz, 4); ofs.write((char*)&sz, 4);
        ofs.write((char*)&nameLen, 2); ofs.write((char*)&extraLen, 2);
        ofs.write(nameUtf8.data(), (std::streamsize)nameUtf8.size());
        ofs.write(data.data(), (std::streamsize)data.size());
        centrals.push_back({ nameUtf8, crc, sz, offset });
    }
    uint32_t centralStart = (uint32_t)ofs.tellp();
    for (auto& c : centrals) {
        uint32_t sig = 0x02014b50; uint16_t vm = 20, vn = 20, fl = 0, meth = 0, mt = 0, md = 0;
        uint16_t nameLen = (uint16_t)c.name.size(), extraLen = 0, commentLen = 0, diskStart = 0, intAttr = 0;
        uint32_t extAttr = 0;
        ofs.write((char*)&sig, 4); ofs.write((char*)&vm, 2); ofs.write((char*)&vn, 2); ofs.write((char*)&fl, 2); ofs.write((char*)&meth, 2);
        ofs.write((char*)&mt, 2); ofs.write((char*)&md, 2);
        ofs.write((char*)&c.crc, 4);
        ofs.write((char*)&c.size, 4); ofs.write((char*)&c.size, 4);
        ofs.write((char*)&nameLen, 2); ofs.write((char*)&extraLen, 2); ofs.write((char*)&commentLen, 2);
        ofs.write((char*)&diskStart, 2); ofs.write((char*)&intAttr, 2); ofs.write((char*)&extAttr, 4);
        ofs.write((char*)&c.offset, 4);
        ofs.write(c.name.data(), (std::streamsize)c.name.size());
    }
    uint32_t centralSize = (uint32_t)ofs.tellp() - centralStart;
    uint32_t endSig = 0x06054b50; uint16_t diskNum = 0, diskStart2 = 0;
    uint16_t entriesDisk = (uint16_t)centrals.size(), entriesTotal = (uint16_t)centrals.size(), commentLen2 = 0;
    ofs.write((char*)&endSig, 4); ofs.write((char*)&diskNum, 2); ofs.write((char*)&diskStart2, 2);
    ofs.write((char*)&entriesDisk, 2); ofs.write((char*)&entriesTotal, 2);
    ofs.write((char*)&centralSize, 4); ofs.write((char*)&centralStart, 4);
    ofs.write((char*)&commentLen2, 2);
    ofs.close();
    Log(L"완료: 압축됨 -> " + args[1] + L" (" + std::to_wstring(files.size()) + L"개 파일, 무압축 저장방식)", 1);
}

static void CmdExtract(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Extract(\"파일.zip\",\"폴더\")", 3); return; }
    fs::path zipPath(args[0]);
    std::error_code ec;
    if (!fs::exists(zipPath, ec)) { Log(L"[ERROR] 압축파일이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 압축 해제 예정: " + args[0] + L" -> " + args[1], 2); return; }
    std::ifstream ifs(zipPath, std::ios::binary);
    std::vector<char> all((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    fs::path destDir(args[1]);
    fs::create_directories(destDir, ec);
    size_t pos = 0; int count = 0, skipped = 0;
    while (pos + 30 <= all.size()) {
        uint32_t sig; memcpy(&sig, &all[pos], 4);
        if (sig != 0x04034b50) break;
        uint16_t method, nameLen, extraLen; uint32_t compSize;
        memcpy(&method, &all[pos + 8], 2);
        memcpy(&compSize, &all[pos + 18], 4);
        memcpy(&nameLen, &all[pos + 26], 2);
        memcpy(&extraLen, &all[pos + 28], 2);
        size_t nameStart = pos + 30;
        if (nameStart + nameLen > all.size()) break;
        std::string name(all.begin() + nameStart, all.begin() + nameStart + nameLen);
        size_t dataStart = nameStart + nameLen + extraLen;
        if (dataStart + compSize > all.size()) break;
        if (method == 0) {
            std::wstring wname = Utf8ToWide(name);
            fs::path outPath = destDir / wname;
            if (!name.empty() && name.back() == '/') fs::create_directories(outPath, ec);
            else {
                fs::create_directories(outPath.parent_path(), ec);
                std::ofstream o(outPath, std::ios::binary);
                o.write(&all[dataStart], compSize);
            }
            count++;
        } else skipped++;
        pos = dataStart + compSize;
    }
    Log(L"완료: 압축 해제됨 -> " + args[1] + L" (" + std::to_wstring(count) + L"개, 미지원(압축방식) " + std::to_wstring(skipped) + L"개)", 1);
}

struct WatchParam { std::wstring folder; std::wstring command; };
static DWORD WINAPI WatchThreadProc(LPVOID lp) {
    WatchParam* wp = (WatchParam*)lp;
    HANDLE hDir = CreateFileW(wp->folder.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (hDir == INVALID_HANDLE_VALUE) { Log(L"[ERROR] Watch: 폴더를 열 수 없습니다 -> " + wp->folder, 3); delete wp; return 0; }
    BYTE buffer[4096]; DWORD bytesReturned;
    while (true) {
        BOOL ok = ReadDirectoryChangesW(hDir, buffer, sizeof(buffer), TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytesReturned, nullptr, nullptr);
        if (!ok) break;
        Log(L"[Watch] 변경 감지: " + wp->folder + L" -> 실행: " + wp->command, 2);
        ExecuteLine(wp->command);
        PostMessageW(g_hwnd, WM_JAT_REFRESH, 0, 0);
    }
    CloseHandle(hDir);
    delete wp;
    return 0;
}
static void CmdWatch(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Watch(\"폴더경로\",\"실행할명령어\")", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 감시 시작 예정: " + args[0], 2); return; }
    auto* wp = new WatchParam{ args[0], args[1] };
    HANDLE h = CreateThread(nullptr, 0, WatchThreadProc, wp, 0, nullptr);
    if (h) CloseHandle(h);
    Log(L"완료: 감시 시작됨 -> " + args[0] + L" (변경시 실행: " + args[1] + L")", 1);
}

struct ScheduleParam { int seconds; std::wstring command; };
static DWORD WINAPI ScheduleThreadProc(LPVOID lp) {
    ScheduleParam* sp = (ScheduleParam*)lp;
    Sleep((DWORD)sp->seconds * 1000);
    Log(L"[Schedule] 예약 실행: " + sp->command, 2);
    ExecuteLine(sp->command);
    PostMessageW(g_hwnd, WM_JAT_REFRESH, 0, 0);
    delete sp;
    return 0;
}
static void CmdSchedule(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Schedule(\"10s|5m|1h\",\"실행할명령어\")", 3); return; }
    int seconds = 0;
    std::wstring t = args[0];
    if (!t.empty()) {
        wchar_t unit = t.back();
        if (unit == L's' || unit == L'm' || unit == L'h') {
            int val = _wtoi(t.substr(0, t.size() - 1).c_str());
            seconds = (unit == L's') ? val : (unit == L'm') ? val * 60 : val * 3600;
        } else seconds = _wtoi(t.c_str());
    }
    if (seconds <= 0) { Log(L"[ERROR] 시간 형식 오류. 예: 10s, 5m, 1h", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] " + std::to_wstring(seconds) + L"초 후 실행 예정: " + args[1], 2); return; }
    auto* sp = new ScheduleParam{ seconds, args[1] };
    HANDLE h = CreateThread(nullptr, 0, ScheduleThreadProc, sp, 0, nullptr);
    if (h) CloseHandle(h);
    Log(L"완료: " + std::to_wstring(seconds) + L"초 후 실행 예약됨 -> " + args[1], 1);
}

static void CmdMacro(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Macro(\"이름\",[명령어1,명령어2,...])", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 매크로 등록 예정: " + args[0], 2); return; }
    auto steps = ParseBracketList(args[1]);
    std::vector<std::wstring> trimmed;
    for (auto& s : steps) { auto t = Trim(s); if (!t.empty()) trimmed.push_back(t); }
    g_macros[args[0]] = trimmed;
    Log(L"완료: 매크로 등록됨 -> " + args[0] + L"() (" + std::to_wstring(trimmed.size()) + L"단계)", 1);
}

static void CmdBatch(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Batch([명령어1,명령어2,...])", 3); return; }
    auto steps = ParseBracketList(args[0]);
    if (dryRun) { Log(L"[DRY-RUN] " + std::to_wstring(steps.size()) + L"개 명령 순차 실행 예정", 2); return; }
    for (auto& s : steps) { auto t = Trim(s); if (!t.empty()) ExecuteLine(t); }
    Log(L"완료: Batch 실행됨 (" + std::to_wstring(steps.size()) + L"단계)", 1);
}

static void CmdAlias(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Alias(\"이름\",\"명령어\")", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 별칭 등록 예정: " + args[0], 2); return; }
    g_macros[args[0]] = { args[1] };
    Log(L"완료: 별칭 등록됨 -> " + args[0] + L"() 실행시 " + args[1], 1);
}

// ============================================================================
// [시스템/프로세스] Status / Monitor / Run / Kill / Service / Startup / Doctor / Repair / Optimize / Benchmark
// ============================================================================
static double GetCpuUsagePercent() {
    FILETIME idle1, kernel1, user1, idle2, kernel2, user2;
    GetSystemTimes(&idle1, &kernel1, &user1);
    Sleep(200);
    GetSystemTimes(&idle2, &kernel2, &user2);
    auto toULL = [](FILETIME f) { return (((ULONGLONG)f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    ULONGLONG idle = toULL(idle2) - toULL(idle1);
    ULONGLONG kernelD = toULL(kernel2) - toULL(kernel1);
    ULONGLONG userD = toULL(user2) - toULL(user1);
    ULONGLONG total = kernelD + userD;
    return total == 0 ? 0.0 : (1.0 - (double)idle / (double)total) * 100.0;
}

static void CmdStatus(std::vector<std::wstring>&, bool dryRun) {
    if (dryRun) { Log(L"[DRY-RUN] 시스템 상태 조회 예정", 2); return; }
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    double cpu = GetCpuUsagePercent();
    ULONGLONG totalMB = ms.ullTotalPhys / (1024 * 1024), availMB = ms.ullAvailPhys / (1024 * 1024), usedMB = totalMB - availMB;
    ULONGLONG uptimeSec = GetTickCount64() / 1000;
    ULARGE_INTEGER freeB, totalB;
    GetDiskFreeSpaceExW(L"C:\\", &freeB, &totalB, nullptr);
    wchar_t buf[512];
    swprintf(buf, 512, L"CPU 사용률: %.1f%%\nRAM: %llu MB / %llu MB 사용중 (부하 %lu%%)\nC: 여유 %.1fGB / %.1fGB\n가동시간: %llu시간 %llu분",
        cpu, usedMB, totalMB, ms.dwMemoryLoad, freeB.QuadPart / 1073741824.0, totalB.QuadPart / 1073741824.0, uptimeSec / 3600, (uptimeSec % 3600) / 60);
    Log(buf, 1);
}

static void CmdMonitor(std::vector<std::wstring>&, bool dryRun) {
    if (dryRun) { Log(L"[DRY-RUN] 실시간 모니터링(5회 샘플) 예정", 2); return; }
    for (int i = 0; i < 5; i++) {
        MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms); GlobalMemoryStatusEx(&ms);
        double cpu = GetCpuUsagePercent();
        ULARGE_INTEGER freeB, totalB;
        GetDiskFreeSpaceExW(L"C:\\", &freeB, &totalB, nullptr);
        wchar_t buf[256];
        swprintf(buf, 256, L"[%d/5] CPU %.1f%% | RAM %lu%% | C: 여유 %.1fGB", i + 1, cpu, ms.dwMemoryLoad, freeB.QuadPart / 1073741824.0);
        Log(buf, 1);
        Sleep(600);
    }
}

static void CmdRun(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Run(\"프로그램이름\")", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 실행 예정: " + args[0], 2); return; }
    HINSTANCE r = ShellExecuteW(nullptr, L"open", args[0].c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) { Log(L"[ERROR] 실행 실패: " + args[0], 3); return; }
    Log(L"완료: 실행됨 -> " + args[0], 1);
}

static void CmdKill(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Kill(\"프로그램이름\")", 3); return; }
    std::wstring target = ToLowerW(args[0]);
    if (target.find(L".exe") == std::wstring::npos) target += L".exe";
    if (dryRun) { Log(L"[DRY-RUN] 프로세스 종료 예정: " + args[0], 2); return; }
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    int killedCount = 0;
    if (snap != INVALID_HANDLE_VALUE && Process32FirstW(snap, &pe)) {
        do {
            if (ToLowerW(pe.szExeFile) == target) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) { TerminateProcess(h, 0); CloseHandle(h); killedCount++; }
            }
        } while (Process32NextW(snap, &pe));
    }
    if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
    if (killedCount > 0) Log(L"완료: " + std::to_wstring(killedCount) + L"개 프로세스 종료됨 -> " + args[0], 1);
    else Log(L"[ERROR] 프로세스를 찾을 수 없습니다: " + args[0], 3);
}

static void CmdService(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Service(\"이름\",\"start|stop|restart|status\")", 3); return; }
    std::wstring action = args.size() >= 2 ? ToLowerW(args[1]) : L"status";
    if (dryRun) { Log(L"[DRY-RUN] 서비스 " + action + L" 예정: " + args[0], 2); return; }
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) { Log(L"[ERROR] 서비스 관리자 접근 실패 (관리자 권한이 필요할 수 있습니다)", 3); return; }
    SC_HANDLE svc = OpenServiceW(scm, args[0].c_str(), SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!svc) { Log(L"[ERROR] 서비스를 찾을 수 없습니다: " + args[0], 3); CloseServiceHandle(scm); return; }
    SERVICE_STATUS_PROCESS ssp{}; DWORD bytesNeeded = 0;
    if (action == L"start") { StartServiceW(svc, 0, nullptr); Log(L"완료: 서비스 시작 요청됨 -> " + args[0], 1); }
    else if (action == L"stop") { SERVICE_STATUS st{}; ControlService(svc, SERVICE_CONTROL_STOP, &st); Log(L"완료: 서비스 중지 요청됨 -> " + args[0], 1); }
    else if (action == L"restart") { SERVICE_STATUS st{}; ControlService(svc, SERVICE_CONTROL_STOP, &st); Sleep(1000); StartServiceW(svc, 0, nullptr); Log(L"완료: 서비스 재시작됨 -> " + args[0], 1); }
    else {
        QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded);
        std::wstring state = ssp.dwCurrentState == SERVICE_RUNNING ? L"실행중" : ssp.dwCurrentState == SERVICE_STOPPED ? L"정지됨" : L"전환중";
        Log(L"서비스 상태: " + args[0] + L" -> " + state, 1);
    }
    CloseServiceHandle(svc); CloseServiceHandle(scm);
}

static void ListRunKey(HKEY root, const wchar_t* rootName, std::wstring& out) {
    HKEY hKey;
    if (RegOpenKeyExW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t name[256]; wchar_t data[512]; DWORD idx = 0, nameLen, dataLen, type;
        while (true) {
            nameLen = 256; dataLen = sizeof(data);
            if (RegEnumValueW(hKey, idx, name, &nameLen, nullptr, &type, (LPBYTE)data, &dataLen) != ERROR_SUCCESS) break;
            out += std::wstring(rootName) + L" | " + name + L" = " + data + L"\n";
            idx++;
        }
        RegCloseKey(hKey);
    }
}
static void CmdStartup(std::vector<std::wstring>&, bool dryRun) {
    if (dryRun) { Log(L"[DRY-RUN] 시작프로그램 조회 예정", 2); return; }
    std::wstring out;
    ListRunKey(HKEY_CURRENT_USER, L"HKCU", out);
    ListRunKey(HKEY_LOCAL_MACHINE, L"HKLM", out);
    Log(out.empty() ? L"등록된 시작 프로그램 없음." : out, 1);
}

static void CmdDoctor(std::vector<std::wstring>&, bool dryRun) {
    if (dryRun) { Log(L"[DRY-RUN] 시스템 진단 예정", 2); return; }
    std::wstring out = L"=== jat Doctor 진단 결과 ===\n";
    ULARGE_INTEGER freeB, totalB;
    if (GetDiskFreeSpaceExW(L"C:\\", &freeB, &totalB, nullptr)) {
        double freeGB = freeB.QuadPart / 1073741824.0, totalGB = totalB.QuadPart / 1073741824.0;
        wchar_t b[128];
        swprintf(b, 128, L"%s C: 드라이브 여유공간 %.1fGB / %.1fGB\n", (freeGB < 10.0 ? L"[경고]" : L"[정상]"), freeGB, totalGB);
        out += b;
    }
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms); GlobalMemoryStatusEx(&ms);
    out += (ms.dwMemoryLoad > 90 ? L"[경고] " : L"[정상] ") + (L"메모리 사용률 " + std::to_wstring(ms.dwMemoryLoad) + L"%\n");
    HKEY hKey;
    bool pendingReboot = (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager", 0, KEY_READ, &hKey) == ERROR_SUCCESS);
    if (pendingReboot) {
        DWORD type; BYTE buf[8]; DWORD sz = sizeof(buf);
        bool exists = RegQueryValueExW(hKey, L"PendingFileRenameOperations", nullptr, &type, buf, &sz) == ERROR_SUCCESS;
        RegCloseKey(hKey);
        out += (exists ? L"[경고] 재부팅 대기중인 작업이 있습니다.\n" : L"[정상] 재부팅 대기 없음\n");
    }
    Log(out, 1);
}

static void CmdRepair(std::vector<std::wstring>&, bool dryRun) {
    if (dryRun) { Log(L"[DRY-RUN] 시스템 복구(sfc /scannow) 실행 예정 (관리자 권한 필요)", 2); return; }
    HINSTANCE r = ShellExecuteW(nullptr, L"runas", L"sfc.exe", L"/scannow", nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) { Log(L"[ERROR] 복구 실행 실패 (관리자 권한 필요)", 3); return; }
    Log(L"완료: sfc /scannow 실행됨 (관리자 승인 창을 확인하세요)", 1);
}

static void CmdOptimize(std::vector<std::wstring>&, bool dryRun) {
    if (dryRun) { Log(L"[DRY-RUN] 임시파일 정리 + 휴지통 비우기 예정", 2); return; }
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    int count = 0; std::error_code ec;
    for (auto& entry : fs::directory_iterator(fs::path(tempPath), fs::directory_options::skip_permission_denied, ec)) {
        std::error_code delEc;
        if (fs::is_directory(entry, delEc)) fs::remove_all(entry.path(), delEc);
        else fs::remove(entry.path(), delEc);
        if (!delEc) count++;
    }
    SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    Log(L"완료: 임시파일 " + std::to_wstring(count) + L"개 정리 + 휴지통 비움", 1);
}

static void CmdBenchmark(std::vector<std::wstring>&, bool dryRun) {
    if (dryRun) { Log(L"[DRY-RUN] 디스크 벤치마크 예정", 2); return; }
    fs::path testFile = fs::temp_directory_path() / L"jat_bench_tmp.dat";
    const size_t chunkSize = 1024 * 1024; const int chunks = 50;
    std::vector<char> chunk(chunkSize, 'A');
    auto t1 = std::chrono::high_resolution_clock::now();
    { std::ofstream ofs(testFile, std::ios::binary); for (int i = 0; i < chunks; i++) ofs.write(chunk.data(), chunkSize); }
    auto t2 = std::chrono::high_resolution_clock::now();
    { std::ifstream ifs(testFile, std::ios::binary); std::vector<char> buf(chunkSize); while (ifs.read(buf.data(), chunkSize)) {} }
    auto t3 = std::chrono::high_resolution_clock::now();
    std::error_code ec; fs::remove(testFile, ec);
    double writeSec = std::chrono::duration<double>(t2 - t1).count();
    double readSec = std::chrono::duration<double>(t3 - t2).count();
    wchar_t buf[256];
    swprintf(buf, 256, L"쓰기 속도: %.1f MB/s\n읽기 속도: %.1f MB/s", 50.0 / (writeSec > 0 ? writeSec : 0.001), 50.0 / (readSec > 0 ? readSec : 0.001));
    Log(buf, 1);
}

// ============================================================================
// [네트워크] Download / Upload / Ping / Network / IP / DNS / Port
// ============================================================================
static void CmdDownload(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Download(\"url\",\"저장할파일명\")", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 다운로드 예정: " + args[0] + L" -> " + args[1], 2); return; }
    HRESULT hr = URLDownloadToFileW(nullptr, args[0].c_str(), args[1].c_str(), 0, nullptr);
    if (FAILED(hr)) { Log(L"[ERROR] 다운로드 실패", 3); return; }
    Log(L"완료: 다운로드됨 -> " + args[1], 1);
}

static void CmdUpload(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Upload(\"파일\",\"서버URL\")", 3); return; }
    fs::path p(args[0]);
    if (!fs::exists(p)) { Log(L"[ERROR] 파일이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 업로드 예정: " + args[0] + L" -> " + args[1], 2); return; }
    URL_COMPONENTSW uc{}; uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = { 0 }, path[1024] = { 0 };
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
    if (!InternetCrackUrlW(args[1].c_str(), 0, 0, &uc)) { Log(L"[ERROR] URL 파싱 실패", 3); return; }
    HINTERNET hInet = InternetOpenW(L"jatTerminal", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
    HINTERNET hConn = InternetConnectW(hInet, host, uc.nPort, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
    DWORD flags = INTERNET_FLAG_KEEP_CONNECTION | (uc.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_FLAG_SECURE : 0);
    HINTERNET hReq = HttpOpenRequestW(hConn, L"PUT", path, nullptr, nullptr, nullptr, flags, 0);
    std::ifstream f(p, std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    BOOL ok = HttpSendRequestW(hReq, nullptr, 0, data.data(), (DWORD)data.size());
    if (hReq) InternetCloseHandle(hReq); if (hConn) InternetCloseHandle(hConn); if (hInet) InternetCloseHandle(hInet);
    if (!ok) { Log(L"[ERROR] 업로드 실패", 3); return; }
    Log(L"완료: 업로드됨 -> " + args[1], 1);
}

static void CmdPing(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Ping(\"주소\")", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] Ping 예정: " + args[0], 2); return; }
    HANDLE hIcmp = IcmpCreateFile();
    if (hIcmp == INVALID_HANDLE_VALUE) { Log(L"[ERROR] ICMP 초기화 실패", 3); return; }
    ADDRINFOW hints{}; hints.ai_family = AF_INET;
    ADDRINFOW* result = nullptr;
    if (GetAddrInfoW(args[0].c_str(), nullptr, &hints, &result) != 0) { Log(L"[ERROR] 주소 확인 실패: " + args[0], 3); IcmpCloseHandle(hIcmp); return; }
    sockaddr_in* sa = (sockaddr_in*)result->ai_addr;
    IPAddr ipaddr = sa->sin_addr.S_un.S_addr;
    FreeAddrInfoW(result);
    char sendData[32] = "jatTerminalPing";
    BYTE replyBuf[sizeof(ICMP_ECHO_REPLY) + 32];
    DWORD ret = IcmpSendEcho(hIcmp, ipaddr, sendData, sizeof(sendData), nullptr, replyBuf, sizeof(replyBuf), 1000);
    if (ret == 0) Log(L"[ERROR] 응답 없음 (타임아웃): " + args[0], 3);
    else {
        PICMP_ECHO_REPLY reply = (PICMP_ECHO_REPLY)replyBuf;
        Log(L"완료: " + args[0] + L" 응답시간 " + std::to_wstring(reply->RoundTripTime) + L"ms (상태코드 " + std::to_wstring(reply->Status) + L")", 1);
    }
    IcmpCloseHandle(hIcmp);
}

static void CmdIP(std::vector<std::wstring>&, bool dryRun) {
    if (dryRun) { Log(L"[DRY-RUN] IP 조회 예정", 2); return; }
    ULONG bufLen = 15000; std::vector<BYTE> buf(bufLen);
    auto addrs = (PIP_ADAPTER_ADDRESSES)buf.data();
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, addrs, &bufLen) != NO_ERROR) {
        Log(L"[ERROR] 어댑터 정보를 가져올 수 없습니다", 3); return;
    }
    std::wstring out;
    for (auto p = addrs; p; p = p->Next) {
        if (p->OperStatus != IfOperStatusUp) continue;
        for (auto ua = p->FirstUnicastAddress; ua; ua = ua->Next) {
            sockaddr_in* sa = (sockaddr_in*)ua->Address.lpSockaddr;
            wchar_t ipStr[64];
            InetNtopW(AF_INET, &sa->sin_addr, ipStr, 64);
            out += std::wstring(p->FriendlyName) + L": " + ipStr + L"\n";
        }
    }
    Log(out.empty() ? L"활성 IP를 찾을 수 없습니다." : out, 1);
}

static void CmdNetwork(std::vector<std::wstring>&, bool dryRun) {
    if (dryRun) { Log(L"[DRY-RUN] 네트워크 상태 조회 예정", 2); return; }
    ULONG bufLen = 15000; std::vector<BYTE> buf(bufLen);
    auto addrs = (PIP_ADAPTER_ADDRESSES)buf.data();
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, addrs, &bufLen) != NO_ERROR) {
        Log(L"[ERROR] 어댑터 정보를 가져올 수 없습니다", 3); return;
    }
    std::wstring out;
    for (auto p = addrs; p; p = p->Next) {
        out += std::wstring(p->FriendlyName) + L" : " + (p->OperStatus == IfOperStatusUp ? L"연결됨" : L"끊김") + L"\n";
    }
    Log(out.empty() ? L"어댑터 없음." : out, 1);
}

static void CmdDNS(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: DNS(\"도메인\")", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] DNS 조회 예정: " + args[0], 2); return; }
    ADDRINFOW hints{}; hints.ai_family = AF_UNSPEC;
    ADDRINFOW* result = nullptr;
    if (GetAddrInfoW(args[0].c_str(), nullptr, &hints, &result) != 0) { Log(L"[ERROR] 조회 실패: " + args[0], 3); return; }
    std::wstring out;
    for (auto p = result; p; p = p->ai_next) {
        wchar_t ipStr[64];
        if (p->ai_family == AF_INET) { auto sa = (sockaddr_in*)p->ai_addr; InetNtopW(AF_INET, &sa->sin_addr, ipStr, 64); out += std::wstring(L"IPv4: ") + ipStr + L"\n"; }
        else if (p->ai_family == AF_INET6) { auto sa = (sockaddr_in6*)p->ai_addr; InetNtopW(AF_INET6, &sa->sin6_addr, ipStr, 64); out += std::wstring(L"IPv6: ") + ipStr + L"\n"; }
    }
    FreeAddrInfoW(result);
    Log(out.empty() ? L"결과 없음" : out, 1);
}

static void CmdPort(std::vector<std::wstring>&, bool dryRun) {
    if (dryRun) { Log(L"[DRY-RUN] 열린 포트 조회 예정", 2); return; }
    ULONG size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    std::vector<BYTE> buf(size);
    auto table = (PMIB_TCPTABLE_OWNER_PID)buf.data();
    std::wstring out;
    if (GetExtendedTcpTable(table, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) == NO_ERROR) {
        for (DWORD i = 0; i < table->dwNumEntries; i++) {
            DWORD port = ntohs((u_short)table->table[i].dwLocalPort);
            out += L"포트 " + std::to_wstring(port) + L" (PID " + std::to_wstring(table->table[i].dwOwningPid) + L")\n";
        }
    }
    Log(out.empty() ? L"열린 포트 없음" : out, 1);
}

// ============================================================================
// [보안] Checksum / HashCompare / Encrypt / Decrypt
// ============================================================================
static void CmdChecksum(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Checksum(\"파일\")", 3); return; }
    fs::path p(args[0]);
    if (!fs::exists(p)) { Log(L"[ERROR] 파일이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 해시 계산 예정: " + args[0], 2); return; }
    Log(L"SHA-256: " + Sha256File(p), 1);
}

static void CmdHashCompare(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: HashCompare(\"파일1\",\"파일2\")", 3); return; }
    fs::path p1(args[0]), p2(args[1]);
    if (!fs::exists(p1) || !fs::exists(p2)) { Log(L"[ERROR] 파일을 찾을 수 없습니다", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 해시 비교 예정", 2); return; }
    Log(Sha256File(p1) == Sha256File(p2) ? L"결과: 동일한 파일입니다 (해시 일치)" : L"결과: 다른 파일입니다 (해시 불일치)", 1);
}

static void CmdEncrypt(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Encrypt(\"파일\",\"비밀번호\")", 3); return; }
    fs::path p(args[0]);
    if (!fs::exists(p)) { Log(L"[ERROR] 파일이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 암호화 예정: " + args[0], 2); return; }
    std::ifstream f(p, std::ios::binary);
    std::vector<BYTE> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    BYTE salt[16], iv[16];
    BCRYPT_ALG_HANDLE hRng = nullptr;
    BCryptOpenAlgorithmProvider(&hRng, BCRYPT_RNG_ALGORITHM, nullptr, 0);
    BCryptGenRandom(hRng, salt, 16, 0);
    BCryptGenRandom(hRng, iv, 16, 0);
    BCryptCloseAlgorithmProvider(hRng, 0);
    BYTE key[32];
    if (!DeriveKey(args[1], salt, 16, key, 32)) { Log(L"[ERROR] 키 생성 실패", 3); return; }
    BCRYPT_ALG_HANDLE hAes = nullptr; BCRYPT_KEY_HANDLE hKey = nullptr;
    BCryptOpenAlgorithmProvider(&hAes, BCRYPT_AES_ALGORITHM, nullptr, 0);
    BCryptSetProperty(hAes, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    BCryptGenerateSymmetricKey(hAes, &hKey, nullptr, 0, key, 32, 0);
    DWORD outLen = 0; BYTE ivCopy[16];
    memcpy(ivCopy, iv, 16);
    BCryptEncrypt(hKey, data.data(), (ULONG)data.size(), nullptr, ivCopy, 16, nullptr, 0, &outLen, BCRYPT_BLOCK_PADDING);
    std::vector<BYTE> outBuf(outLen);
    memcpy(ivCopy, iv, 16);
    DWORD written = 0;
    BCryptEncrypt(hKey, data.data(), (ULONG)data.size(), nullptr, ivCopy, 16, outBuf.data(), outLen, &written, BCRYPT_BLOCK_PADDING);
    BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAes, 0);
    fs::path outPath = p; outPath += L".enc";
    std::ofstream ofs(outPath, std::ios::binary);
    ofs.write((char*)salt, 16); ofs.write((char*)iv, 16); ofs.write((char*)outBuf.data(), written);
    ofs.close();
    Log(L"완료: 암호화됨 -> " + outPath.wstring(), 1);
}

static void CmdDecrypt(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Decrypt(\"파일\",\"비밀번호\")", 3); return; }
    fs::path p(args[0]);
    if (!fs::exists(p)) { Log(L"[ERROR] 파일이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 복호화 예정: " + args[0], 2); return; }
    std::ifstream f(p, std::ios::binary);
    std::vector<BYTE> all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    if (all.size() < 32) { Log(L"[ERROR] 잘못된 암호화 파일 형식", 3); return; }
    BYTE salt[16], iv[16];
    memcpy(salt, all.data(), 16); memcpy(iv, all.data() + 16, 16);
    std::vector<BYTE> cipher(all.begin() + 32, all.end());
    BYTE key[32];
    if (!DeriveKey(args[1], salt, 16, key, 32)) { Log(L"[ERROR] 키 생성 실패", 3); return; }
    BCRYPT_ALG_HANDLE hAes = nullptr; BCRYPT_KEY_HANDLE hKey = nullptr;
    BCryptOpenAlgorithmProvider(&hAes, BCRYPT_AES_ALGORITHM, nullptr, 0);
    BCryptSetProperty(hAes, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    BCryptGenerateSymmetricKey(hAes, &hKey, nullptr, 0, key, 32, 0);
    DWORD outLen = 0; BYTE ivCopy[16];
    memcpy(ivCopy, iv, 16);
    BCryptDecrypt(hKey, cipher.data(), (ULONG)cipher.size(), nullptr, ivCopy, 16, nullptr, 0, &outLen, BCRYPT_BLOCK_PADDING);
    std::vector<BYTE> outBuf(outLen);
    memcpy(ivCopy, iv, 16);
    DWORD written = 0;
    NTSTATUS st = BCryptDecrypt(hKey, cipher.data(), (ULONG)cipher.size(), nullptr, ivCopy, 16, outBuf.data(), outLen, &written, BCRYPT_BLOCK_PADDING);
    BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAes, 0);
    if (st != 0) { Log(L"[ERROR] 복호화 실패 (비밀번호를 확인하세요)", 3); return; }
    std::wstring outName = p.wstring();
    if (outName.size() > 4 && outName.substr(outName.size() - 4) == L".enc") outName = outName.substr(0, outName.size() - 4);
    else outName += L".dec";
    std::ofstream ofs(outName, std::ios::binary);
    ofs.write((char*)outBuf.data(), written);
    ofs.close();
    Log(L"완료: 복호화됨 -> " + outName, 1);
}

// ============================================================================
// [강력한 추가 도구 4종] Diff / Snapshot / Lock,Unlock
//  (Alias는 위쪽 자동화 섹션에 포함됨)
// ============================================================================
static void CmdDiff(std::vector<std::wstring>& args, bool dryRun) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Diff(\"파일1\",\"파일2\")", 3); return; }
    fs::path p1(args[0]), p2(args[1]);
    if (!fs::exists(p1) || !fs::exists(p2)) { Log(L"[ERROR] 파일을 찾을 수 없습니다", 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 비교 예정: " + args[0] + L" vs " + args[1], 2); return; }
    std::wifstream f1(p1), f2(p2);
    std::vector<std::wstring> l1, l2; std::wstring line;
    while (std::getline(f1, line)) l1.push_back(line);
    while (std::getline(f2, line)) l2.push_back(line);
    size_t maxN = (std::max)(l1.size(), l2.size());
    std::wstring out; int diffCount = 0;
    for (size_t i = 0; i < maxN; i++) {
        std::wstring a = i < l1.size() ? l1[i] : L"(없음)";
        std::wstring b = i < l2.size() ? l2[i] : L"(없음)";
        if (a != b) { diffCount++; out += L"L" + std::to_wstring(i + 1) + L": - " + a + L"\n     + " + b + L"\n"; }
    }
    Log(diffCount == 0 ? L"결과: 두 파일이 동일합니다." : (L"차이 " + std::to_wstring(diffCount) + L"줄:\n" + out), 1);
}

static void CmdSnapshot(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Snapshot(\"폴더\")", 3); return; }
    fs::path src(args[0]);
    std::error_code ec;
    if (!fs::exists(src, ec)) { Log(L"[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 스냅샷 생성 예정: " + args[0], 2); return; }
    long long epoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    fs::path snapDir = fs::current_path() / L".jat_snapshots";
    fs::create_directories(snapDir, ec);
    fs::path dest = snapDir / (src.filename().wstring() + L"_" + std::to_wstring(epoch));
    fs::copy(src, dest, fs::copy_options::recursive, ec);
    if (ec) { Log(L"[ERROR] 스냅샷 실패: " + AcpToW(ec.message()), 3); return; }
    Log(L"완료: 스냅샷 생성됨 -> .jat_snapshots/" + dest.filename().wstring(), 1);
}

static void CmdLock(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Lock(\"파일\")", 3); return; }
    fs::path p(args[0]);
    if (!fs::exists(p)) { Log(L"[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 잠금 예정: " + args[0], 2); return; }
    DWORD attr = GetFileAttributesW(p.c_str());
    SetFileAttributesW(p.c_str(), attr | FILE_ATTRIBUTE_READONLY);
    Log(L"완료: 잠금(읽기전용) 설정됨 -> " + args[0], 1);
}
static void CmdUnlock(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Unlock(\"파일\")", 3); return; }
    fs::path p(args[0]);
    if (!fs::exists(p)) { Log(L"[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 잠금 해제 예정: " + args[0], 2); return; }
    DWORD attr = GetFileAttributesW(p.c_str());
    SetFileAttributesW(p.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
    Log(L"완료: 잠금 해제됨 -> " + args[0], 1);
}

// ============================================================================
// [터미널 UX] help / version / clear / history
// ============================================================================
static void CmdHelp() {
    Log(
        L"=== jat Terminal v2 명령어 목록 ===\n"
        L"[파일] Create Update Copy Move Rename Open Duplicate\n"
        L"[삭제] Delete(Lv.1) Remove(Lv.2 30일) Erase(Lv.3 영구)\n"
        L"[정보] Info Tree Size Find Search Replace Analyze DuplicateFinder EmptyFinder\n"
        L"[자동화] Clean Compress Extract Watch Schedule Macro Batch Alias\n"
        L"[시스템] Status Monitor Run Kill Service Startup Doctor Repair Optimize Benchmark\n"
        L"[네트워크] Download Upload Ping Network IP DNS Port\n"
        L"[보안] Checksum HashCompare Encrypt Decrypt\n"
        L"[강력한 추가도구] Diff Snapshot Lock Unlock\n"
        L"[UX] help() version() clear() history()\n"
        L"[안전기호] ?명령어(...) = 미리보기(Dry-run) / $명령어(...) = 무음 실행\n"
        L"※ Compress/Extract 는 무압축(저장방식) ZIP만 지원합니다.",
        1);
}
static void CmdVersion() { Log(L"jat Terminal v2.0.0 (Win32 Native / C++17 / GDI+)", 1); }
static void CmdClear() { EnterCriticalSection(&g_cs); g_logLines.clear(); LeaveCriticalSection(&g_cs); }
static void CmdHistoryShow() {
    if (g_history.empty()) { Log(L"기록 없음.", 2); return; }
    std::wstring out;
    for (size_t i = 0; i < g_history.size(); ++i) out += std::to_wstring(i + 1) + L". " + g_history[i] + L"\n";
    Log(out, 1);
}

// ----------------------------------------------------------------------------
// 명령어 디스패치
// ----------------------------------------------------------------------------
static void ExecuteParsed(const ParsedCommand& pc) {
    std::wstring nl = ToLowerW(pc.name);
    auto args = pc.args;
    try {
        if (nl == L"create") CmdCreateUpdate(args, pc.dryRun, false);
        else if (nl == L"update") CmdCreateUpdate(args, pc.dryRun, true);
        else if (nl == L"copy") CmdCopy(args, pc.dryRun);
        else if (nl == L"move") CmdMove(args, pc.dryRun);
        else if (nl == L"rename") CmdRename(args, pc.dryRun);
        else if (nl == L"open") CmdOpen(args, pc.dryRun);
        else if (nl == L"duplicate") CmdDuplicate(args, pc.dryRun);
        else if (nl == L"delete") CmdDelete(args, pc.dryRun);
        else if (nl == L"remove") CmdRemove(args, pc.dryRun);
        else if (nl == L"erase") CmdErase(args, pc.dryRun);
        else if (nl == L"info") CmdInfo(args, pc.dryRun);
        else if (nl == L"tree") CmdTree(args, pc.dryRun);
        else if (nl == L"size") CmdSize(args, pc.dryRun);
        else if (nl == L"find") CmdFind(args, pc.dryRun);
        else if (nl == L"search") CmdSearch(args, pc.dryRun);
        else if (nl == L"replace") CmdReplace(args, pc.dryRun);
        else if (nl == L"analyze") CmdAnalyze(args, pc.dryRun);
        else if (nl == L"duplicatefinder") CmdDuplicateFinder(args, pc.dryRun);
        else if (nl == L"emptyfinder") CmdEmptyFinder(args, pc.dryRun);
        else if (nl == L"clean") CmdClean(args, pc.dryRun);
        else if (nl == L"compress") CmdCompress(args, pc.dryRun);
        else if (nl == L"extract") CmdExtract(args, pc.dryRun);
        else if (nl == L"watch") CmdWatch(args, pc.dryRun);
        else if (nl == L"schedule") CmdSchedule(args, pc.dryRun);
        else if (nl == L"macro") CmdMacro(args, pc.dryRun);
        else if (nl == L"batch") CmdBatch(args, pc.dryRun);
        else if (nl == L"alias") CmdAlias(args, pc.dryRun);
        else if (nl == L"status") CmdStatus(args, pc.dryRun);
        else if (nl == L"monitor") CmdMonitor(args, pc.dryRun);
        else if (nl == L"run") CmdRun(args, pc.dryRun);
        else if (nl == L"kill") CmdKill(args, pc.dryRun);
        else if (nl == L"service") CmdService(args, pc.dryRun);
        else if (nl == L"startup") CmdStartup(args, pc.dryRun);
        else if (nl == L"doctor") CmdDoctor(args, pc.dryRun);
        else if (nl == L"repair") CmdRepair(args, pc.dryRun);
        else if (nl == L"optimize") CmdOptimize(args, pc.dryRun);
        else if (nl == L"benchmark") CmdBenchmark(args, pc.dryRun);
        else if (nl == L"download") CmdDownload(args, pc.dryRun);
        else if (nl == L"upload") CmdUpload(args, pc.dryRun);
        else if (nl == L"ping") CmdPing(args, pc.dryRun);
        else if (nl == L"network") CmdNetwork(args, pc.dryRun);
        else if (nl == L"ip") CmdIP(args, pc.dryRun);
        else if (nl == L"dns") CmdDNS(args, pc.dryRun);
        else if (nl == L"port") CmdPort(args, pc.dryRun);
        else if (nl == L"checksum") CmdChecksum(args, pc.dryRun);
        else if (nl == L"hashcompare") CmdHashCompare(args, pc.dryRun);
        else if (nl == L"encrypt") CmdEncrypt(args, pc.dryRun);
        else if (nl == L"decrypt") CmdDecrypt(args, pc.dryRun);
        else if (nl == L"diff") CmdDiff(args, pc.dryRun);
        else if (nl == L"snapshot") CmdSnapshot(args, pc.dryRun);
        else if (nl == L"lock") CmdLock(args, pc.dryRun);
        else if (nl == L"unlock") CmdUnlock(args, pc.dryRun);
        else if (nl == L"help") CmdHelp();
        else if (nl == L"version") CmdVersion();
        else if (nl == L"clear") CmdClear();
        else if (nl == L"history") CmdHistoryShow();
        else {
            auto mit = g_macros.find(pc.name);
            if (mit != g_macros.end()) {
                if (pc.dryRun) Log(L"[DRY-RUN] 매크로 실행 예정: " + pc.name + L" (" + std::to_wstring(mit->second.size()) + L"단계)", 2);
                else for (auto& step : mit->second) ExecuteLine(step);
            } else Log(L"[ERROR] 알 수 없는 명령어: " + pc.name, 3);
        }
    } catch (std::exception& e) {
        Log(L"[ERROR] " + AcpToW(e.what()), 3);
    }
}

static void ExecuteLine(const std::wstring& raw) {
    std::wstring line = Trim(raw);
    if (line.empty()) return;
    ParsedCommand pc = ParseCommand(line);
    if (!pc.silent) Log(L"jat> " + line, 1);
    if (!pc.valid) { Log(L"[ERROR] " + pc.error, 3); return; }
    ExecuteParsed(pc);
}

// ----------------------------------------------------------------------------
// GDI+ 렌더링 (레이어드 윈도우, 둥근 모서리 + 초록 글로우)
// ----------------------------------------------------------------------------
static void AddRoundedRect(GraphicsPath& path, float x, float y, float w, float h, float r) {
    path.Reset();
    path.AddArc(x, y, r * 2, r * 2, 180, 90);
    path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
    path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();
}

struct HitRects { RectF close, minimize, dragZone, inputZone; };
static HitRects g_hit;

static void Render() {
    Bitmap bmp(WIN_W, WIN_H, PixelFormat32bppPArgb);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));

    float m = (float)MARGIN, bt = (float)BORDER_T, r = (float)RADIUS;

    GraphicsPath glowPath;
    for (int i = 10; i >= 1; --i) {
        Pen pen(Color(10 * i, 110, 255, 170), (float)i * 1.6f);
        AddRoundedRect(glowPath, m - i, m - i, (float)WIN_W - 2 * (m - i), (float)WIN_H - 2 * (m - i), r + i);
        g.DrawPath(&pen, &glowPath);
    }

    GraphicsPath outerPath;
    AddRoundedRect(outerPath, m, m, (float)WIN_W - 2 * m, (float)WIN_H - 2 * m, r);
    Pen borderPen(COL_GREEN, bt);
    g.DrawPath(&borderPen, &outerPath);

    GraphicsPath innerPath;
    float ix = m + bt, iy = m + bt, iw = (float)WIN_W - 2 * (m + bt), ih = (float)WIN_H - 2 * (m + bt);
    AddRoundedRect(innerPath, ix, iy, iw, ih, r - bt > 0 ? r - bt : 2);
    Region innerRegion(&innerPath);
    g.SetClip(&innerRegion);
    SolidBrush blackBrush(COL_BLACK_BG);
    g.FillPath(&blackBrush, &innerPath);

    RectF titleRect(ix, iy, iw, (float)TITLE_H);
    SolidBrush titleBrush(COL_GRAY_TITLE);
    g.FillRectangle(&titleBrush, titleRect);

    float cy = iy + TITLE_H / 2.0f;
    float cx0 = ix + 22, gap = 26, crad = 7;
    SolidBrush c1(COL_GRAY_C1), c2(COL_GRAY_C2), c3(COL_GRAY_C3);
    g.FillEllipse(&c1, cx0 - crad, cy - crad, crad * 2, crad * 2);
    g.FillEllipse(&c2, cx0 + gap - crad, cy - crad, crad * 2, crad * 2);
    g.FillEllipse(&c3, cx0 + gap * 2 - crad, cy - crad, crad * 2, crad * 2);

    float titleTextX = cx0 + gap * 3;
    if (g_logo) {
        float logoH = TITLE_H - 14.0f, logoW = logoH;
        g.DrawImage(g_logo, ix + gap * 3 + 6, iy + 7, logoW, logoH);
        titleTextX = ix + gap * 3 + 6 + logoW + 8;
    }

    FontFamily ffTitle(L"Consolas");
    Font titleFont(&ffTitle, 15, FontStyleBold, UnitPixel);
    SolidBrush whiteBrush(COL_WHITE);
    g.DrawString(L"jat Terminal", -1, &titleFont, PointF(titleTextX, iy + 13), &whiteBrush);

    float btnSize = 20.0f;
    RectF closeRect(ix + iw - btnSize - 16, iy + (TITLE_H - btnSize) / 2, btnSize, btnSize);
    RectF minRect(ix + iw - btnSize * 2 - 30, iy + (TITLE_H - btnSize) / 2, btnSize, btnSize);
    g_hit.close = closeRect;
    g_hit.minimize = minRect;
    Pen greenThin(COL_GREEN, 2.0f);
    g.DrawLine(&greenThin, closeRect.X + 5, closeRect.Y + 5, closeRect.GetRight() - 5, closeRect.GetBottom() - 5);
    g.DrawLine(&greenThin, closeRect.GetRight() - 5, closeRect.Y + 5, closeRect.X + 5, closeRect.GetBottom() - 5);
    g.DrawLine(&greenThin, minRect.X + 4, minRect.Y + minRect.Height / 2, minRect.GetRight() - 4, minRect.Y + minRect.Height / 2);

    g_hit.dragZone = RectF(ix, iy, iw - btnSize * 2 - 40, (float)TITLE_H);

    float logY = iy + TITLE_H;
    float logH = ih - TITLE_H - INPUT_H;
    RectF logRect(ix, logY, iw, logH);
    Region logRegion(logRect);
    g.SetClip(&innerRegion);
    g.SetClip(&logRegion, CombineModeIntersect);

    FontFamily ffMono(L"Consolas");
    Font monoFont(&ffMono, 13, FontStyleRegular, UnitPixel);
    float lineH = 18.0f;
    int maxLines = (int)(logH / lineH);

    SolidBrush brWhite(COL_WHITE), brGreen(COL_GREEN), brGray(COL_GRAY_TEXT);

    EnterCriticalSection(&g_cs);
    int total = (int)g_logLines.size();
    int start = total - maxLines - g_scrollOffset; if (start < 0) start = 0;
    int end = total - g_scrollOffset; if (end > total) end = total; if (end < 0) end = 0;
    float ty = logY + logH - lineH;
    for (int i = end - 1; i >= start; --i) {
        if (ty < logY - lineH) break;
        auto& ll = g_logLines[i];
        Brush* br = &brWhite;
        if (ll.color == 1) br = &brGreen; else if (ll.color == 2) br = &brGray;
        g.DrawString(ll.text.c_str(), -1, &monoFont, PointF(ix + PADDING, ty), br);
        ty -= lineH;
    }
    LeaveCriticalSection(&g_cs);
    g.SetClip(&innerRegion);

    float inputY = iy + ih - INPUT_H;
    RectF inputRect(ix, inputY, iw, (float)INPUT_H);
    g_hit.inputZone = inputRect;
    SolidBrush inputBg(Color(255, 20, 20, 20));
    g.FillRectangle(&inputBg, inputRect);
    Pen topLine(Color(255, 45, 45, 45), 1.0f);
    g.DrawLine(&topLine, ix, inputY, ix + iw, inputY);

    std::wstring prompt = L"jat> " + g_input;
    g.DrawString(prompt.c_str(), -1, &monoFont, PointF(ix + PADDING, inputY + 8), &brGreen);

    if (g_cursorVisible) {
        RectF sz;
        g.MeasureString(prompt.c_str(), -1, &monoFont, PointF(0, 0), &sz);
        SolidBrush cursorBrush(COL_WHITE);
        g.FillRectangle(&cursorBrush, ix + PADDING + sz.Width, inputY + 8, 8.0f, 15.0f);
    }

    g.ResetClip();

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmp = nullptr;
    bmp.GetHBITMAP(Color(0, 0, 0, 0), &hbmp);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hbmp);

    POINT ptSrc = { 0, 0 };
    SIZE sizeWnd = { WIN_W, WIN_H };
    POINT ptDst;
    RECT wr; GetWindowRect(g_hwnd, &wr);
    ptDst.x = wr.left; ptDst.y = wr.top;
    BLENDFUNCTION blend{}; blend.BlendOp = AC_SRC_OVER; blend.SourceConstantAlpha = 255; blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(g_hwnd, hdcScreen, &ptDst, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hOld);
    DeleteObject(hbmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

static void LoadLogoIfExists() {
    const wchar_t* candidates[] = { L"logo.png", L"logo.jpg", L"logo.jpeg" };
    for (auto c : candidates) {
        Image* img = new Image(c);
        if (img->GetLastStatus() == Ok) { g_logo = img; return; }
        delete img;
    }
    g_logo = nullptr;
}

// ----------------------------------------------------------------------------
// 윈도우 프로시저
// ----------------------------------------------------------------------------
static bool PtInRectF(const RectF& r, int x, int y) { return x >= r.X && x <= r.X + r.Width && y >= r.Y && y <= r.Y + r.Height; }

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        PurgeOldQuarantine();
        LoadLogoIfExists();
        Log(L"jat Terminal v2.0.0 - 준비 완료. help() 를 입력하세요.", 1);
        SetTimer(hwnd, 1, 500, nullptr);
        return 0;

    case WM_JAT_REFRESH:
        Render();
        return 0;

    case WM_TIMER:
        g_cursorVisible = !g_cursorVisible;
        Render();
        return 0;

    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam), y = HIWORD(lParam);
        if (PtInRectF(g_hit.close, x, y)) { DestroyWindow(hwnd); return 0; }
        if (PtInRectF(g_hit.minimize, x, y)) { ShowWindow(hwnd, SW_MINIMIZE); return 0; }
        if (PtInRectF(g_hit.dragZone, x, y)) { ReleaseCapture(); SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); return 0; }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_scrollOffset += (delta > 0) ? 3 : -3;
        if (g_scrollOffset < 0) g_scrollOffset = 0;
        int maxOff = (int)g_logLines.size();
        if (g_scrollOffset > maxOff) g_scrollOffset = maxOff;
        Render();
        return 0;
    }

    case WM_CHAR: {
        wchar_t ch = (wchar_t)wParam;
        if (ch == VK_RETURN || ch == VK_BACK || ch == 27) return 0;
        if (ch >= 0x20) { g_input += ch; Render(); }
        return 0;
    }

    case WM_KEYDOWN: {
        switch (wParam) {
        case VK_RETURN: {
            std::wstring line = g_input;
            if (!Trim(line).empty()) { g_history.push_back(line); ExecuteLine(line); }
            g_input.clear(); g_historyIndex = -1; g_scrollOffset = 0;
            Render();
            return 0;
        }
        case VK_BACK: if (!g_input.empty()) g_input.pop_back(); Render(); return 0;
        case VK_UP:
            if (!g_history.empty()) {
                if (g_historyIndex == -1) g_historyIndex = (int)g_history.size() - 1;
                else if (g_historyIndex > 0) g_historyIndex--;
                g_input = g_history[g_historyIndex];
                Render();
            }
            return 0;
        case VK_DOWN:
            if (g_historyIndex != -1) {
                g_historyIndex++;
                if (g_historyIndex >= (int)g_history.size()) { g_historyIndex = -1; g_input.clear(); }
                else g_input = g_history[g_historyIndex];
                Render();
            }
            return 0;
        }
        return 0;
    }

    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: KillTimer(hwnd, 1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ----------------------------------------------------------------------------
// 진입점
// ----------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    InitializeCriticalSection(&g_cs);

    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);

    GdiplusStartupInput gsi;
    GdiplusStartup(&g_gdiToken, &gsi, nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"jatTerminalClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    int x = (GetSystemMetrics(SM_CXSCREEN) - WIN_W) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - WIN_H) / 2;
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW, wc.lpszClassName, L"jat Terminal",
        WS_POPUP, x, y, WIN_W, WIN_H, nullptr, nullptr, hInstance, nullptr);

    ShowWindow(g_hwnd, nCmdShow);
    Render();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_logo) delete g_logo;
    GdiplusShutdown(g_gdiToken);
    WSACleanup();
    DeleteCriticalSection(&g_cs);
    return 0;
}
