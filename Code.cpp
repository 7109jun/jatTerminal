// ============================================================================
// jat Terminal v1.0.0
// 괄호형 명령어 UX 터미널 시스템 - Win32 Native / GDI+ 커스텀 렌더링
// 팔레트: 초록(테두리/글로우) / 검정(배경) / 회색(타이틀바) / 흰색(텍스트)
// ============================================================================
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <chrono>
#include <cwctype>
#include <cstdio>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace fs = std::filesystem;
using namespace Gdiplus;

// ----------------------------------------------------------------------------
// 상수 / 팔레트
// ----------------------------------------------------------------------------
static const int WIN_W = 1000;
static const int WIN_H = 650;
static const int MARGIN = 18;        // 글로우 여백
static const int BORDER_T = 7;       // 테두리 두께
static const int RADIUS = 26;        // 모서리 반경
static const int TITLE_H = 46;       // 타이틀바 높이
static const int INPUT_H = 34;       // 입력줄 높이
static const int PADDING = 14;       // 내부 좌우 여백

static Color COL_GREEN      (255, 120, 255, 175);
static Color COL_GREEN_DIM  (255,  70, 190, 130);
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

struct LogLine { std::wstring text; int color; }; // color: 0=white 1=green 2=gray 3=error(white)
static std::vector<LogLine> g_logLines;
static std::wstring g_input;
static std::vector<std::wstring> g_history;
static int g_historyIndex = -1;
static int g_scrollOffset = 0;
static bool g_cursorVisible = true;
static bool g_dragging = false;

// ----------------------------------------------------------------------------
// 문자열 유틸
// ----------------------------------------------------------------------------
static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::wstring ToLowerW(std::wstring s) {
    for (auto& c : s) c = towlower(c);
    return s;
}

static std::string WStringToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], sz, nullptr, nullptr);
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
    std::wstringstream ss(text);
    std::wstring line;
    bool any = false;
    while (std::getline(ss, line)) { g_logLines.push_back({ line, colorType }); any = true; }
    if (!any) g_logLines.push_back({ L"", colorType });
    if (g_logLines.size() > 3000) g_logLines.erase(g_logLines.begin(), g_logLines.begin() + 800);
    g_scrollOffset = 0;
}

// ----------------------------------------------------------------------------
// 명령어 파서 (괄호형 UX)
// ----------------------------------------------------------------------------
struct ParsedCommand {
    bool valid = false;
    bool dryRun = false;
    bool silent = false;
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
        if (c == delim && !inQuote && depthParen == 0 && depthBrack == 0) {
            res.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
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
        } else {
            pc.args.push_back(t);
        }
    }
    pc.name = name;
    pc.valid = true;
    return pc;
}

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
    long long now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (auto& entry : fs::directory_iterator(trash, ec)) {
        if (entry.path().extension() == L".meta") continue;
        fs::path meta = entry.path();
        meta += L".meta";
        if (!fs::exists(meta, ec)) continue;
        std::wifstream mf(meta);
        std::wstring origPath;
        long long ts = 0;
        std::getline(mf, origPath);
        mf >> ts;
        mf.close();
        if (now - ts > 30LL * 24 * 3600) {
            fs::remove_all(entry.path(), ec);
            fs::remove(meta, ec);
        }
    }
}

// ----------------------------------------------------------------------------
// 명령어 구현
// ----------------------------------------------------------------------------
static void CmdCreateUpdate(std::vector<std::wstring>& args, bool dryRun, bool isUpdate) {
    if (args.size() < 2) { Log(L"[ERROR] 사용법: " + std::wstring(isUpdate ? L"Update" : L"Create") + L"(\"파일명\",\"내용\",\"확장자\")", 3); return; }
    std::wstring full = args[0];
    if (args.size() >= 3 && !args[2].empty()) {
        std::wstring ext = args[2];
        if (ext[0] != L'.') ext = L"." + ext;
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
    if (args.size() < 2) { Log(L"[ERROR] 사용법: Rename(\"기존파일명\",\"새파일명\")", 3); return; }
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
    long long epoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
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
    if (total == 0) return 0.0;
    return (1.0 - (double)idle / (double)total) * 100.0;
}

static void CmdStatus(std::vector<std::wstring>&, bool dryRun) {
    if (dryRun) { Log(L"[DRY-RUN] 시스템 상태 조회 예정", 2); return; }
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    double cpu = GetCpuUsagePercent();
    ULONGLONG totalMB = ms.ullTotalPhys / (1024 * 1024);
    ULONGLONG availMB = ms.ullAvailPhys / (1024 * 1024);
    ULONGLONG usedMB = totalMB - availMB;
    ULONGLONG uptimeSec = GetTickCount64() / 1000;
    wchar_t buf[512];
    swprintf(buf, 512,
        L"CPU 사용률: %.1f%%\nRAM: %llu MB / %llu MB 사용중 (부하 %lu%%)\n가동시간: %llu시간 %llu분",
        cpu, usedMB, totalMB, ms.dwMemoryLoad, uptimeSec / 3600, (uptimeSec % 3600) / 60);
    Log(buf, 1);
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
            std::wstring exe = ToLowerW(pe.szExeFile);
            if (exe == target) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) { TerminateProcess(h, 0); CloseHandle(h); killedCount++; }
            }
        } while (Process32NextW(snap, &pe));
    }
    if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
    if (killedCount > 0) Log(L"완료: " + std::to_wstring(killedCount) + L"개 프로세스 종료됨 -> " + args[0], 1);
    else Log(L"[ERROR] 프로세스를 찾을 수 없습니다: " + args[0], 3);
}

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
    uintmax_t total = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2;
        if (!it->is_directory(e2)) total += it->file_size(e2);
    }
    return total;
}

static void CmdSize(std::vector<std::wstring>& args, bool dryRun) {
    fs::path p = args.empty() ? fs::current_path() : fs::path(args[0]);
    std::error_code ec;
    if (!fs::exists(p, ec)) { Log(L"[ERROR] 대상이 없습니다: " + p.wstring(), 3); return; }
    if (dryRun) { Log(L"[DRY-RUN] 용량 계산 예정: " + p.wstring(), 2); return; }
    uintmax_t sz = fs::is_directory(p, ec) ? SizeRecurse(p) : fs::file_size(p, ec);
    double mb = sz / 1048576.0;
    wchar_t buf[128];
    swprintf(buf, 128, L"용량: %.2f MB (%llu bytes)", mb, (unsigned long long)sz);
    Log(buf, 1);
}

static void CmdFind(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Find(\"검색어\",\"경로\")", 3); return; }
    std::wstring query = ToLowerW(args[0]);
    fs::path root = args.size() >= 2 ? fs::path(args[1]) : fs::current_path();
    if (dryRun) { Log(L"[DRY-RUN] 검색 예정: '" + args[0] + L"' in " + root.wstring(), 2); return; }
    std::wstring out; int count = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        std::wstring fname = ToLowerW(it->path().filename().wstring());
        if (fname.find(query) != std::wstring::npos) { out += it->path().wstring() + L"\n"; count++; }
    }
    if (count == 0) Log(L"검색 결과 없음: " + args[0], 2);
    else Log(L"검색 결과 " + std::to_wstring(count) + L"건:\n" + out, 1);
}

static void CmdClean(std::vector<std::wstring>& args, bool dryRun) {
    if (args.empty()) { Log(L"[ERROR] 사용법: Clean(\"확장자\",\"경로\")", 3); return; }
    std::wstring ext = args[0];
    if (!ext.empty() && ext[0] != L'.') ext = L"." + ext;
    ext = ToLowerW(ext);
    fs::path root = args.size() >= 2 ? fs::path(args[1]) : fs::current_path();
    std::vector<fs::path> targets;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2;
        if (!it->is_directory(e2) && ToLowerW(it->path().extension().wstring()) == ext) targets.push_back(it->path());
    }
    if (dryRun) {
        std::wstring out = L"[DRY-RUN] " + std::to_wstring(targets.size()) + L"개 파일 삭제 예정:\n";
        for (auto& t : targets) out += t.wstring() + L"\n";
        Log(out, 2);
        return;
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

static void CmdHelp() {
    Log(
        L"=== jat Terminal v1 명령어 목록 ===\n"
        L"[파일] Create Update Copy Move Rename Open\n"
        L"[삭제] Delete(Lv.1 휴지통) Remove(Lv.2 30일유예) Erase(Lv.3 영구)\n"
        L"[시스템] Status Run Kill\n"
        L"[정보] Info Tree Size Find Clean\n"
        L"[UX] help() version() clear() history()\n"
        L"[안전기호] ?명령어(...) = 미리보기(Dry-run) / $명령어(...) = 무음 실행\n"
        L"※ Download/Upload/Ping/Compress/Watch/Schedule/Macro 는 v2에서 추가됩니다.",
        1);
}

static void CmdVersion() { Log(L"jat Terminal v1.0.0 (Win32 Native / C++17 / GDI+)", 1); }
static void CmdClear() { g_logLines.clear(); }
static void CmdHistoryShow() {
    if (g_history.empty()) { Log(L"기록 없음.", 2); return; }
    std::wstring out;
    for (size_t i = 0; i < g_history.size(); ++i) out += std::to_wstring(i + 1) + L". " + g_history[i] + L"\n";
    Log(out, 1);
}

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
        else if (nl == L"delete") CmdDelete(args, pc.dryRun);
        else if (nl == L"remove") CmdRemove(args, pc.dryRun);
        else if (nl == L"erase") CmdErase(args, pc.dryRun);
        else if (nl == L"status") CmdStatus(args, pc.dryRun);
        else if (nl == L"run") CmdRun(args, pc.dryRun);
        else if (nl == L"kill") CmdKill(args, pc.dryRun);
        else if (nl == L"info") CmdInfo(args, pc.dryRun);
        else if (nl == L"tree") CmdTree(args, pc.dryRun);
        else if (nl == L"size") CmdSize(args, pc.dryRun);
        else if (nl == L"find") CmdFind(args, pc.dryRun);
        else if (nl == L"clean") CmdClean(args, pc.dryRun);
        else if (nl == L"help") CmdHelp();
        else if (nl == L"version") CmdVersion();
        else if (nl == L"clear") CmdClear();
        else if (nl == L"history") CmdHistoryShow();
        else Log(L"[ERROR] 알 수 없는 명령어이거나 v2 예정 기능입니다: " + pc.name, 3);
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

    // 글로우 (바깥쪽으로 알파 감소하는 겹쳐진 라운드 사각형 테두리)
    GraphicsPath glowPath;
    for (int i = 10; i >= 1; --i) {
        int alpha = 10 * i;
        Pen pen(Color(alpha, 110, 255, 170), (float)i * 1.6f);
        AddRoundedRect(glowPath, m - i, m - i, (float)WIN_W - 2 * (m - i), (float)WIN_H - 2 * (m - i), r + i);
        g.DrawPath(&pen, &glowPath);
    }

    // 바깥 테두리 (초록, 실선)
    GraphicsPath outerPath;
    AddRoundedRect(outerPath, m, m, (float)WIN_W - 2 * m, (float)WIN_H - 2 * m, r);
    Pen borderPen(COL_GREEN, bt);
    g.DrawPath(&borderPen, &outerPath);

    // 안쪽 배경 (검정)
    GraphicsPath innerPath;
    float ix = m + bt, iy = m + bt, iw = (float)WIN_W - 2 * (m + bt), ih = (float)WIN_H - 2 * (m + bt);
    AddRoundedRect(innerPath, ix, iy, iw, ih, r - bt > 0 ? r - bt : 2);
    Region innerRegion(&innerPath);
    g.SetClip(&innerRegion);
    SolidBrush blackBrush(COL_BLACK_BG);
    g.FillPath(&blackBrush, &innerPath);

    // 타이틀바 (회색, innerPath로 클립되어 위쪽만 자연스럽게 둥글게 보임)
    RectF titleRect(ix, iy, iw, (float)TITLE_H);
    SolidBrush titleBrush(COL_GRAY_TITLE);
    g.FillRectangle(&titleBrush, titleRect);

    // 신호등 스타일 원 3개 (좌측)
    float cy = iy + TITLE_H / 2.0f;
    float cx0 = ix + 22, gap = 26, crad = 7;
    SolidBrush c1(COL_GRAY_C1), c2(COL_GRAY_C2), c3(COL_GRAY_C3);
    g.FillEllipse(&c1, cx0 - crad, cy - crad, crad * 2, crad * 2);
    g.FillEllipse(&c2, cx0 + gap - crad, cy - crad, crad * 2, crad * 2);
    g.FillEllipse(&c3, cx0 + gap * 2 - crad, cy - crad, crad * 2, crad * 2);

    // 로고 (있으면)
    float titleTextX = cx0 + gap * 3;
    if (g_logo) {
        float logoH = TITLE_H - 14.0f, logoW = logoH;
        g.DrawImage(g_logo, ix + gap * 3 + 6, iy + 7, logoW, logoH);
        titleTextX = ix + gap * 3 + 6 + logoW + 8;
    }

    // 타이틀 텍스트
    FontFamily ffTitle(L"Consolas");
    Font titleFont(&ffTitle, 15, FontStyleBold, UnitPixel);
    SolidBrush whiteBrush(COL_WHITE);
    g.DrawString(L"jat Terminal", -1, &titleFont, PointF(titleTextX, iy + 13), &whiteBrush);

    // 닫기 / 최소화 버튼 (우측)
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

    // 터미널 로그 영역
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

    int total = (int)g_logLines.size();
    int start = total - maxLines - g_scrollOffset;
    if (start < 0) start = 0;
    int end = total - g_scrollOffset;
    if (end > total) end = total;
    if (end < 0) end = 0;

    float ty = logY + logH - lineH; // 아래에서부터 위로 그림
    for (int i = end - 1; i >= start; --i) {
        if (ty < logY - lineH) break;
        auto& ll = g_logLines[i];
        Brush* br = &brWhite;
        if (ll.color == 1) br = &brGreen;
        else if (ll.color == 2) br = &brGray;
        g.DrawString(ll.text.c_str(), -1, &monoFont, PointF(ix + PADDING, ty), br);
        ty -= lineH;
    }
    g.SetClip(&innerRegion);

    // 입력줄
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

    // 화면에 반영 (레이어드 윈도우)
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
static bool PtInRectF(const RectF& r, int x, int y) {
    return x >= r.X && x <= r.X + r.Width && y >= r.Y && y <= r.Y + r.Height;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        PurgeOldQuarantine();
        LoadLogoIfExists();
        Log(L"jat Terminal v1.0.0 - 준비 완료. help() 를 입력하세요.", 1);
        SetTimer(hwnd, 1, 500, nullptr);
        return 0;

    case WM_TIMER:
        g_cursorVisible = !g_cursorVisible;
        Render();
        return 0;

    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam), y = HIWORD(lParam);
        if (PtInRectF(g_hit.close, x, y)) { DestroyWindow(hwnd); return 0; }
        if (PtInRectF(g_hit.minimize, x, y)) { ShowWindow(hwnd, SW_MINIMIZE); return 0; }
        if (PtInRectF(g_hit.dragZone, x, y)) {
            ReleaseCapture();
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
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
        if (ch == VK_RETURN || ch == VK_BACK || ch == 27) return 0; // 별도 처리
        if (ch >= 0x20) { g_input += ch; Render(); }
        return 0;
    }

    case WM_KEYDOWN: {
        switch (wParam) {
        case VK_RETURN: {
            std::wstring line = g_input;
            if (!Trim(line).empty()) {
                g_history.push_back(line);
                ExecuteLine(line);
            }
            g_input.clear();
            g_historyIndex = -1;
            g_scrollOffset = 0;
            Render();
            return 0;
        }
        case VK_BACK:
            if (!g_input.empty()) g_input.pop_back();
            Render();
            return 0;
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

    case WM_ERASEBKGND:
        return 1;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ----------------------------------------------------------------------------
// 진입점
// ----------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
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
    return 0;
}
