// ============================================================================
// jat Terminal v3.0.0
// 괄호형 명령어 UX 터미널 시스템 - 진짜 CLI (Windows + Linux 지원)
// 팔레트: 초록(강조/프롬프트) / 검정(배경, 터미널 기본) / 회색(보조텍스트) / 흰색(본문)
// 의존성: curl (Download/Upload/API/Ping 보조), python3(Linux)/python(Windows) (^코드^ 실행)
// ============================================================================
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <winsvc.h>
    #include <iphlpapi.h>
    #include <shellapi.h>
    #include <shlobj.h>
    #include <tlhelp32.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "advapi32.lib")
    #pragma comment(lib, "iphlpapi.lib")
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "shell32.lib")
#else
    #include <unistd.h>
    #include <dirent.h>
    #include <signal.h>
    #include <sys/wait.h>
    #include <ifaddrs.h>
    #include <netdb.h>
    #include <arpa/inet.h>
    #include <cstring>
    #include <X11/Xlib.h>
    #include <X11/extensions/XTest.h>
#endif

#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <random>
#include <iomanip>
#include <functional>
#include <memory>
#include <stdexcept>
#include <iterator>

namespace fs = std::filesystem;

// ----------------------------------------------------------------------------
// ANSI 색상 팔레트 (초록/검정/회색/흰색)
// ----------------------------------------------------------------------------
static const std::string C_RESET = "\033[0m";
static const std::string C_GREEN = "\033[1;32m";
static const std::string C_GRAY  = "\033[90m";
static const std::string C_WHITE = "\033[97m";

static std::vector<std::string> g_history;
static std::map<std::string, std::vector<std::string>> g_macros;   // Macro / Alias
static std::map<std::string, std::string> g_explain;                // Explain

// ----------------------------------------------------------------------------
// 문자열 유틸
// ----------------------------------------------------------------------------
static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::string ToLower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

static void Log(const std::string& text, int colorType) {
    // colorType: 0=white 1=green 2=gray 3=error(white, [ERROR] 접두어는 호출부에서 붙임)
    const std::string& col = (colorType == 1) ? C_GREEN : (colorType == 2) ? C_GRAY : C_WHITE;
    std::istringstream iss(text);
    std::string line;
    bool any = false;
    while (std::getline(iss, line)) { std::cout << col << line << C_RESET << "\n"; any = true; }
    if (!any) std::cout << "\n";
}

static void ShowPercent(const std::string& label, int pct) {
    std::cout << "\r" << C_GREEN << label << " " << pct << "%" << C_RESET << "     " << std::flush;
}
static void EndProgress(const std::string& finalMsg) {
    std::cout << "\r" << C_GREEN << finalMsg << C_RESET << "                              " << std::endl;
}

static std::string RunShellCapture(const std::string& cmd) {
    std::string result;
    char buf[512];
#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) return "";
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return result;
}

// ----------------------------------------------------------------------------
// 순수 C++ SHA-256 (외부 라이브러리 없이 크로스플랫폼 지원)
// ----------------------------------------------------------------------------
static const uint32_t SHA256_K[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define SHA_ROTR(x,n) (((x)>>(n))|((x)<<(32-(n))))

static void Sha256Transform(uint32_t st[8], const uint8_t data[64]) {
    uint32_t m[64];
    for (int i = 0, j = 0; i < 16; i++, j += 4)
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) | ((uint32_t)data[j + 2] << 8) | data[j + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = SHA_ROTR(m[i-15],7) ^ SHA_ROTR(m[i-15],18) ^ (m[i-15]>>3);
        uint32_t s1 = SHA_ROTR(m[i-2],17) ^ SHA_ROTR(m[i-2],19) ^ (m[i-2]>>10);
        m[i] = m[i-16] + s0 + m[i-7] + s1;
    }
    uint32_t a=st[0],b=st[1],c=st[2],d=st[3],e=st[4],f=st[5],g=st[6],h=st[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = SHA_ROTR(e,6) ^ SHA_ROTR(e,11) ^ SHA_ROTR(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + SHA256_K[i] + m[i];
        uint32_t S0 = SHA_ROTR(a,2) ^ SHA_ROTR(a,13) ^ SHA_ROTR(a,22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    st[0]+=a; st[1]+=b; st[2]+=c; st[3]+=d; st[4]+=e; st[5]+=f; st[6]+=g; st[7]+=h;
}

static std::vector<uint8_t> Sha256Bytes(const uint8_t* data, size_t len) {
    uint32_t st[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint64_t bitlen = (uint64_t)len * 8;
    size_t fullBlocks = len / 64;
    for (size_t i = 0; i < fullBlocks; i++) Sha256Transform(st, data + i * 64);
    size_t rem = len - fullBlocks * 64;
    uint8_t block[128] = {0};
    memcpy(block, data + fullBlocks * 64, rem);
    block[rem] = 0x80;
    size_t padLen = (rem < 56) ? 64 : 128;
    for (int i = 0; i < 8; i++) block[padLen - 1 - i] = (uint8_t)(bitlen >> (8 * i));
    Sha256Transform(st, block);
    if (padLen == 128) Sha256Transform(st, block + 64);
    std::vector<uint8_t> out(32);
    for (int i = 0; i < 8; i++) {
        out[i*4] = (st[i]>>24)&0xFF; out[i*4+1] = (st[i]>>16)&0xFF;
        out[i*4+2] = (st[i]>>8)&0xFF; out[i*4+3] = st[i]&0xFF;
    }
    return out;
}
static std::string Sha256Hex(const std::vector<uint8_t>& data) {
    auto h = Sha256Bytes(data.data(), data.size());
    std::ostringstream ss;
    for (auto b : h) ss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return ss.str();
}
static std::string Sha256File(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return Sha256Hex(data);
}

// 간이 스트림 암호 (SHA-256 해시체인 기반 XOR, AES 아님 - 캐주얼 보호용)
static std::vector<uint8_t> HashStreamCipher(const std::vector<uint8_t>& data, const std::string& password, const uint8_t salt[16]) {
    std::vector<uint8_t> out(data.size());
    std::vector<uint8_t> seed(password.begin(), password.end());
    seed.insert(seed.end(), salt, salt + 16);
    uint64_t counter = 0; size_t pos = 0;
    while (pos < data.size()) {
        std::vector<uint8_t> input = seed;
        for (int i = 0; i < 8; i++) input.push_back((uint8_t)(counter >> (8 * i)));
        auto h = Sha256Bytes(input.data(), input.size());
        size_t chunk = (std::min)((size_t)32, data.size() - pos);
        for (size_t i = 0; i < chunk; i++) out[pos + i] = data[pos + i] ^ h[i];
        pos += chunk; counter++;
    }
    return out;
}

static bool IsProbablyText(const std::vector<char>& data) {
    size_t n = (std::min)(data.size(), (size_t)4096);
    for (size_t i = 0; i < n; i++) if (data[i] == 0) return false;
    return true;
}

// ----------------------------------------------------------------------------
// 명령어 파서 (괄호형 UX) - v1/v2와 동일한 문법
// ----------------------------------------------------------------------------
struct ParsedCommand {
    bool valid = false, dryRun = false, silent = false;
    std::string name;
    std::vector<std::string> args;
    std::string error;
    std::string raw; // 원본 라인 (jat코드 폴백에 사용)
};

static std::vector<std::string> SplitTopLevel(const std::string& s, char delim) {
    std::vector<std::string> res;
    int depthParen = 0, depthBrack = 0;
    bool inQuote = false;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"' && (i == 0 || s[i - 1] != '\\')) inQuote = !inQuote;
        if (!inQuote) {
            if (c == '(') depthParen++;
            else if (c == ')') depthParen--;
            else if (c == '[') depthBrack++;
            else if (c == ']') depthBrack--;
        }
        if (c == delim && !inQuote && depthParen == 0 && depthBrack == 0) { res.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!Trim(cur).empty() || !res.empty()) res.push_back(cur);
    return res;
}

static ParsedCommand ParseCommand(std::string s) {
    ParsedCommand pc;
    s = Trim(s);
    while (!s.empty() && (s[0] == '?' || s[0] == '$')) {
        if (s[0] == '?') pc.dryRun = true; else pc.silent = true;
        s = Trim(s.substr(1));
    }
    size_t p = s.find('(');
    if (p == std::string::npos || s.empty() || s.back() != ')') { pc.error = "괄호 형식이 아닙니다. 예) Create(\"이름\",\"내용\",\"txt\")"; return pc; }
    std::string name = Trim(s.substr(0, p));
    if (name.empty()) { pc.error = "명령어 이름이 없습니다."; return pc; }
    std::string inner = s.substr(p + 1, s.size() - p - 2);
    for (auto& a : SplitTopLevel(inner, ',')) {
        std::string t = Trim(a);
        if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
            std::string content = t.substr(1, t.size() - 2), out;
            for (size_t i = 0; i < content.size(); ++i) {
                if (content[i] == '\\' && i + 1 < content.size()) {
                    char nx = content[i + 1];
                    if (nx == 'n') { out += '\n'; i++; continue; }
                    if (nx == '"') { out += '"'; i++; continue; }
                    if (nx == '\\') { out += '\\'; i++; continue; }
                }
                out += content[i];
            }
            pc.args.push_back(out);
        } else pc.args.push_back(t);
    }
    pc.name = name; pc.valid = true;
    return pc;
}
static std::vector<std::string> ParseBracketList(const std::string& raw) {
    std::string t = Trim(raw);
    if (t.size() >= 2 && t.front() == '[' && t.back() == ']') t = t.substr(1, t.size() - 2);
    return SplitTopLevel(t, ',');
}
static void ExecuteLine(const std::string& raw); // 전방 선언

// ----------------------------------------------------------------------------
// 격리 휴지통 (Remove - Lv.2, 30일 유예) - 완전 이식성
// ----------------------------------------------------------------------------
static fs::path TrashDir() {
    fs::path d = fs::current_path() / ".jat_trash";
    std::error_code ec; fs::create_directories(d, ec);
    return d;
}
static void PurgeOldQuarantine() {
    fs::path trash = fs::current_path() / ".jat_trash";
    std::error_code ec;
    if (!fs::exists(trash, ec)) return;
    long long now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (auto& entry : fs::directory_iterator(trash, ec)) {
        if (entry.path().extension() == ".meta") continue;
        fs::path meta = entry.path(); meta += ".meta";
        if (!fs::exists(meta, ec)) continue;
        std::ifstream mf(meta);
        std::string origPath; long long ts = 0;
        std::getline(mf, origPath); mf >> ts; mf.close();
        if (now - ts > 30LL * 24 * 3600) { fs::remove_all(entry.path(), ec); fs::remove(meta, ec); }
    }
}

// ============================================================================
// [파일 제어] Create/Update/Copy/Move/Rename/Open/Duplicate
// ============================================================================
static void CmdCreateUpdate(std::vector<std::string>& args, bool dryRun, bool isUpdate) {
    if (args.size() < 2) { Log("[ERROR] 사용법: " + std::string(isUpdate ? "Update" : "Create") + "(\"파일명\",\"내용\",\"확장자\")", 3); return; }
    std::string full = args[0];
    if (args.size() >= 3 && !args[2].empty()) { std::string ext = args[2]; if (ext[0] != '.') ext = "." + ext; full = args[0] + ext; }
    fs::path p = fs::u8path(full);
    if (isUpdate && !fs::exists(p)) { Log("[ERROR] 파일이 존재하지 않습니다: " + full, 3); return; }
    if (dryRun) { Log("[DRY-RUN] 파일 " + std::string(isUpdate ? "갱신" : "생성") + " 예정: " + full, 2); return; }
    std::ofstream ofs(p, std::ios::binary);
    if (!ofs) { Log("[ERROR] 파일을 열 수 없습니다: " + full, 3); return; }
    ofs.write(args[1].data(), (std::streamsize)args[1].size());
    ofs.close();
    Log("완료: " + full + " (" + std::to_string(args[1].size()) + " bytes)", 1);
}

static void CmdCopy(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Copy(\"출발지\",\"목적지\")", 3); return; }
    fs::path src = fs::u8path(args[0]), dst = fs::u8path(args[1]);
    std::error_code ec;
    if (!fs::exists(src, ec)) { Log("[ERROR] 원본이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 복사 예정: " + args[0] + " -> " + args[1], 2); return; }
    if (fs::is_directory(src, ec)) {
        fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) { Log("[ERROR] 복사 실패: " + ec.message(), 3); return; }
        Log("완료: 복사됨 -> " + args[1], 1);
        return;
    }
    std::ifstream in(src, std::ios::binary);
    std::ofstream out(dst, std::ios::binary);
    if (!in || !out) { Log("[ERROR] 파일을 열 수 없습니다.", 3); return; }
    uintmax_t total = fs::file_size(src, ec), done = 0;
    std::vector<char> buf(1 << 16);
    while (in) {
        in.read(buf.data(), (std::streamsize)buf.size());
        std::streamsize n = in.gcount();
        if (n > 0) { out.write(buf.data(), n); done += n; }
        ShowPercent("복사 중", total ? (int)(done * 100 / total) : 100);
    }
    EndProgress("완료: 복사됨 -> " + args[1]);
}

static void CmdMove(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Move(\"출발지\",\"목적지\")", 3); return; }
    fs::path src = fs::u8path(args[0]), dst = fs::u8path(args[1]);
    std::error_code ec;
    if (!fs::exists(src, ec)) { Log("[ERROR] 원본이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 이동 예정: " + args[0] + " -> " + args[1], 2); return; }
    fs::rename(src, dst, ec);
    if (ec) {
        ec.clear();
        if (fs::is_directory(src)) fs::copy(src, dst, fs::copy_options::recursive, ec);
        else fs::copy_file(src, dst, ec);
        if (!ec) fs::remove_all(src, ec);
    }
    if (ec) { Log("[ERROR] 이동 실패: " + ec.message(), 3); return; }
    Log("완료: 이동됨 -> " + args[1], 1);
}

static void CmdRename(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Rename(\"기존\",\"새이름\")", 3); return; }
    fs::path src = fs::u8path(args[0]);
    std::error_code ec;
    if (!fs::exists(src, ec)) { Log("[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    fs::path dst = src.parent_path() / fs::u8path(args[1]);
    if (dryRun) { Log("[DRY-RUN] 이름 변경 예정: " + args[0] + " -> " + args[1], 2); return; }
    fs::rename(src, dst, ec);
    if (ec) { Log("[ERROR] 이름 변경 실패: " + ec.message(), 3); return; }
    Log("완료: 이름 변경됨 -> " + args[1], 1);
}

static void CmdOpen(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Open(\"파일/경로\")", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 열기 예정: " + args[0], 2); return; }
#ifdef _WIN32
    HINSTANCE r = ShellExecuteA(nullptr, "open", args[0].c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) { Log("[ERROR] 열기 실패: " + args[0], 3); return; }
#else
    int rc = system(("xdg-open '" + args[0] + "' >/dev/null 2>&1 &").c_str());
    (void)rc;
#endif
    Log("완료: 열림 -> " + args[0], 1);
}
static void CmdDuplicate(std::vector<std::string>& args, bool dryRun) { CmdCopy(args, dryRun); }

// ============================================================================
// [3단계 삭제] Delete/Remove/Erase
// ============================================================================
static void CmdDelete(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Delete(\"파일명\")", 3); return; }
    fs::path p = fs::u8path(args[0]);
    std::error_code ec;
    if (!fs::exists(p, ec)) { Log("[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 휴지통 이동 예정 (Lv.1): " + args[0], 2); return; }
#ifdef _WIN32
    std::string full = fs::absolute(p).string();
    full.push_back('\0'); full.push_back('\0');
    SHFILEOPSTRUCTA op{}; op.wFunc = FO_DELETE; op.pFrom = full.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    if (SHFileOperationA(&op) != 0) { Log("[ERROR] 삭제 실패", 3); return; }
#else
    int rc = system(("gio trash '" + fs::absolute(p).string() + "' >/dev/null 2>&1").c_str());
    if (rc != 0) {
        fs::path trash = TrashDir();
        fs::rename(p, trash / p.filename(), ec);
        Log("[알림] 시스템 휴지통(gio) 사용 불가 - 자체 격리 폴더로 이동했습니다.", 2);
    }
#endif
    Log("완료: 휴지통으로 이동됨 (Lv.1) -> " + args[0], 1);
}

static void CmdRemove(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Remove(\"파일명\")", 3); return; }
    fs::path p = fs::u8path(args[0]);
    std::error_code ec;
    if (!fs::exists(p, ec)) { Log("[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 30일 유예 격리 예정 (Lv.2): " + args[0], 2); return; }
    fs::path trash = TrashDir();
    long long epoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::string uniqueName = std::to_string(epoch) + "_" + p.filename().string();
    fs::path dest = trash / uniqueName;
    fs::path absOrig = fs::absolute(p);
    fs::rename(p, dest, ec);
    if (ec) {
        ec.clear();
        if (fs::is_directory(p)) fs::copy(p, dest, fs::copy_options::recursive, ec);
        else fs::copy_file(p, dest, ec);
        if (!ec) fs::remove_all(p, ec);
    }
    if (ec) { Log("[ERROR] 격리 실패: " + ec.message(), 3); return; }
    std::ofstream meta(trash / (uniqueName + ".meta"));
    meta << absOrig.string() << "\n" << epoch;
    meta.close();
    Log("완료: 30일 유예 격리됨 (Lv.2) -> .jat_trash/" + uniqueName, 1);
}

static void CmdErase(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Erase(\"파일명\")", 3); return; }
    fs::path p = fs::u8path(args[0]);
    std::error_code ec;
    if (!fs::exists(p, ec)) { Log("[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 영구 파괴 예정 (Lv.3, 복구불가): " + args[0], 2); return; }
    uintmax_t n = fs::remove_all(p, ec);
    if (ec) { Log("[ERROR] 파괴 실패: " + ec.message(), 3); return; }
    Log("완료: 영구 파괴됨 (Lv.3) -> " + args[0] + " (" + std::to_string(n) + "개 항목)", 1);
}

// ============================================================================
// [정보 탐색] Info/Tree/Size/Find/Search/Replace/Analyze/DuplicateFinder/EmptyFinder
// ============================================================================
static void CmdInfo(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Info(\"파일명\")", 3); return; }
    fs::path p = fs::u8path(args[0]);
    std::error_code ec;
    if (!fs::exists(p, ec)) { Log("[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 정보 조회 예정: " + args[0], 2); return; }
    bool isDir = fs::is_directory(p, ec);
    std::string msg = "종류: " + std::string(isDir ? "폴더" : "파일");
    if (!isDir) msg += "\n크기: " + std::to_string(fs::file_size(p, ec)) + " bytes";
    auto ftime = fs::last_write_time(p, ec);
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    char tbuf[64]; std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
    msg += "\n수정일: " + std::string(tbuf);
    Log(msg, 1);
}

static void TreeRecurse(const fs::path& p, int depth, std::string& out) {
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(p, fs::directory_options::skip_permission_denied, ec)) {
        out += std::string((size_t)depth * 2, ' ') + (entry.is_directory() ? "[D] " : "    ") + entry.path().filename().string() + "\n";
        if (entry.is_directory()) TreeRecurse(entry.path(), depth + 1, out);
    }
}
static void CmdTree(std::vector<std::string>& args, bool dryRun) {
    fs::path p = args.empty() ? fs::current_path() : fs::u8path(args[0]);
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_directory(p, ec)) { Log("[ERROR] 폴더가 아닙니다: " + p.string(), 3); return; }
    if (dryRun) { Log("[DRY-RUN] 트리 조회 예정: " + p.string(), 2); return; }
    std::string out = p.string() + "\n";
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
static void CmdSize(std::vector<std::string>& args, bool dryRun) {
    fs::path p = args.empty() ? fs::current_path() : fs::u8path(args[0]);
    std::error_code ec;
    if (!fs::exists(p, ec)) { Log("[ERROR] 대상이 없습니다: " + p.string(), 3); return; }
    if (dryRun) { Log("[DRY-RUN] 용량 계산 예정: " + p.string(), 2); return; }
    uintmax_t sz = fs::is_directory(p, ec) ? SizeRecurse(p) : fs::file_size(p, ec);
    std::ostringstream ss; ss << "용량: " << std::fixed << std::setprecision(2) << (sz / 1048576.0) << " MB (" << sz << " bytes)";
    Log(ss.str(), 1);
}

static void CmdFind(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Find(\"검색어\",\"경로\")", 3); return; }
    std::string query = ToLower(args[0]);
    fs::path root = args.size() >= 2 ? fs::u8path(args[1]) : fs::current_path();
    if (dryRun) { Log("[DRY-RUN] 검색 예정: '" + args[0] + "' in " + root.string(), 2); return; }
    std::string out; int count = 0; std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        if (ToLower(it->path().filename().string()).find(query) != std::string::npos) { out += it->path().string() + "\n"; count++; }
    }
    Log(count == 0 ? "검색 결과 없음: " + args[0] : ("검색 결과 " + std::to_string(count) + "건:\n" + out), count == 0 ? 2 : 1);
}

static void CmdSearch(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Search(\"검색어\",\"경로\")", 3); return; }
    fs::path root = args.size() >= 2 ? fs::u8path(args[1]) : fs::current_path();
    if (dryRun) { Log("[DRY-RUN] 내용 검색 예정: '" + args[0] + "' in " + root.string(), 2); return; }
    std::string out; int hits = 0; std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2;
        if (it->is_directory(e2) || it->file_size(e2) > 5 * 1024 * 1024) continue;
        std::ifstream f(it->path(), std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (!IsProbablyText(data)) continue;
        std::string content(data.begin(), data.end());
        if (content.find(args[0]) != std::string::npos) { out += it->path().string() + "\n"; hits++; }
    }
    Log(hits == 0 ? "검색 결과 없음." : ("내용 검색 결과 " + std::to_string(hits) + "개 파일:\n" + out), 1);
}

static void CmdReplace(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Replace(\"찾을내용\",\"바꿀내용\",\"경로\")", 3); return; }
    fs::path root = args.size() >= 3 ? fs::u8path(args[2]) : fs::current_path();
    std::vector<fs::path> targets; std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2;
        if (it->is_directory(e2) || it->file_size(e2) > 5 * 1024 * 1024) continue;
        std::ifstream f(it->path(), std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (!IsProbablyText(data)) continue;
        std::string content(data.begin(), data.end());
        if (content.find(args[0]) != std::string::npos) targets.push_back(it->path());
    }
    if (dryRun) {
        std::string out = "[DRY-RUN] " + std::to_string(targets.size()) + "개 파일에서 치환 예정:\n";
        for (auto& t : targets) out += t.string() + "\n";
        Log(out, 2); return;
    }
    int count = 0;
    for (auto& t : targets) {
        std::ifstream f(t, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
        size_t pos = 0;
        while ((pos = content.find(args[0], pos)) != std::string::npos) { content.replace(pos, args[0].size(), args[1]); pos += args[1].size(); }
        std::ofstream ofs(t, std::ios::binary);
        ofs.write(content.data(), (std::streamsize)content.size());
        count++;
    }
    Log("완료: " + std::to_string(count) + "개 파일 치환됨", 1);
}

static void CmdAnalyze(std::vector<std::string>& args, bool dryRun) {
    fs::path root = args.empty() ? fs::current_path() : fs::u8path(args[0]);
    std::error_code ec;
    if (!fs::exists(root, ec)) { Log("[ERROR] 대상이 없습니다", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 분석 예정: " + root.string(), 2); return; }
    std::map<std::string, std::pair<int, uintmax_t>> stats;
    int totalFiles = 0; uintmax_t totalSize = 0;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2; if (it->is_directory(e2)) continue;
        std::string ext = ToLower(it->path().extension().string()); if (ext.empty()) ext = "(없음)";
        uintmax_t sz = it->file_size(e2);
        stats[ext].first++; stats[ext].second += sz; totalFiles++; totalSize += sz;
    }
    std::string out = "=== 분석 결과: " + root.string() + " ===\n총 파일 수: " + std::to_string(totalFiles) + ", 총 용량: " + std::to_string(totalSize / 1048576) + " MB\n";
    for (auto& kv : stats) out += kv.first + " : " + std::to_string(kv.second.first) + "개, " + std::to_string(kv.second.second / 1024) + " KB\n";
    Log(out, 1);
}

static void CmdDuplicateFinder(std::vector<std::string>& args, bool dryRun) {
    fs::path root = args.empty() ? fs::current_path() : fs::u8path(args[0]);
    std::error_code ec;
    if (!fs::exists(root, ec)) { Log("[ERROR] 대상이 없습니다", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 중복 파일 검색 예정: " + root.string(), 2); return; }
    std::map<std::string, std::vector<std::string>> byHash;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2; if (it->is_directory(e2) || it->file_size(e2) == 0) continue;
        byHash[Sha256File(it->path())].push_back(it->path().string());
    }
    std::string out; int groups = 0;
    for (auto& kv : byHash) if (kv.second.size() > 1) { groups++; out += "[중복그룹 " + std::to_string(groups) + "]\n"; for (auto& f : kv.second) out += "  " + f + "\n"; }
    Log(groups == 0 ? "중복 파일 없음." : out, 1);
}

static void CmdEmptyFinder(std::vector<std::string>& args, bool dryRun) {
    fs::path root = args.empty() ? fs::current_path() : fs::u8path(args[0]);
    std::error_code ec;
    if (!fs::exists(root, ec)) { Log("[ERROR] 대상이 없습니다", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 빈 폴더 검색 예정: " + root.string(), 2); return; }
    std::string out; int count = 0;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2; if (it->is_directory(e2) && fs::is_empty(it->path(), e2)) { out += it->path().string() + "\n"; count++; }
    }
    Log(count == 0 ? "빈 폴더 없음." : ("빈 폴더 " + std::to_string(count) + "개:\n" + out), 1);
}

// ============================================================================
// [자동화] Clean/Compress/Extract/Watch/Schedule/Macro/Batch/Alias
// ============================================================================
static void CmdClean(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Clean(\"확장자\",\"경로\")", 3); return; }
    std::string ext = args[0]; if (!ext.empty() && ext[0] != '.') ext = "." + ext; ext = ToLower(ext);
    fs::path root = args.size() >= 2 ? fs::u8path(args[1]) : fs::current_path();
    std::vector<fs::path> targets; std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2; if (!it->is_directory(e2) && ToLower(it->path().extension().string()) == ext) targets.push_back(it->path());
    }
    if (dryRun) {
        std::string out = "[DRY-RUN] " + std::to_string(targets.size()) + "개 파일 삭제 예정:\n";
        for (auto& t : targets) out += t.string() + "\n";
        Log(out, 2); return;
    }
    std::vector<std::string> asArgs;
    int count = 0;
    for (auto& t : targets) { asArgs = { t.string() }; bool d=false; CmdDelete(asArgs, d); count++; }
    Log("완료: " + std::to_string(count) + "개 파일 정리(휴지통 이동) -> *" + ext, 1);
}

static unsigned long g_crcTable[256]; static bool g_crcInit = false;
static void InitCrc32Table() { for (unsigned long i=0;i<256;i++){ unsigned long c=i; for(int k=0;k<8;k++) c=(c&1)?(0xEDB88320UL^(c>>1)):(c>>1); g_crcTable[i]=c; } g_crcInit=true; }
static unsigned long Crc32(const unsigned char* data, size_t len) {
    if (!g_crcInit) InitCrc32Table();
    unsigned long c = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) c = g_crcTable[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFUL;
}

static void CmdCompress(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Compress(\"폴더\",\"파일.zip\")", 3); return; }
    fs::path src = fs::u8path(args[0]);
    std::error_code ec;
    if (!fs::exists(src, ec)) { Log("[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 압축 예정: " + args[0] + " -> " + args[1], 2); return; }
    std::vector<fs::path> files;
    bool isDir = fs::is_directory(src, ec);
    if (isDir) for (auto it = fs::recursive_directory_iterator(src, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) { std::error_code e2; if (!it->is_directory(e2)) files.push_back(it->path()); }
    else files.push_back(src);

    std::ofstream ofs(args[1], std::ios::binary);
    struct CentralEntry { std::string name; uint32_t crc, size, offset; };
    std::vector<CentralEntry> centrals;
    size_t idx = 0;
    for (auto& f : files) {
        std::string rel = isDir ? fs::relative(f, src).string() : f.filename().string();
        for (auto& c : rel) if (c == '\\') c = '/';
        std::ifstream inf(f, std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(inf)), std::istreambuf_iterator<char>());
        uint32_t crc = Crc32((const unsigned char*)data.data(), data.size());
        uint32_t offset = (uint32_t)ofs.tellp();
        uint32_t sig = 0x04034b50; uint16_t v=20, fl=0, meth=0, mt=0, md=0, nameLen=(uint16_t)rel.size(), extraLen=0;
        uint32_t sz = (uint32_t)data.size();
        ofs.write((char*)&sig,4); ofs.write((char*)&v,2); ofs.write((char*)&fl,2); ofs.write((char*)&meth,2);
        ofs.write((char*)&mt,2); ofs.write((char*)&md,2); ofs.write((char*)&crc,4);
        ofs.write((char*)&sz,4); ofs.write((char*)&sz,4);
        ofs.write((char*)&nameLen,2); ofs.write((char*)&extraLen,2);
        ofs.write(rel.data(), (std::streamsize)rel.size());
        ofs.write(data.data(), (std::streamsize)data.size());
        centrals.push_back({ rel, crc, sz, offset });
        idx++;
        ShowPercent("압축 중", (int)(idx * 100 / (files.empty() ? 1 : files.size())));
    }
    uint32_t centralStart = (uint32_t)ofs.tellp();
    for (auto& c : centrals) {
        uint32_t sig=0x02014b50; uint16_t vm=20, vn=20, fl=0, meth=0, mt=0, md=0, nameLen=(uint16_t)c.name.size(), extraLen=0, commentLen=0, diskStart=0, intAttr=0; uint32_t extAttr=0;
        ofs.write((char*)&sig,4); ofs.write((char*)&vm,2); ofs.write((char*)&vn,2); ofs.write((char*)&fl,2); ofs.write((char*)&meth,2);
        ofs.write((char*)&mt,2); ofs.write((char*)&md,2); ofs.write((char*)&c.crc,4);
        ofs.write((char*)&c.size,4); ofs.write((char*)&c.size,4);
        ofs.write((char*)&nameLen,2); ofs.write((char*)&extraLen,2); ofs.write((char*)&commentLen,2);
        ofs.write((char*)&diskStart,2); ofs.write((char*)&intAttr,2); ofs.write((char*)&extAttr,4);
        ofs.write((char*)&c.offset,4);
        ofs.write(c.name.data(), (std::streamsize)c.name.size());
    }
    uint32_t centralSize = (uint32_t)ofs.tellp() - centralStart;
    uint32_t endSig=0x06054b50; uint16_t diskNum=0, diskStart2=0, entriesDisk=(uint16_t)centrals.size(), entriesTotal=(uint16_t)centrals.size(), commentLen2=0;
    ofs.write((char*)&endSig,4); ofs.write((char*)&diskNum,2); ofs.write((char*)&diskStart2,2);
    ofs.write((char*)&entriesDisk,2); ofs.write((char*)&entriesTotal,2);
    ofs.write((char*)&centralSize,4); ofs.write((char*)&centralStart,4); ofs.write((char*)&commentLen2,2);
    ofs.close();
    EndProgress("완료: 압축됨 -> " + args[1] + " (" + std::to_string(files.size()) + "개 파일, 무압축 저장방식)");
}

static void CmdExtract(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Extract(\"파일.zip\",\"폴더\")", 3); return; }
    fs::path zipPath = fs::u8path(args[0]);
    std::error_code ec;
    if (!fs::exists(zipPath, ec)) { Log("[ERROR] 압축파일이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 압축 해제 예정: " + args[0] + " -> " + args[1], 2); return; }
    std::ifstream ifs(zipPath, std::ios::binary);
    std::vector<char> all((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    fs::path destDir = fs::u8path(args[1]);
    fs::create_directories(destDir, ec);
    size_t pos = 0; int count = 0, skipped = 0;
    while (pos + 30 <= all.size()) {
        uint32_t sig; memcpy(&sig, &all[pos], 4);
        if (sig != 0x04034b50) break;
        uint16_t method, nameLen, extraLen; uint32_t compSize;
        memcpy(&method, &all[pos+8], 2); memcpy(&compSize, &all[pos+18], 4);
        memcpy(&nameLen, &all[pos+26], 2); memcpy(&extraLen, &all[pos+28], 2);
        size_t nameStart = pos + 30;
        if (nameStart + nameLen > all.size()) break;
        std::string name(all.begin() + nameStart, all.begin() + nameStart + nameLen);
        size_t dataStart = nameStart + nameLen + extraLen;
        if (dataStart + compSize > all.size()) break;
        if (method == 0) {
            fs::path outPath = destDir / fs::u8path(name);
            if (!name.empty() && name.back() == '/') fs::create_directories(outPath, ec);
            else { fs::create_directories(outPath.parent_path(), ec); std::ofstream o(outPath, std::ios::binary); o.write(&all[dataStart], compSize); }
            count++;
        } else skipped++;
        pos = dataStart + compSize;
        ShowPercent("압축 해제 중", (int)(pos * 100 / (all.empty() ? 1 : all.size())));
    }
    EndProgress("완료: 압축 해제됨 -> " + args[1] + " (" + std::to_string(count) + "개, 미지원(압축방식) " + std::to_string(skipped) + "개)");
}

static void WatchThreadFunc(std::string folder, std::string command) {
    fs::path root = fs::u8path(folder);
    std::error_code ec;
    auto Snapshot = [&]() {
        std::map<std::string, fs::file_time_type> snap;
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
            std::error_code e2; snap[it->path().string()] = fs::last_write_time(it->path(), e2);
        }
        return snap;
    };
    auto prev = Snapshot();
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto cur = Snapshot();
        if (cur != prev) {
            Log("[Watch] 변경 감지: " + folder + " -> 실행: " + command, 2);
            ExecuteLine(command);
            prev = cur;
        }
    }
}
static void CmdWatch(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Watch(\"폴더경로\",\"실행할명령어\")", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 감시(1초 폴링) 시작 예정: " + args[0], 2); return; }
    std::thread(WatchThreadFunc, args[0], args[1]).detach();
    Log("완료: 감시 시작됨(1초 폴링) -> " + args[0] + " (변경시 실행: " + args[1] + ")", 1);
}

static void ScheduleThreadFunc(int seconds, std::string command) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    Log("[Schedule] 예약 실행: " + command, 2);
    ExecuteLine(command);
}
static void CmdSchedule(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Schedule(\"10s|5m|1h\",\"실행할명령어\")", 3); return; }
    int seconds = 0; std::string t = args[0];
    if (!t.empty()) {
        char unit = t.back();
        if (unit=='s'||unit=='m'||unit=='h') { int val = atoi(t.substr(0,t.size()-1).c_str()); seconds = (unit=='s')?val:(unit=='m')?val*60:val*3600; }
        else seconds = atoi(t.c_str());
    }
    if (seconds <= 0) { Log("[ERROR] 시간 형식 오류. 예: 10s, 5m, 1h", 3); return; }
    if (dryRun) { Log("[DRY-RUN] " + std::to_string(seconds) + "초 후 실행 예정: " + args[1], 2); return; }
    std::thread(ScheduleThreadFunc, seconds, args[1]).detach();
    Log("완료: " + std::to_string(seconds) + "초 후 실행 예약됨 -> " + args[1], 1);
}

static void CmdMacro(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Macro(\"이름\",[명령어1,명령어2,...])", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 매크로 등록 예정: " + args[0], 2); return; }
    std::vector<std::string> trimmed;
    for (auto& s : ParseBracketList(args[1])) { auto t = Trim(s); if (!t.empty()) trimmed.push_back(t); }
    g_macros[args[0]] = trimmed;
    Log("완료: 매크로 등록됨 -> " + args[0] + "() (" + std::to_string(trimmed.size()) + "단계)", 1);
}
static void CmdBatch(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Batch([명령어1,명령어2,...])", 3); return; }
    auto steps = ParseBracketList(args[0]);
    if (dryRun) { Log("[DRY-RUN] " + std::to_string(steps.size()) + "개 명령 순차 실행 예정", 2); return; }
    for (auto& s : steps) { auto t = Trim(s); if (!t.empty()) ExecuteLine(t); }
    Log("완료: Batch 실행됨 (" + std::to_string(steps.size()) + "단계)", 1);
}
static void CmdAlias(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Alias(\"이름\",\"명령어\")", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 별칭 등록 예정: " + args[0], 2); return; }
    g_macros[args[0]] = { args[1] };
    Log("완료: 별칭 등록됨 -> " + args[0] + "() 실행시 " + args[1], 1);
}

// ============================================================================
// [시스템/프로세스] Status/Monitor/Run/Kill/Service/Startup/Doctor/Repair/Optimize/Benchmark
// ============================================================================
static double GetCpuUsagePercent() {
#ifdef _WIN32
    FILETIME idle1,kernel1,user1,idle2,kernel2,user2;
    GetSystemTimes(&idle1,&kernel1,&user1);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    GetSystemTimes(&idle2,&kernel2,&user2);
    auto toULL=[](FILETIME f){ return (((ULONGLONG)f.dwHighDateTime)<<32)|f.dwLowDateTime; };
    ULONGLONG idle=toULL(idle2)-toULL(idle1), k=toULL(kernel2)-toULL(kernel1), u=toULL(user2)-toULL(user1);
    ULONGLONG total=k+u;
    return total==0?0.0:(1.0-(double)idle/(double)total)*100.0;
#else
    auto readStat = []() -> std::pair<long long,long long> {
        std::ifstream f("/proc/stat"); std::string cpu; long long user,nice_,sys,idle,iowait=0,irq=0,softirq=0;
        f >> cpu >> user >> nice_ >> sys >> idle >> iowait >> irq >> softirq;
        long long total = user+nice_+sys+idle+iowait+irq+softirq;
        return { idle, total };
    };
    auto a = readStat();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto b = readStat();
    long long idleD = b.first - a.first, totalD = b.second - a.second;
    return totalD==0?0.0:(1.0-(double)idleD/(double)totalD)*100.0;
#endif
}

static void CmdStatus(std::vector<std::string>&, bool dryRun) {
    if (dryRun) { Log("[DRY-RUN] 시스템 상태 조회 예정", 2); return; }
    double cpu = GetCpuUsagePercent();
    std::error_code ec;
    auto sp = fs::space(fs::current_path(), ec);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "CPU 사용률: " << cpu << "%\n";
#ifdef _WIN32
    MEMORYSTATUSEX ms{}; ms.dwLength=sizeof(ms); GlobalMemoryStatusEx(&ms);
    ss << "RAM: " << (ms.ullTotalPhys-ms.ullAvailPhys)/(1024*1024) << " MB / " << ms.ullTotalPhys/(1024*1024) << " MB 사용중 (부하 " << ms.dwMemoryLoad << "%)\n";
#else
    std::ifstream mf("/proc/meminfo");
    long long memTotal=0, memAvail=0; std::string key, unit; long long val;
    while (mf >> key >> val >> unit) { if (key=="MemTotal:") memTotal=val; else if (key=="MemAvailable:") memAvail=val; }
    long long usedMB=(memTotal-memAvail)/1024, totalMB=memTotal/1024;
    int loadPct = totalMB? (int)(usedMB*100/totalMB):0;
    ss << "RAM: " << usedMB << " MB / " << totalMB << " MB 사용중 (부하 " << loadPct << "%)\n";
#endif
    ss << "디스크 여유: " << (sp.available/1073741824.0) << " GB / " << (sp.capacity/1073741824.0) << " GB";
    Log(ss.str(), 1);
}

static void CmdMonitor(std::vector<std::string>&, bool dryRun) {
    if (dryRun) { Log("[DRY-RUN] 실시간 모니터링(5회 샘플) 예정", 2); return; }
    for (int i = 0; i < 5; i++) {
        double cpu = GetCpuUsagePercent();
        std::error_code ec; auto sp = fs::space(fs::current_path(), ec);
        std::ostringstream ss; ss << std::fixed << std::setprecision(1);
        ss << "[" << (i+1) << "/5] CPU " << cpu << "% | 디스크 여유 " << (sp.available/1073741824.0) << "GB";
        Log(ss.str(), 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

static void CmdRun(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Run(\"프로그램이름\")", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 실행 예정: " + args[0], 2); return; }
#ifdef _WIN32
    HINSTANCE r = ShellExecuteA(nullptr, "open", args[0].c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) { Log("[ERROR] 실행 실패: " + args[0], 3); return; }
#else
    int rc = system((args[0] + " >/dev/null 2>&1 &").c_str()); (void)rc;
#endif
    Log("완료: 실행됨 -> " + args[0], 1);
}

static void CmdKill(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Kill(\"프로그램이름\")", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 프로세스 종료 예정: " + args[0], 2); return; }
#ifdef _WIN32
    std::string target = ToLower(args[0]);
    if (target.find(".exe") == std::string::npos) target += ".exe";
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe{}; pe.dwSize = sizeof(pe);
    int killed = 0;
    if (snap != INVALID_HANDLE_VALUE && Process32First(snap, &pe)) {
        do { if (ToLower(pe.szExeFile) == target) { HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID); if (h) { TerminateProcess(h,0); CloseHandle(h); killed++; } } } while (Process32Next(snap, &pe));
    }
    if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
    if (killed > 0) Log("완료: " + std::to_string(killed) + "개 프로세스 종료됨 -> " + args[0], 1);
    else Log("[ERROR] 프로세스를 찾을 수 없습니다: " + args[0], 3);
#else
    DIR* d = opendir("/proc");
    int killed = 0;
    if (d) {
        struct dirent* ent;
        while ((ent = readdir(d))) {
            if (!isdigit((unsigned char)ent->d_name[0])) continue;
            std::ifstream cf(std::string("/proc/") + ent->d_name + "/comm");
            std::string comm; std::getline(cf, comm);
            if (comm == args[0]) { kill(atoi(ent->d_name), SIGTERM); killed++; }
        }
        closedir(d);
    }
    if (killed > 0) Log("완료: " + std::to_string(killed) + "개 프로세스 종료됨 -> " + args[0], 1);
    else Log("[ERROR] 프로세스를 찾을 수 없습니다: " + args[0], 3);
#endif
}

static void CmdService(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Service(\"이름\",\"start|stop|restart|status\")", 3); return; }
    std::string action = args.size() >= 2 ? ToLower(args[1]) : "status";
    if (dryRun) { Log("[DRY-RUN] 서비스 " + action + " 예정: " + args[0], 2); return; }
#ifdef _WIN32
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) { Log("[ERROR] 서비스 관리자 접근 실패 (관리자 권한 필요할 수 있음)", 3); return; }
    SC_HANDLE svc = OpenServiceA(scm, args[0].c_str(), SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!svc) { Log("[ERROR] 서비스를 찾을 수 없습니다: " + args[0], 3); CloseServiceHandle(scm); return; }
    SERVICE_STATUS_PROCESS ssp{}; DWORD need=0;
    if (action=="start") { StartServiceA(svc,0,nullptr); Log("완료: 서비스 시작 요청됨 -> " + args[0], 1); }
    else if (action=="stop") { SERVICE_STATUS st{}; ControlService(svc, SERVICE_CONTROL_STOP, &st); Log("완료: 서비스 중지 요청됨 -> " + args[0], 1); }
    else if (action=="restart") { SERVICE_STATUS st{}; ControlService(svc, SERVICE_CONTROL_STOP, &st); std::this_thread::sleep_for(std::chrono::seconds(1)); StartServiceA(svc,0,nullptr); Log("완료: 서비스 재시작됨 -> " + args[0], 1); }
    else { QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &need); std::string st = ssp.dwCurrentState==SERVICE_RUNNING?"실행중":ssp.dwCurrentState==SERVICE_STOPPED?"정지됨":"전환중"; Log("서비스 상태: " + args[0] + " -> " + st, 1); }
    CloseServiceHandle(svc); CloseServiceHandle(scm);
#else
    if (action=="status") Log(RunShellCapture("systemctl status " + args[0] + " --no-pager 2>&1"), 1);
    else { int rc = system(("systemctl " + action + " " + args[0] + " 2>&1").c_str()); Log(rc==0? "완료: 서비스 " + action + " 요청됨 -> " + args[0] : "[ERROR] 실패 (sudo 권한이 필요할 수 있습니다)", rc==0?1:3); }
#endif
}

static void CmdStartup(std::vector<std::string>&, bool dryRun) {
    if (dryRun) { Log("[DRY-RUN] 시작프로그램 조회 예정", 2); return; }
    std::string out;
#ifdef _WIN32
    HKEY hKey;
    auto listKey = [&](HKEY root, const char* name) {
        if (RegOpenKeyExA(root, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char kn[256], data[512]; DWORD idx=0, kl, dl, type;
            while (true) { kl=256; dl=sizeof(data); if (RegEnumValueA(hKey, idx, kn, &kl, nullptr, &type, (LPBYTE)data, &dl) != ERROR_SUCCESS) break; out += std::string(name)+" | "+kn+" = "+data+"\n"; idx++; }
            RegCloseKey(hKey);
        }
    };
    listKey(HKEY_CURRENT_USER, "HKCU");
    listKey(HKEY_LOCAL_MACHINE, "HKLM");
#else
    const char* home = getenv("HOME");
    if (home) {
        fs::path autostart = fs::path(home) / ".config/autostart";
        std::error_code ec;
        if (fs::exists(autostart, ec)) for (auto& e : fs::directory_iterator(autostart, ec)) out += e.path().filename().string() + "\n";
    }
#endif
    Log(out.empty() ? "등록된 시작 프로그램 없음." : out, 1);
}

static void CmdDoctor(std::vector<std::string>&, bool dryRun) {
    if (dryRun) { Log("[DRY-RUN] 시스템 진단 예정", 2); return; }
    std::error_code ec;
    auto sp = fs::space(fs::current_path(), ec);
    double freeGB = sp.available / 1073741824.0, totalGB = sp.capacity / 1073741824.0;
    std::ostringstream ss; ss << std::fixed << std::setprecision(1);
    ss << "=== jat Doctor 진단 결과 ===\n";
    ss << (freeGB < 10.0 ? "[경고] " : "[정상] ") << "드라이브 여유공간 " << freeGB << "GB / " << totalGB << "GB\n";
    double cpu = GetCpuUsagePercent();
    ss << (cpu > 90 ? "[경고] " : "[정상] ") << "CPU 사용률 " << cpu << "%\n";
    Log(ss.str(), 1);
}

static void CmdRepair(std::vector<std::string>&, bool dryRun) {
    if (dryRun) { Log("[DRY-RUN] 시스템 복구 진단 예정", 2); return; }
#ifdef _WIN32
    HINSTANCE r = ShellExecuteA(nullptr, "runas", "sfc.exe", "/scannow", nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) { Log("[ERROR] 복구 실행 실패 (관리자 권한 필요)", 3); return; }
    Log("완료: sfc /scannow 실행됨 (관리자 승인 창을 확인하세요)", 1);
#else
    Log("=== 최근 시스템 오류 로그 (journalctl) ===\n" + RunShellCapture("journalctl -p 3 -b --no-pager 2>/dev/null | tail -n 20"), 1);
#endif
}

static void CmdOptimize(std::vector<std::string>&, bool dryRun) {
    if (dryRun) { Log("[DRY-RUN] 임시파일 정리 예정", 2); return; }
    std::error_code ec;
#ifdef _WIN32
    char tempPath[MAX_PATH]; GetTempPathA(MAX_PATH, tempPath);
    fs::path tp(tempPath);
#else
    const char* tmpEnv = getenv("TMPDIR");
    fs::path tp = tmpEnv ? fs::path(tmpEnv) : fs::path("/tmp");
#endif
    int count = 0;
    for (auto& entry : fs::directory_iterator(tp, fs::directory_options::skip_permission_denied, ec)) {
        std::error_code delEc;
        if (fs::is_directory(entry, delEc)) fs::remove_all(entry.path(), delEc);
        else fs::remove(entry.path(), delEc);
        if (!delEc) count++;
    }
#ifdef _WIN32
    SHEmptyRecycleBinA(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
#else
    int rc = system("gio trash --empty >/dev/null 2>&1"); (void)rc;
#endif
    Log("완료: 임시파일 " + std::to_string(count) + "개 정리 + 휴지통 비움", 1);
}

static void CmdBenchmark(std::vector<std::string>&, bool dryRun) {
    if (dryRun) { Log("[DRY-RUN] 디스크 벤치마크 예정", 2); return; }
    fs::path testFile = fs::temp_directory_path() / "jat_bench_tmp.dat";
    const size_t chunkSize = 1024*1024; const int chunks = 50;
    std::vector<char> chunk(chunkSize, 'A');
    auto t1 = std::chrono::high_resolution_clock::now();
    { std::ofstream ofs(testFile, std::ios::binary); for (int i=0;i<chunks;i++){ ofs.write(chunk.data(), chunkSize); ShowPercent("쓰기 벤치마크", (i+1)*100/chunks); } }
    auto t2 = std::chrono::high_resolution_clock::now();
    { std::ifstream ifs(testFile, std::ios::binary); std::vector<char> buf(chunkSize); int i=0; while (ifs.read(buf.data(), chunkSize)) { i++; ShowPercent("읽기 벤치마크", i*100/chunks); } }
    auto t3 = std::chrono::high_resolution_clock::now();
    std::error_code ec; fs::remove(testFile, ec);
    double wSec = std::chrono::duration<double>(t2-t1).count(), rSec = std::chrono::duration<double>(t3-t2).count();
    std::ostringstream ss; ss << std::fixed << std::setprecision(1);
    ss << "쓰기 속도: " << (50.0/(wSec>0?wSec:0.001)) << " MB/s\n읽기 속도: " << (50.0/(rSec>0?rSec:0.001)) << " MB/s";
    EndProgress(ss.str());
}

// ============================================================================
// [네트워크] Download/Upload/Ping/Network/IP/DNS/Port/API
// ============================================================================
static void CmdDownload(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Download(\"url\",\"저장할파일명\")", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 다운로드 예정: " + args[0] + " -> " + args[1], 2); return; }
    std::cout << C_GREEN << "다운로드 중... (curl 필요)" << C_RESET << std::flush;
    int rc = system(("curl -sL -o \"" + args[1] + "\" \"" + args[0] + "\"").c_str());
    EndProgress(rc == 0 ? "완료: 다운로드됨 -> " + args[1] : "[ERROR] 다운로드 실패 (curl 확인 필요)");
}
static void CmdUpload(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Upload(\"파일\",\"서버URL\")", 3); return; }
    if (!fs::exists(fs::u8path(args[0]))) { Log("[ERROR] 파일이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 업로드 예정: " + args[0] + " -> " + args[1], 2); return; }
    int rc = system(("curl -s -T \"" + args[0] + "\" \"" + args[1] + "\"").c_str());
    Log(rc == 0 ? "완료: 업로드됨 -> " + args[1] : "[ERROR] 업로드 실패 (curl 확인 필요)", rc==0?1:3);
}
static void CmdPing(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Ping(\"주소\")", 3); return; }
    if (dryRun) { Log("[DRY-RUN] Ping 예정: " + args[0], 2); return; }
#ifdef _WIN32
    std::string cmd = "ping -n 4 " + args[0];
#else
    std::string cmd = "ping -c 4 " + args[0];
#endif
    Log(RunShellCapture(cmd + " 2>&1"), 1);
}
static void CmdIP(std::vector<std::string>&, bool dryRun) {
    if (dryRun) { Log("[DRY-RUN] IP 조회 예정", 2); return; }
    std::string out;
#ifdef _WIN32
    ULONG bufLen = 15000; std::vector<BYTE> buf(bufLen);
    auto addrs = (PIP_ADAPTER_ADDRESSES)buf.data();
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST, nullptr, addrs, &bufLen) == NO_ERROR) {
        for (auto p=addrs; p; p=p->Next) {
            if (p->OperStatus != IfOperStatusUp) continue;
            for (auto ua=p->FirstUnicastAddress; ua; ua=ua->Next) {
                sockaddr_in* sa=(sockaddr_in*)ua->Address.lpSockaddr;
                char ipStr[64]; inet_ntop(AF_INET,&sa->sin_addr,ipStr,64);
                int len = WideCharToMultiByte(CP_UTF8,0,p->FriendlyName,-1,nullptr,0,nullptr,nullptr);
                std::string name(len,0); WideCharToMultiByte(CP_UTF8,0,p->FriendlyName,-1,&name[0],len,nullptr,nullptr);
                out += name + ": " + ipStr + "\n";
            }
        }
    }
#else
    struct ifaddrs* ifap; if (getifaddrs(&ifap) == 0) {
        for (auto ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            char ipStr[64]; inet_ntop(AF_INET, &((sockaddr_in*)ifa->ifa_addr)->sin_addr, ipStr, 64);
            out += std::string(ifa->ifa_name) + ": " + ipStr + "\n";
        }
        freeifaddrs(ifap);
    }
#endif
    Log(out.empty() ? "활성 IP를 찾을 수 없습니다." : out, 1);
}
static void CmdNetwork(std::vector<std::string>& args, bool dryRun) { CmdIP(args, dryRun); }
static void CmdDNS(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: DNS(\"도메인\")", 3); return; }
    if (dryRun) { Log("[DRY-RUN] DNS 조회 예정: " + args[0], 2); return; }
    struct addrinfo hints{}; hints.ai_family = AF_UNSPEC;
    struct addrinfo* result = nullptr;
    if (getaddrinfo(args[0].c_str(), nullptr, &hints, &result) != 0) { Log("[ERROR] 조회 실패: " + args[0], 3); return; }
    std::string out;
    for (auto p = result; p; p = p->ai_next) {
        char ipStr[64];
        if (p->ai_family == AF_INET) { inet_ntop(AF_INET, &((sockaddr_in*)p->ai_addr)->sin_addr, ipStr, 64); out += "IPv4: " + std::string(ipStr) + "\n"; }
        else if (p->ai_family == AF_INET6) { inet_ntop(AF_INET6, &((sockaddr_in6*)p->ai_addr)->sin6_addr, ipStr, 64); out += "IPv6: " + std::string(ipStr) + "\n"; }
    }
    freeaddrinfo(result);
    Log(out.empty() ? "결과 없음" : out, 1);
}
static void CmdPort(std::vector<std::string>&, bool dryRun) {
    if (dryRun) { Log("[DRY-RUN] 열린 포트 조회 예정", 2); return; }
    std::string out;
#ifdef _WIN32
    ULONG size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    std::vector<BYTE> buf(size);
    auto table = (PMIB_TCPTABLE_OWNER_PID)buf.data();
    if (GetExtendedTcpTable(table, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) == NO_ERROR)
        for (DWORD i=0;i<table->dwNumEntries;i++) out += "포트 " + std::to_string(ntohs((u_short)table->table[i].dwLocalPort)) + " (PID " + std::to_string(table->table[i].dwOwningPid) + ")\n";
#else
    std::ifstream f("/proc/net/tcp");
    std::string line; std::getline(f, line);
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string idx, localAddr, state;
        iss >> idx >> localAddr >> idx >> state;
        if (state != "0A") continue;
        size_t colon = localAddr.find(':');
        if (colon == std::string::npos) continue;
        int port = std::stoi(localAddr.substr(colon+1), nullptr, 16);
        out += "포트 " + std::to_string(port) + " (LISTEN)\n";
    }
#endif
    Log(out.empty() ? "열린 포트 없음" : out, 1);
}
static void CmdApi(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: API(\"url\")", 3); return; }
    if (dryRun) { Log("[DRY-RUN] API GET 요청 예정: " + args[0], 2); return; }
    std::string out = RunShellCapture("curl -s \"" + args[0] + "\"");
    if (out.size() > 3000) out = out.substr(0, 3000) + "\n...(생략)";
    Log(out.empty() ? "[ERROR] 응답이 없거나 curl이 설치되어 있지 않습니다." : ("=== API 응답 ===\n" + out), 1);
}

// ============================================================================
// [보안] Checksum/HashCompare/Encrypt/Decrypt
// ============================================================================
static void CmdChecksum(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Checksum(\"파일\")", 3); return; }
    fs::path p = fs::u8path(args[0]);
    if (!fs::exists(p)) { Log("[ERROR] 파일이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 해시 계산 예정: " + args[0], 2); return; }
    Log("SHA-256: " + Sha256File(p), 1);
}
static void CmdHashCompare(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: HashCompare(\"파일1\",\"파일2\")", 3); return; }
    fs::path p1 = fs::u8path(args[0]), p2 = fs::u8path(args[1]);
    if (!fs::exists(p1) || !fs::exists(p2)) { Log("[ERROR] 파일을 찾을 수 없습니다", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 해시 비교 예정", 2); return; }
    Log(Sha256File(p1) == Sha256File(p2) ? "결과: 동일한 파일입니다 (해시 일치)" : "결과: 다른 파일입니다 (해시 불일치)", 1);
}
static void CmdEncrypt(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Encrypt(\"파일\",\"비밀번호\")", 3); return; }
    fs::path p = fs::u8path(args[0]);
    if (!fs::exists(p)) { Log("[ERROR] 파일이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 암호화 예정: " + args[0], 2); return; }
    std::ifstream f(p, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    uint8_t salt[16];
    std::random_device rd;
    for (auto& b : salt) b = (uint8_t)(rd() & 0xFF);
    auto enc = HashStreamCipher(data, args[1], salt);
    fs::path outPath = p; outPath += ".enc";
    std::ofstream ofs(outPath, std::ios::binary);
    ofs.write((char*)salt, 16);
    ofs.write((char*)enc.data(), (std::streamsize)enc.size());
    ofs.close();
    Log("완료: 암호화됨 (SHA-256 스트림암호, AES 아님) -> " + outPath.string(), 1);
}
static void CmdDecrypt(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Decrypt(\"파일\",\"비밀번호\")", 3); return; }
    fs::path p = fs::u8path(args[0]);
    if (!fs::exists(p)) { Log("[ERROR] 파일이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 복호화 예정: " + args[0], 2); return; }
    std::ifstream f(p, std::ios::binary);
    std::vector<uint8_t> all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    if (all.size() < 16) { Log("[ERROR] 잘못된 암호화 파일 형식", 3); return; }
    uint8_t salt[16]; memcpy(salt, all.data(), 16);
    std::vector<uint8_t> cipher(all.begin()+16, all.end());
    auto dec = HashStreamCipher(cipher, args[1], salt);
    std::string outName = p.string();
    if (outName.size() > 4 && outName.substr(outName.size()-4) == ".enc") outName = outName.substr(0, outName.size()-4);
    else outName += ".dec";
    std::ofstream ofs(outName, std::ios::binary);
    ofs.write((char*)dec.data(), (std::streamsize)dec.size());
    ofs.close();
    Log("완료: 복호화됨 -> " + outName + " (비밀번호가 틀려도 오류 없이 깨진 파일이 생성될 수 있으니 결과를 확인하세요)", 1);
}

// ============================================================================
// [강력한 추가 도구] Diff/Snapshot/Lock/Unlock/Holder/Risk/Estimate/Explain
// ============================================================================
static void CmdDiff(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Diff(\"파일1\",\"파일2\")", 3); return; }
    fs::path p1 = fs::u8path(args[0]), p2 = fs::u8path(args[1]);
    if (!fs::exists(p1) || !fs::exists(p2)) { Log("[ERROR] 파일을 찾을 수 없습니다", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 비교 예정: " + args[0] + " vs " + args[1], 2); return; }
    std::ifstream f1(p1), f2(p2);
    std::vector<std::string> l1, l2; std::string line;
    while (std::getline(f1, line)) l1.push_back(line);
    while (std::getline(f2, line)) l2.push_back(line);
    size_t maxN = (std::max)(l1.size(), l2.size());
    std::string out; int diffCount = 0;
    for (size_t i = 0; i < maxN; i++) {
        std::string a = i < l1.size() ? l1[i] : "(없음)", b = i < l2.size() ? l2[i] : "(없음)";
        if (a != b) { diffCount++; out += "L" + std::to_string(i+1) + ": - " + a + "\n     + " + b + "\n"; }
    }
    Log(diffCount == 0 ? "결과: 두 파일이 동일합니다." : ("차이 " + std::to_string(diffCount) + "줄:\n" + out), 1);
}
static void CmdSnapshot(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Snapshot(\"폴더\")", 3); return; }
    fs::path src = fs::u8path(args[0]);
    std::error_code ec;
    if (!fs::exists(src, ec)) { Log("[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 스냅샷 생성 예정: " + args[0], 2); return; }
    long long epoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    fs::path snapDir = fs::current_path() / ".jat_snapshots";
    fs::create_directories(snapDir, ec);
    fs::path dest = snapDir / (src.filename().string() + "_" + std::to_string(epoch));
    fs::copy(src, dest, fs::copy_options::recursive, ec);
    if (ec) { Log("[ERROR] 스냅샷 실패: " + ec.message(), 3); return; }
    Log("완료: 스냅샷 생성됨 -> .jat_snapshots/" + dest.filename().string(), 1);
}
static void CmdBackup(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Backup(\"파일/폴더\")", 3); return; }
    fs::path src = fs::u8path(args[0]);
    std::error_code ec;
    if (!fs::exists(src, ec)) { Log("[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 백업 생성 예정: " + args[0], 2); return; }
    long long epoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    fs::path dir = fs::current_path() / ".jat_backup";
    fs::create_directories(dir, ec);
    fs::path dest = dir / (src.filename().string() + "_" + std::to_string(epoch));
    fs::copy(src, dest, fs::copy_options::recursive, ec);
    if (ec) { Log("[ERROR] 백업 실패: " + ec.message(), 3); return; }
    Log("완료: 백업 생성됨 -> .jat_backup/" + dest.filename().string(), 1);
}
static void CmdLock(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Lock(\"파일\")", 3); return; }
    fs::path p = fs::u8path(args[0]);
    if (!fs::exists(p)) { Log("[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 잠금 예정: " + args[0], 2); return; }
    std::error_code ec;
#ifdef _WIN32
    SetFileAttributesA(p.string().c_str(), GetFileAttributesA(p.string().c_str()) | FILE_ATTRIBUTE_READONLY);
#else
    fs::permissions(p, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write, fs::perm_options::remove, ec);
#endif
    Log("완료: 잠금(읽기전용) 설정됨 -> " + args[0], 1);
}
static void CmdUnlock(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Unlock(\"파일\")", 3); return; }
    fs::path p = fs::u8path(args[0]);
    if (!fs::exists(p)) { Log("[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 잠금 해제 예정: " + args[0], 2); return; }
    std::error_code ec;
#ifdef _WIN32
    SetFileAttributesA(p.string().c_str(), GetFileAttributesA(p.string().c_str()) & ~FILE_ATTRIBUTE_READONLY);
#else
    fs::permissions(p, fs::perms::owner_write, fs::perm_options::add, ec);
#endif
    Log("완료: 잠금 해제됨 -> " + args[0], 1);
}

static void CmdHolder(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Holder(\"파일\")", 3); return; }
    fs::path p = fs::u8path(args[0]);
    if (!fs::exists(p)) { Log("[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 점유 프로세스 조회 예정: " + args[0], 2); return; }
#ifdef _WIN32
    HANDLE h = CreateFileA(p.string().c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION)
        Log("결과: 다른 프로세스가 파일을 사용 중입니다 (Windows에서는 프로세스명 특정이 제한적입니다).", 1);
    else { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); Log("결과: 현재 파일을 점유 중인 프로세스가 없습니다.", 1); }
#else
    char real[4096];
    std::string realStr = realpath(p.string().c_str(), real) ? real : p.string();
    std::string out; int count = 0;
    DIR* d = opendir("/proc");
    if (d) {
        struct dirent* ent;
        while ((ent = readdir(d))) {
            if (!isdigit((unsigned char)ent->d_name[0])) continue;
            std::string fdDir = std::string("/proc/") + ent->d_name + "/fd";
            DIR* fdd = opendir(fdDir.c_str());
            if (!fdd) continue;
            struct dirent* fe; bool matched = false;
            while ((fe = readdir(fdd))) {
                if (fe->d_name[0] == '.') continue;
                char target[4096];
                ssize_t n = readlink((fdDir + "/" + fe->d_name).c_str(), target, sizeof(target)-1);
                if (n > 0) { target[n] = 0; if (realStr == target) { matched = true; break; } }
            }
            closedir(fdd);
            if (matched) {
                std::ifstream cf(std::string("/proc/") + ent->d_name + "/comm");
                std::string comm; std::getline(cf, comm);
                out += "PID " + std::string(ent->d_name) + " : " + comm + "\n"; count++;
            }
        }
        closedir(d);
    }
    Log(count == 0 ? "결과: 현재 파일을 점유 중인 프로세스가 없습니다." : ("결과: " + std::to_string(count) + "개 프로세스가 사용중:\n" + out), 1);
#endif
}

struct PathStats { int files=0, dirs=0; uintmax_t size=0; };
static PathStats ScanStats(const fs::path& p) {
    PathStats s; std::error_code ec;
    if (!fs::exists(p, ec)) return s;
    if (fs::is_directory(p, ec)) {
        for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
            std::error_code e2;
            if (it->is_directory(e2)) s.dirs++; else { s.files++; s.size += it->file_size(e2); }
        }
    } else { s.files = 1; s.size = fs::file_size(p, ec); }
    return s;
}
static bool IsInUse(const fs::path& p) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) return false;
#ifdef _WIN32
    HANDLE h = CreateFileA(p.string().c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_SHARING_VIOLATION;
    CloseHandle(h); return false;
#else
    return false; // 리눅스는 공유 잠금 개념이 약해 간이 처리(항상 미사용으로 간주)
#endif
}
static void CmdRisk(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Risk(\"경로\")", 3); return; }
    fs::path p = fs::u8path(args[0]);
    if (!fs::exists(p)) { Log("[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 위험도 평가 예정: " + args[0], 2); return; }
    PathStats s = ScanStats(p);
    bool inUse = IsInUse(p);
    int score = (std::min)(s.dirs,20)*5 + (std::min)(s.files,50)*10 + (inUse?20:0);
    std::string level = score<50 ? "낮음" : score<150 ? "중간" : "높음";
    Log("=== 위험도 평가: " + args[0] + " ===\n하위 폴더 " + std::to_string(s.dirs) + "개 (+" + std::to_string((std::min)(s.dirs,20)*5) +
        ")\n파일 " + std::to_string(s.files) + "개 (+" + std::to_string((std::min)(s.files,50)*10) +
        ")\n사용중: " + std::string(inUse?"예 (+20)":"아니오") +
        "\n총 위험 점수: " + std::to_string(score) + " (" + level + ")", 1);
}
static void CmdEstimate(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Estimate(\"경로\")", 3); return; }
    fs::path p = fs::u8path(args[0]);
    if (!fs::exists(p)) { Log("[ERROR] 대상이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 실행 예상치 계산 예정: " + args[0], 2); return; }
    PathStats s = ScanStats(p);
    double estSec = (s.size / 1048576.0) / 80.0; // 가정: 80MB/s
    Log("=== 실행 예상치: " + args[0] + " ===\n변경될 파일 개수: " + std::to_string(s.files) +
        "개\n총 용량: " + std::to_string(s.size / 1048576) + " MB\n예상 처리 시간: 약 " +
        std::to_string((int)estSec + 1) + "초 (가정 처리속도 80MB/s)", 1);
}
static void CmdExplain(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Explain(\"명령어\",\"할 말\")", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 설명 등록 예정: " + args[0], 2); return; }
    g_explain[ToLower(args[0])] = args[1];
    Log("완료: 설명 등록됨 -> " + args[0] + "() 실행시 말풍선 출력", 1);
}

// ============================================================================
// [10개 추가 자동화/편의 명령어] Cd/Path/Timestamp/Env/Random/Count/Sort/Merge/Backup/WordCount
//  (Backup은 위쪽 강력한 추가도구 섹션에 포함됨)
// ============================================================================
static void CmdCd(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Cd(\"경로\")", 3); return; }
    std::error_code ec;
    fs::path p = fs::u8path(args[0]);
    if (!fs::exists(p, ec) || !fs::is_directory(p, ec)) { Log("[ERROR] 폴더가 아닙니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 작업폴더 변경 예정: " + args[0], 2); return; }
    fs::current_path(p, ec);
    if (ec) { Log("[ERROR] 이동 실패: " + ec.message(), 3); return; }
    Log("완료: 작업폴더 변경됨 -> " + fs::current_path().string(), 1);
}
static void CmdPath(std::vector<std::string>&, bool dryRun) {
    if (dryRun) { Log("[DRY-RUN] 현재 경로 조회 예정", 2); return; }
    Log(fs::current_path().string(), 1);
}
static void CmdTimestamp(std::vector<std::string>&, bool dryRun) {
    if (dryRun) { Log("[DRY-RUN] 현재 시각 조회 예정", 2); return; }
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    char buf[64]; std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    Log(std::string(buf), 1);
}
static void CmdEnv(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Env(\"변수명\")", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 환경변수 조회 예정: " + args[0], 2); return; }
    const char* v = getenv(args[0].c_str());
    Log(v ? (args[0] + " = " + v) : (args[0] + " : (설정되지 않음)"), 1);
}
static void CmdRandom(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 2) { Log("[ERROR] 사용법: Random(\"최소\",\"최대\")", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 난수 생성 예정: " + args[0] + " ~ " + args[1], 2); return; }
    int lo = atoi(args[0].c_str()), hi = atoi(args[1].c_str());
    if (lo > hi) std::swap(lo, hi);
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(lo, hi);
    Log("난수: " + std::to_string(dist(rng)), 1);
}
static void CmdCount(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Count(\"확장자\",\"경로\")", 3); return; }
    std::string ext = args[0]; if (!ext.empty() && ext[0] != '.') ext = "." + ext; ext = ToLower(ext);
    fs::path root = args.size() >= 2 ? fs::u8path(args[1]) : fs::current_path();
    if (dryRun) { Log("[DRY-RUN] 개수 집계 예정: *" + ext, 2); return; }
    int count = 0; std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); ++it) {
        std::error_code e2; if (!it->is_directory(e2) && ToLower(it->path().extension().string()) == ext) count++;
    }
    Log("개수: " + std::to_string(count) + "개 (*" + ext + ")", 1);
}
static void CmdSort(std::vector<std::string>& args, bool dryRun) {
    fs::path root = args.empty() ? fs::current_path() : fs::u8path(args[0]);
    std::string mode = args.size() >= 2 ? ToLower(args[1]) : "name";
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) { Log("[ERROR] 폴더가 아닙니다.", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 정렬 목록 조회 예정 (" + mode + ")", 2); return; }
    struct Item { std::string name; uintmax_t size; fs::file_time_type time; };
    std::vector<Item> items;
    for (auto& e : fs::directory_iterator(root, ec)) {
        std::error_code e2;
        items.push_back({ e.path().filename().string(), e.is_directory(e2)?0:e.file_size(e2), e.is_directory(e2)?fs::file_time_type{}:fs::last_write_time(e.path(), e2) });
    }
    if (mode == "size") std::sort(items.begin(), items.end(), [](auto&a,auto&b){ return a.size > b.size; });
    else if (mode == "date") std::sort(items.begin(), items.end(), [](auto&a,auto&b){ return a.time > b.time; });
    else std::sort(items.begin(), items.end(), [](auto&a,auto&b){ return a.name < b.name; });
    std::string out;
    for (auto& it : items) out += it.name + "  (" + std::to_string(it.size) + " bytes)\n";
    Log(out.empty() ? "빈 폴더입니다." : out, 1);
}
static void CmdMerge(std::vector<std::string>& args, bool dryRun) {
    if (args.size() < 3) { Log("[ERROR] 사용법: Merge(\"파일1\",\"파일2\",\"결과파일\")", 3); return; }
    fs::path p1 = fs::u8path(args[0]), p2 = fs::u8path(args[1]);
    if (!fs::exists(p1) || !fs::exists(p2)) { Log("[ERROR] 파일을 찾을 수 없습니다", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 병합 예정: " + args[0] + " + " + args[1] + " -> " + args[2], 2); return; }
    std::ifstream f1(p1, std::ios::binary), f2(p2, std::ios::binary);
    std::ofstream out(args[2], std::ios::binary);
    out << f1.rdbuf() << f2.rdbuf();
    Log("완료: 병합됨 -> " + args[2], 1);
}
static void CmdWordCount(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: WordCount(\"파일\")", 3); return; }
    fs::path p = fs::u8path(args[0]);
    if (!fs::exists(p)) { Log("[ERROR] 파일이 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] 단어/줄 수 계산 예정: " + args[0], 2); return; }
    std::ifstream f(p, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    long lines = (long)std::count(content.begin(), content.end(), '\n');
    long chars = (long)content.size();
    std::istringstream iss(content);
    long words = std::distance(std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>());
    Log("줄 수: " + std::to_string(lines) + "\n단어 수: " + std::to_string(words) + "\n글자 수: " + std::to_string(chars), 1);
}

// ============================================================================
// [Python 코드 실행] ^(코드)^
// ============================================================================
static void RunPythonCode(const std::string& code) {
    fs::path tmp = fs::temp_directory_path() / "jat_pytemp.py";
    { std::ofstream f(tmp, std::ios::binary); f << code; }
#ifdef _WIN32
    std::string cmd = "python \"" + tmp.string() + "\" 2>&1";
#else
    std::string cmd = "python3 \"" + tmp.string() + "\" 2>&1";
#endif
    std::string output = RunShellCapture(cmd);
    std::error_code ec; fs::remove(tmp, ec);
    Log("[Python 실행결과]\n" + (output.empty() ? "(출력 없음, python/python3 설치 여부를 확인하세요)" : output), 1);
}

// ============================================================================
// [파일 탐색] file find <이름> / file ext find <확장자>  (크로스 플랫폼)
// std::filesystem 자체가 Windows/Linux 공용 API이므로 별도 dirent.h 분기는
// 두지 않고, 대신 CPU 부하가 큰 상위 폴더 탐색만 OS 무관 공용 로직으로 구현.
// ============================================================================
static void RecursiveSearchDown(const fs::path& root, const std::function<bool(const fs::path&)>& matchFn,
                                 std::vector<std::string>& results, int& skipped) {
    std::error_code ec;
    if (!fs::exists(root, ec)) throw std::runtime_error("경로가 존재하지 않습니다: " + root.string());
    if (!fs::is_directory(root, ec)) throw std::runtime_error("폴더가 아닙니다: " + root.string());
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) throw std::runtime_error("권한 없음(또는 접근 불가): " + root.string());
    while (it != end) {
        std::error_code entEc;
        if (matchFn(it->path())) results.push_back(it->path().string());
        it.increment(entEc);
        if (entEc) skipped++;
    }
}
static void UpwardShallowSearch(const fs::path& start, const std::function<bool(const fs::path&)>& matchFn, std::vector<std::string>& results) {
    std::error_code ec;
    fs::path cur = fs::absolute(start, ec);
    fs::path prevChild = cur;
    while (cur.has_parent_path()) {
        fs::path parent = cur.parent_path();
        if (parent == cur) break;
        for (auto& e : fs::directory_iterator(parent, fs::directory_options::skip_permission_denied, ec)) {
            if (e.path() == prevChild) continue; // 이미 아래로 검색한 가지는 건너뜀
            if (matchFn(e.path())) results.push_back(e.path().string());
        }
        prevChild = parent;
        cur = parent;
    }
}
static void CmdFileFindImpl(const std::string& name) {
    std::string needle = ToLower(name);
    std::vector<std::string> results; int skipped = 0;
    auto matcher = [&](const fs::path& p) { return ToLower(p.filename().string()).find(needle) != std::string::npos; };
    try { RecursiveSearchDown(fs::current_path(), matcher, results, skipped); }
    catch (std::exception& e) { Log(std::string("[ERROR] ") + e.what(), 3); return; }
    UpwardShallowSearch(fs::current_path(), matcher, results);
    std::string out; for (auto& r : results) out += r + "\n";
    if (skipped > 0) out += "(권한 없음 등으로 " + std::to_string(skipped) + "개 항목 건너뜀)\n";
    Log(results.empty() ? "검색 결과 없음: " + name : ("검색 결과 " + std::to_string(results.size()) + "건 (하위+상위):\n" + out), results.empty() ? 2 : 1);
}
static void CmdFileExtFindImpl(const std::string& ext) {
    std::string e2 = ext; if (!e2.empty() && e2[0] != '.') e2 = "." + e2; e2 = ToLower(e2);
    std::vector<std::string> results; int skipped = 0;
    auto matcher = [&](const fs::path& p) { std::error_code ec3; return !fs::is_directory(p, ec3) && ToLower(p.extension().string()) == e2; };
    try { RecursiveSearchDown(fs::current_path(), matcher, results, skipped); }
    catch (std::exception& e) { Log(std::string("[ERROR] ") + e.what(), 3); return; }
    UpwardShallowSearch(fs::current_path(), matcher, results);
    std::string out; for (auto& r : results) out += r + "\n";
    if (skipped > 0) out += "(권한 없음 등으로 " + std::to_string(skipped) + "개 항목 건너뜀)\n";
    Log(results.empty() ? "검색 결과 없음: *" + e2 : ("검색 결과 " + std::to_string(results.size()) + "건 (하위+상위):\n" + out), results.empty() ? 2 : 1);
}
// "file find <이름>" / "file ext find <확장자>" (공백 구분 명령, 괄호형이 아님)
static void HandleFileCommand(const std::string& line) {
    std::istringstream iss(line);
    std::string tok1, tok2;
    iss >> tok1 >> tok2;
    if (ToLower(tok2) == "find") {
        std::string rest; std::getline(iss, rest); rest = Trim(rest);
        if (rest.empty()) { Log("[ERROR] 사용법: file find <이름>", 3); return; }
        CmdFileFindImpl(rest);
    } else if (ToLower(tok2) == "ext") {
        std::string tok3; iss >> tok3;
        if (ToLower(tok3) != "find") { Log("[ERROR] 사용법: file ext find <확장자>", 3); return; }
        std::string rest; std::getline(iss, rest); rest = Trim(rest);
        if (rest.empty()) { Log("[ERROR] 사용법: file ext find <확장자>", 3); return; }
        CmdFileExtFindImpl(rest);
    } else {
        Log("[ERROR] 사용법: file find <이름> | file ext find <확장자>", 3);
    }
}

// ============================================================================
// [외부 프로그램 자동화] Wait / MouseMoveX / MouseMoveY / Mouse(Click) / Keyboard(key)
// Windows: SendInput   |   Linux: X11 / XTest (libx11-dev, libxtst-dev 필요, X 세션 필요)
// ============================================================================
#ifdef _WIN32
static void PlatformMouseMoveRelative(int dx, int dy) {
    INPUT input{}; input.type = INPUT_MOUSE; input.mi.dx = dx; input.mi.dy = dy; input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}
// Windows: GetSystemMetrics + SetCursorPos
static void PlatformCenterMouse() {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    if (screenW <= 0 || screenH <= 0) return; // 조용히 실패
    SetCursorPos(screenW / 2, screenH / 2);
}
// Windows: INPUT + SendInput (LEFTDOWN/LEFTUP), 50ms 안정화 대기
static void PlatformMouseClickLeft() {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_MOUSE; inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].type = INPUT_MOUSE; inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, inputs, sizeof(INPUT));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
// Windows: INPUT + SendInput (RIGHTDOWN/RIGHTUP), 50ms 안정화 대기
static void PlatformMouseClickRight() {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_MOUSE; inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
    inputs[1].type = INPUT_MOUSE; inputs[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    SendInput(2, inputs, sizeof(INPUT));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
static void PlatformSendKey(const std::string& key) {
    std::string lower = ToLower(key);
    WORD vk;
    if (lower=="enter") vk = VK_RETURN;
    else if (lower=="tab") vk = VK_TAB;
    else if (lower=="esc"||lower=="escape") vk = VK_ESCAPE;
    else if (lower=="space") vk = VK_SPACE;
    else if (lower=="backspace") vk = VK_BACK;
    else if (key.size()==1) { SHORT s = VkKeyScanA(key[0]); vk = LOBYTE(s); }
    else throw std::runtime_error("지원하지 않는 키 이름: " + key);
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = vk;
    inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = vk; inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}
#else
static void PlatformMouseMoveRelative(int dx, int dy) {
    Display* d = XOpenDisplay(nullptr);
    if (!d) throw std::runtime_error("X11 디스플레이를 열 수 없습니다 (DISPLAY 환경변수 확인)");
    XWarpPointer(d, None, None, 0, 0, 0, 0, dx, dy);
    XFlush(d);
    XCloseDisplay(d);
}
// Linux: X11 Xlib - 화면 중앙으로 워프, 에러 시 조용히 실패
static void PlatformCenterMouse() {
    Display* d = XOpenDisplay(nullptr);
    if (!d) return; // 조용히 실패
    int screen = DefaultScreen(d);
    int screenW = DisplayWidth(d, screen);
    int screenH = DisplayHeight(d, screen);
    Window root = RootWindow(d, screen);
    XWarpPointer(d, None, root, 0, 0, 0, 0, screenW / 2, screenH / 2);
    XFlush(d);
    XCloseDisplay(d);
}
// Linux: XTest button 1 = 좌클릭, XSync로 이벤트 플러시, 50ms 안정화 대기
static void PlatformMouseClickLeft() {
    Display* d = XOpenDisplay(nullptr);
    if (!d) throw std::runtime_error("X11 디스플레이를 열 수 없습니다 (DISPLAY 환경변수 확인)");
    XTestFakeButtonEvent(d, 1, True, CurrentTime);
    XTestFakeButtonEvent(d, 1, False, CurrentTime);
    XSync(d, False);
    XCloseDisplay(d);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
// Linux: XTest button 3 = 우클릭, XSync로 이벤트 플러시, 50ms 안정화 대기
static void PlatformMouseClickRight() {
    Display* d = XOpenDisplay(nullptr);
    if (!d) throw std::runtime_error("X11 디스플레이를 열 수 없습니다 (DISPLAY 환경변수 확인)");
    XTestFakeButtonEvent(d, 3, True, CurrentTime);
    XTestFakeButtonEvent(d, 3, False, CurrentTime);
    XSync(d, False);
    XCloseDisplay(d);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
static void PlatformSendKey(const std::string& key) {
    Display* d = XOpenDisplay(nullptr);
    if (!d) throw std::runtime_error("X11 디스플레이를 열 수 없습니다 (DISPLAY 환경변수 확인)");
    std::string mapped = key, lower = ToLower(key);
    if (lower=="enter") mapped="Return";
    else if (lower=="esc"||lower=="escape") mapped="Escape";
    else if (lower=="space") mapped="space";
    else if (lower=="backspace") mapped="BackSpace";
    else if (lower=="tab") mapped="Tab";
    KeySym ks = XStringToKeysym(mapped.c_str());
    if (ks == NoSymbol) { XCloseDisplay(d); throw std::runtime_error("지원하지 않는 키 이름: " + key); }
    KeyCode kc = XKeysymToKeycode(d, ks);
    XTestFakeKeyEvent(d, kc, True, 0);
    XTestFakeKeyEvent(d, kc, False, 0);
    XFlush(d);
    XCloseDisplay(d);
}
#endif
static void CmdWait(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Wait(ms)", 3); return; }
    int ms = atoi(args[0].c_str());
    if (dryRun) { Log("[DRY-RUN] " + std::to_string(ms) + "ms 대기 예정", 2); return; }
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    Log("완료: " + std::to_string(ms) + "ms 대기함", 1);
}
static void CmdMouseMoveX(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: MouseMoveX(n)", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 마우스 X " + args[0] + " 이동 예정", 2); return; }
    try { PlatformMouseMoveRelative(atoi(args[0].c_str()), 0); Log("완료: 마우스 X축 " + args[0] + " 이동함", 1); }
    catch (std::exception& e) { Log(std::string("[ERROR] ") + e.what(), 3); }
}
static void CmdMouseMoveY(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: MouseMoveY(n)", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 마우스 Y " + args[0] + " 이동 예정", 2); return; }
    try { PlatformMouseMoveRelative(0, atoi(args[0].c_str())); Log("완료: 마우스 Y축 " + args[0] + " 이동함", 1); }
    catch (std::exception& e) { Log(std::string("[ERROR] ") + e.what(), 3); }
}
static void CmdCenterMouse(std::vector<std::string>&, bool dryRun) {
    if (dryRun) { Log("[DRY-RUN] 마우스 화면 중앙 고정 예정", 2); return; }
    PlatformCenterMouse(); // 실패해도 조용히 무시 (사양)
    Log("완료: 마우스가 화면 중앙으로 이동함", 1);
}
static void CmdMouseLeft(std::vector<std::string>&, bool dryRun) {
    if (dryRun) { Log("[DRY-RUN] 좌클릭 예정", 2); return; }
    try { PlatformMouseClickLeft(); Log("완료: 좌클릭됨", 1); }
    catch (std::exception& e) { Log(std::string("[ERROR] ") + e.what(), 3); }
}
static void CmdMouseRight(std::vector<std::string>&, bool dryRun) {
    if (dryRun) { Log("[DRY-RUN] 우클릭 예정", 2); return; }
    try { PlatformMouseClickRight(); Log("완료: 우클릭됨", 1); }
    catch (std::exception& e) { Log(std::string("[ERROR] ") + e.what(), 3); }
}
// Mouse("Click"|"Right") - 하위 호환용 라우터. 내부 구현은 좌/우로 완전히 분리되어 있음.
static void CmdMouse(std::vector<std::string>& args, bool dryRun) {
    std::string opt = args.empty() ? "click" : ToLower(args[0]);
    if (opt == "click" || opt == "left") CmdMouseLeft(args, dryRun);
    else if (opt == "right") CmdMouseRight(args, dryRun);
    else Log("[ERROR] 사용법: Mouse(\"Click\") 또는 Mouse(\"Right\") - 권장: MouseLeft() / MouseRight()", 3);
}
static void CmdKeyboard(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Keyboard(\"key\")", 3); return; }
    if (dryRun) { Log("[DRY-RUN] 키 입력 예정: " + args[0], 2); return; }
    try { PlatformSendKey(args[0]); Log("완료: 키 입력됨 -> " + args[0], 1); }
    catch (std::exception& e) { Log(std::string("[ERROR] ") + e.what(), 3); }
}

// ============================================================================
// [Lua 스크립트 실행] 프로세스 격리, lua.dll/liblua.so 동적 로딩 없음, 30초 타임아웃
// ============================================================================
struct LuaResult { std::string output; int exitCode; };

static LuaResult LuaRun(const std::string& scriptPath, const std::vector<std::string>& args) {
    LuaResult result{ "", -1 };
#ifdef _WIN32
    // Windows: CreateProcessA + 익명 파이프(CreatePipe) + CREATE_NO_WINDOW
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = nullptr;
    HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) { result.output = "[ERROR] 파이프 생성 실패"; return result; }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    std::string cmdLine = "lua.exe \"" + scriptPath + "\"";
    for (auto& a : args) cmdLine += " \"" + a + "\"";
    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    STARTUPINFOA si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWritePipe; si.hStdError = hWritePipe; si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWritePipe); // 부모는 쓰기핸들 불필요 - 반드시 닫아야 자식 종료가 파이프 EOF로 감지됨

    if (!ok) { CloseHandle(hReadPipe); result.output = "[ERROR] lua 실행 파일을 찾을 수 없습니다 (PATH 확인 필요)"; result.exitCode = -1; return result; }

    DWORD waitRc = WaitForSingleObject(pi.hProcess, 30000);
    if (waitRc == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        result.output = "[ERROR] 30초 타임아웃으로 강제 종료됨"; result.exitCode = -1;
        CloseHandle(hReadPipe); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return result;
    }
    char buf[4096]; DWORD bytesRead = 0; std::string output;
    while (ReadFile(hReadPipe, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) output.append(buf, bytesRead);
    DWORD exitCode = 0; GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(hReadPipe); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    result.output = output; result.exitCode = (int)exitCode;
    return result;
#else
    // Linux: popen("lua ...") + timeout 명령으로 30초 강제 종료, 2>&1로 stderr 병합 캡처
    std::string cmd = "timeout 30 lua \"" + scriptPath + "\"";
    for (auto& a : args) cmd += " \"" + a + "\"";
    cmd += " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { result.output = "[ERROR] lua 명령을 실행할 수 없습니다"; result.exitCode = -1; return result; }
    char buf[4096]; std::string output;
    while (fgets(buf, sizeof(buf), pipe)) output += buf;
    int status = pclose(pipe);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (code == 127 || output.find("not found") != std::string::npos) {
        result.output = "[ERROR] lua 명령어를 찾을 수 없습니다 (PATH 확인 필요)"; result.exitCode = -1; return result;
    }
    if (code == 124) { result.output = "[ERROR] 30초 타임아웃으로 강제 종료됨"; result.exitCode = -1; return result; }
    result.output = output; result.exitCode = code;
    return result;
#endif
}

static void CmdLua(std::vector<std::string>& args, bool dryRun) {
    if (args.empty()) { Log("[ERROR] 사용법: Lua(\"스크립트경로\",\"인자1 인자2 ...\")", 3); return; }
    if (!fs::exists(fs::u8path(args[0]))) { Log("[ERROR] 스크립트를 찾을 수 없습니다: " + args[0], 3); return; }
    if (dryRun) { Log("[DRY-RUN] Lua 스크립트 실행 예정: " + args[0], 2); return; }
    std::vector<std::string> luaArgs;
    if (args.size() >= 2 && !args[1].empty()) {
        std::istringstream iss(args[1]); std::string tok;
        while (iss >> tok) luaArgs.push_back(tok);
    }
    LuaResult r = LuaRun(args[0], luaArgs);
    Log("=== Lua 실행결과 (exitCode=" + std::to_string(r.exitCode) + ") ===\n" + (r.output.empty() ? "(출력 없음)" : r.output), r.exitCode == 0 ? 1 : 3);
}

// ============================================================================
// [jat코드 컴파일러] Console(...) / 변수 대입 / list / 사칙연산 / 주석(//)
// 구조: 토크나이저 -> 파서(재귀하강, AST 생성) -> 실행기(Evaluator)
// 한 줄 = 한 문장 모델. run script.jat 로 파일 실행, 인라인 타이핑도 지원.
// ============================================================================
struct JatValue {
    enum T { NUM, STR, LIST } type = NUM;
    double num = 0; std::string str; std::vector<JatValue> list;
};
static std::string JatValueToString(const JatValue& v) {
    if (v.type == JatValue::NUM) {
        std::ostringstream ss;
        if (v.num == (long long)v.num) ss << (long long)v.num; else ss << v.num;
        return ss.str();
    } else if (v.type == JatValue::STR) return v.str;
    std::string out = "[";
    for (size_t i = 0; i < v.list.size(); i++) { if (i) out += ", "; out += (v.list[i].type == JatValue::STR ? ("\"" + v.list[i].str + "\"") : JatValueToString(v.list[i])); }
    out += "]"; return out;
}
static std::map<std::string, JatValue> g_jatVars;

struct JatToken {
    enum Ty { NUM, STR, IDENT, PLUS, MINUS, STAR, SLASH, ASSIGN, EQ, NE, LT, GT, LE, GE, LP, RP, LB, RB, COMMA, END } ty = END;
    std::string s; double n = 0; int col = 0;
};
struct JatParseError : std::runtime_error {
    JatParseError(int line, int col, const std::string& msg)
        : std::runtime_error("구문 오류 (줄 " + std::to_string(line) + ", 열 " + std::to_string(col) + "): " + msg) {}
};

static std::vector<JatToken> JatTokenizeLine(const std::string& lineRaw, int lineNo) {
    std::string line = lineRaw;
    size_t cpos = line.find("//");
    bool inQuoteScan = false;
    for (size_t k = 0; cpos != std::string::npos && k < cpos; k++) { if (line[k]=='"' && (k==0||line[k-1]!='\\')) inQuoteScan = !inQuoteScan; }
    if (cpos != std::string::npos && !inQuoteScan) line = line.substr(0, cpos);
    std::vector<JatToken> toks;
    size_t i = 0;
    while (i < line.size()) {
        char c = line[i];
        int col = (int)i + 1;
        if (isspace((unsigned char)c)) { i++; continue; }
        if (isdigit((unsigned char)c) || (c=='.' && i+1<line.size() && isdigit((unsigned char)line[i+1]))) {
            size_t start=i; while (i<line.size() && (isdigit((unsigned char)line[i])||line[i]=='.')) i++;
            JatToken t; t.ty=JatToken::NUM; t.s=line.substr(start,i-start); t.n=atof(t.s.c_str()); t.col=col; toks.push_back(t);
            continue;
        }
        if (c=='"') {
            size_t j=i+1; std::string val;
            while (j<line.size() && line[j] != '"') { if (line[j]=='\\' && j+1<line.size()) { val += line[j+1]; j+=2; continue; } val += line[j]; j++; }
            if (j>=line.size()) throw JatParseError(lineNo, col, "문자열이 닫히지 않았습니다 (따옴표 누락)");
            JatToken t; t.ty=JatToken::STR; t.s=val; t.col=col; toks.push_back(t);
            i = j+1; continue;
        }
        if (isalpha((unsigned char)c) || c=='_') {
            size_t start=i; while (i<line.size() && (isalnum((unsigned char)line[i])||line[i]=='_')) i++;
            JatToken t; t.ty=JatToken::IDENT; t.s=line.substr(start,i-start); t.col=col; toks.push_back(t);
            continue;
        }
        JatToken t; t.col=col;
        if (c=='=' && i+1<line.size() && line[i+1]=='=') { t.ty=JatToken::EQ; i+=2; }
        else if (c=='!' && i+1<line.size() && line[i+1]=='=') { t.ty=JatToken::NE; i+=2; }
        else if (c=='<' && i+1<line.size() && line[i+1]=='=') { t.ty=JatToken::LE; i+=2; }
        else if (c=='>' && i+1<line.size() && line[i+1]=='=') { t.ty=JatToken::GE; i+=2; }
        else if (c=='<') { t.ty=JatToken::LT; i++; }
        else if (c=='>') { t.ty=JatToken::GT; i++; }
        else if (c=='+') { t.ty=JatToken::PLUS; i++; }
        else if (c=='-') { t.ty=JatToken::MINUS; i++; }
        else if (c=='*') { t.ty=JatToken::STAR; i++; }
        else if (c=='/') { t.ty=JatToken::SLASH; i++; }
        else if (c=='=') { t.ty=JatToken::ASSIGN; i++; }
        else if (c=='(') { t.ty=JatToken::LP; i++; }
        else if (c==')') { t.ty=JatToken::RP; i++; }
        else if (c=='[') { t.ty=JatToken::LB; i++; }
        else if (c==']') { t.ty=JatToken::RB; i++; }
        else if (c==',') { t.ty=JatToken::COMMA; i++; }
        else throw JatParseError(lineNo, col, std::string("알 수 없는 문자: '") + c + "'");
        toks.push_back(t);
    }
    JatToken end; end.ty=JatToken::END; end.col=(int)line.size()+1; toks.push_back(end);
    return toks;
}

struct JatExprNode {
    enum K { NUM, STR, VAR, BINOP, LIST } kind = NUM;
    double num=0; std::string str, varName; char op=0;
    std::shared_ptr<JatExprNode> left, right;
    std::vector<std::shared_ptr<JatExprNode>> items;
};
using JatExprPtr = std::shared_ptr<JatExprNode>;
struct JatStmtNode { enum K { CONSOLE, ASSIGN, LISTDECL } kind; std::string varName; JatExprPtr expr; int line=0; };

struct JatParser {
    std::vector<JatToken> toks; size_t pos=0; int lineNo;
    JatParser(std::vector<JatToken> t, int ln): toks(std::move(t)), lineNo(ln) {}
    JatToken& cur() { return toks[pos]; }
    bool check(JatToken::Ty t) { return cur().ty==t; }
    JatToken advance() { return toks[pos++]; }
    void expect(JatToken::Ty t, const std::string& what) { if (!check(t)) throw JatParseError(lineNo, cur().col, what + " 필요"); advance(); }
    JatExprPtr parseFactor() {
        if (check(JatToken::NUM)) { auto t=advance(); auto n=std::make_shared<JatExprNode>(); n->kind=JatExprNode::NUM; n->num=t.n; return n; }
        if (check(JatToken::STR)) { auto t=advance(); auto n=std::make_shared<JatExprNode>(); n->kind=JatExprNode::STR; n->str=t.s; return n; }
        if (check(JatToken::IDENT)) { auto t=advance(); auto n=std::make_shared<JatExprNode>(); n->kind=JatExprNode::VAR; n->varName=t.s; return n; }
        if (check(JatToken::LP)) { advance(); auto e=parseExpr(); expect(JatToken::RP, "')'"); return e; }
        throw JatParseError(lineNo, cur().col, "값(숫자/문자열/변수)이 필요합니다");
    }
    JatExprPtr parseTerm() {
        auto left = parseFactor();
        while (check(JatToken::STAR) || check(JatToken::SLASH)) {
            char op = check(JatToken::STAR) ? '*' : '/'; advance();
            auto right = parseFactor();
            auto n = std::make_shared<JatExprNode>(); n->kind=JatExprNode::BINOP; n->op=op; n->left=left; n->right=right; left=n;
        }
        return left;
    }
    JatExprPtr parseExpr() {
        auto left = parseTerm();
        while (check(JatToken::PLUS) || check(JatToken::MINUS)) {
            char op = check(JatToken::PLUS) ? '+' : '-'; advance();
            auto right = parseTerm();
            auto n = std::make_shared<JatExprNode>(); n->kind=JatExprNode::BINOP; n->op=op; n->left=left; n->right=right; left=n;
        }
        return left;
    }
    JatExprPtr parseListLiteral() {
        expect(JatToken::LB, "'['");
        auto n = std::make_shared<JatExprNode>(); n->kind=JatExprNode::LIST;
        if (!check(JatToken::RB)) {
            while (true) {
                if (!check(JatToken::STR)) throw JatParseError(lineNo, cur().col, "리스트 항목은 반드시 따옴표로 감싼 문자열이어야 합니다");
                auto t = advance();
                auto item = std::make_shared<JatExprNode>(); item->kind=JatExprNode::STR; item->str=t.s;
                n->items.push_back(item);
                if (check(JatToken::COMMA)) { advance(); continue; }
                break;
            }
        }
        expect(JatToken::RB, "']'");
        return n;
    }
    JatStmtNode parseStatement() {
        JatStmtNode stmt; stmt.line = lineNo;
        if (check(JatToken::IDENT) && cur().s == "Console") {
            advance(); expect(JatToken::LP, "'('");
            stmt.kind = JatStmtNode::CONSOLE; stmt.expr = parseExpr();
            expect(JatToken::RP, "')'"); return stmt;
        }
        if (check(JatToken::IDENT) && cur().s == "list") {
            advance();
            if (!check(JatToken::IDENT)) throw JatParseError(lineNo, cur().col, "리스트 변수명이 필요합니다");
            stmt.varName = advance().s;
            expect(JatToken::ASSIGN, "'='");
            stmt.kind = JatStmtNode::LISTDECL; stmt.expr = parseListLiteral();
            return stmt;
        }
        if (check(JatToken::IDENT)) {
            std::string name = advance().s;
            expect(JatToken::ASSIGN, "'='");
            stmt.kind = JatStmtNode::ASSIGN; stmt.varName = name; stmt.expr = parseExpr();
            return stmt;
        }
        throw JatParseError(lineNo, cur().col, "문장을 해석할 수 없습니다 (Console(...), list 변수=[...], 변수=식 중 하나여야 함)");
    }
};

static JatValue JatEval(const JatExprPtr& e, int lineNo) {
    if (!e) throw JatParseError(lineNo, 0, "빈 표현식");
    switch (e->kind) {
        case JatExprNode::NUM: { JatValue v; v.type=JatValue::NUM; v.num=e->num; return v; }
        case JatExprNode::STR: { JatValue v; v.type=JatValue::STR; v.str=e->str; return v; }
        case JatExprNode::VAR: {
            auto it = g_jatVars.find(e->varName);
            if (it == g_jatVars.end()) throw JatParseError(lineNo, 0, "정의되지 않은 변수: " + e->varName);
            return it->second;
        }
        case JatExprNode::LIST: { JatValue v; v.type=JatValue::LIST; for (auto& it : e->items) v.list.push_back(JatEval(it, lineNo)); return v; }
        case JatExprNode::BINOP: {
            JatValue l = JatEval(e->left, lineNo), r = JatEval(e->right, lineNo);
            if (e->op=='+') {
                if (l.type==JatValue::STR || r.type==JatValue::STR) { JatValue v; v.type=JatValue::STR; v.str = JatValueToString(l) + JatValueToString(r); return v; }
                JatValue v; v.type=JatValue::NUM; v.num=l.num+r.num; return v;
            }
            if (l.type!=JatValue::NUM || r.type!=JatValue::NUM) throw JatParseError(lineNo,0,"문자열/리스트에는 -,*,/ 연산을 사용할 수 없습니다");
            JatValue v; v.type=JatValue::NUM;
            if (e->op=='-') v.num=l.num-r.num;
            else if (e->op=='*') v.num=l.num*r.num;
            else { if (r.num==0) throw JatParseError(lineNo,0,"0으로 나눌 수 없습니다"); v.num=l.num/r.num; }
            return v;
        }
    }
    throw JatParseError(lineNo,0,"알 수 없는 표현식");
}
static void JatExecStatement(const JatStmtNode& stmt) {
    if (stmt.kind == JatStmtNode::CONSOLE) { Log(JatValueToString(JatEval(stmt.expr, stmt.line)), 1); }
    else { g_jatVars[stmt.varName] = JatEval(stmt.expr, stmt.line); }
}
static void JatRunLine(const std::string& lineText, int lineNo) {
    auto toks = JatTokenizeLine(lineText, lineNo);
    if (toks.size()==1 && toks[0].ty==JatToken::END) return;
    JatParser parser(toks, lineNo);
    JatExecStatement(parser.parseStatement());
}
static void JatRunScript(const fs::path& path) {
    std::ifstream f(path);
    if (!f) { Log("[ERROR] 스크립트를 열 수 없습니다: " + path.string(), 3); return; }
    std::string line; int lineNo=0, errCount=0;
    while (std::getline(f, line)) {
        lineNo++;
        try { JatRunLine(line, lineNo); }
        catch (std::exception& e) { Log(std::string("[ERROR] ") + e.what(), 3); errCount++; }
    }
    Log("완료: 스크립트 실행됨 -> " + path.string() + (errCount ? (" (" + std::to_string(errCount) + "개 오류 발생)") : " (오류 없음)"), errCount ? 3 : 1);
}
enum class JatTryResult { NotJat, Ok, Error };
static JatTryResult TryRunJatCode(const std::string& lineText, std::string& errMsgOut) {
    std::vector<JatToken> toks;
    try { toks = JatTokenizeLine(lineText, 1); }
    catch (std::exception& e) { errMsgOut = e.what(); return JatTryResult::Error; }
    if (toks.size()==1 && toks[0].ty==JatToken::END) return JatTryResult::NotJat;
    bool looksLikeJat = false;
    if (toks[0].ty==JatToken::IDENT && (toks[0].s=="Console" || toks[0].s=="list")) looksLikeJat = true;
    else if (toks[0].ty==JatToken::IDENT && toks.size()>1 && toks[1].ty==JatToken::ASSIGN) looksLikeJat = true;
    if (!looksLikeJat) return JatTryResult::NotJat;
    try { JatParser parser(toks, 1); JatExecStatement(parser.parseStatement()); return JatTryResult::Ok; }
    catch (std::exception& e) { errMsgOut = e.what(); return JatTryResult::Error; }
}

// ============================================================================
// [Jweb] Jweb("검색어")(옵션) - Google + Bing(Edge) 검색을 기본 브라우저로 오픈
// 옵션: video/image/news/read(전체,기본)/code/doc
// ============================================================================
static std::string UrlEncode(const std::string& s) {
    std::ostringstream out;
    for (unsigned char c : s) {
        if (isalnum(c) || c=='-'||c=='_'||c=='.'||c=='~') out << (char)c;
        else if (c==' ') out << '+';
        else out << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (int)c << std::nouppercase << std::dec;
    }
    return out.str();
}
static void PlatformOpenUrl(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    int rc = system(("xdg-open '" + url + "' >/dev/null 2>&1 &").c_str()); (void)rc;
#endif
}
static void HandleJwebLine(const std::string& line) {
    size_t p1 = line.find('(');
    if (p1 == std::string::npos) { Log("[ERROR] 사용법: Jweb(\"검색어\")(옵션)", 3); return; }
    int depth=0; size_t end1=std::string::npos; bool inQuote=false;
    for (size_t i=p1; i<line.size(); i++) {
        char c=line[i];
        if (c=='"' && (i==0||line[i-1]!='\\')) inQuote=!inQuote;
        if (inQuote) continue;
        if (c=='(') depth++; else if (c==')') { depth--; if (depth==0) { end1=i; break; } }
    }
    if (end1==std::string::npos) { Log("[ERROR] Jweb 괄호가 닫히지 않았습니다.", 3); return; }
    std::string query = Trim(line.substr(p1+1, end1-p1-1));
    if (query.size()>=2 && query.front()=='"' && query.back()=='"') query = query.substr(1, query.size()-2);
    if (query.empty()) { Log("[ERROR] 검색어가 비어 있습니다.", 3); return; }
    std::string option = "read";
    size_t p2 = line.find('(', end1+1);
    if (p2 != std::string::npos) {
        size_t end2 = line.find(')', p2);
        if (end2 != std::string::npos) {
            option = ToLower(Trim(line.substr(p2+1, end2-p2-1)));
            if (option.size()>=2 && option.front()=='"' && option.back()=='"') option = option.substr(1, option.size()-2);
        }
    }
    std::string enc = UrlEncode(query), gUrl, bUrl;
    if (option=="video") { gUrl="https://www.google.com/search?tbm=vid&q="+enc; bUrl="https://www.bing.com/videos/search?q="+enc; }
    else if (option=="image") { gUrl="https://www.google.com/search?tbm=isch&q="+enc; bUrl="https://www.bing.com/images/search?q="+enc; }
    else if (option=="news") { gUrl="https://www.google.com/search?tbm=nws&q="+enc; bUrl="https://www.bing.com/news/search?q="+enc; }
    else if (option=="code") { std::string q2=UrlEncode(query+" site:github.com OR site:stackoverflow.com"); gUrl="https://www.google.com/search?q="+q2; bUrl="https://www.bing.com/search?q="+q2; }
    else if (option=="doc") { std::string q2=UrlEncode(query+" filetype:pdf OR filetype:doc OR filetype:docx"); gUrl="https://www.google.com/search?q="+q2; bUrl="https://www.bing.com/search?q="+q2; }
    else { gUrl="https://www.google.com/search?q="+enc; bUrl="https://www.bing.com/search?q="+enc; }
    PlatformOpenUrl(gUrl); PlatformOpenUrl(bUrl);
    Log("완료: 브라우저에서 검색 결과를 열었습니다 (Google + Bing, 옵션: " + option + ")\nGoogle: " + gUrl + "\nBing: " + bUrl, 1);
}

// ============================================================================
// [Autowork] Autowork( 명령어... if(조건){...}else{...} ) - 순차/조건 자동화
// 본문의 명령어는 jat의 일반 괄호형 명령어(Create(...), Delete(...) 등) 그대로 사용.
// 조건식은 jat코드 변수/사칙연산 + 비교연산(==,!=,<,>,<=,>=) 및 Exists("경로") 지원.
// ============================================================================
static bool EvalAutoworkCondition(const std::string& condRaw) {
    std::string cond = Trim(condRaw);
    if (ToLower(cond).rfind("exists(",0)==0 && !cond.empty() && cond.back()==')') {
        std::string inner = Trim(cond.substr(7, cond.size()-8));
        if (inner.size()>=2 && inner.front()=='"' && inner.back()=='"') inner = inner.substr(1, inner.size()-2);
        std::error_code ec; return fs::exists(fs::u8path(inner), ec);
    }
    auto toks = JatTokenizeLine(cond, 1);
    int depth=0; size_t foundIdx=(size_t)-1; JatToken::Ty opTy=JatToken::END;
    for (size_t i=0;i<toks.size();i++) {
        if (toks[i].ty==JatToken::LP) depth++;
        else if (toks[i].ty==JatToken::RP) depth--;
        else if (depth==0 && (toks[i].ty==JatToken::EQ||toks[i].ty==JatToken::NE||toks[i].ty==JatToken::LT||toks[i].ty==JatToken::GT||toks[i].ty==JatToken::LE||toks[i].ty==JatToken::GE)) { foundIdx=i; opTy=toks[i].ty; break; }
    }
    if (foundIdx == (size_t)-1) {
        JatParser p(toks, 1); JatValue v = JatEval(p.parseExpr(), 1);
        if (v.type==JatValue::NUM) return v.num != 0;
        if (v.type==JatValue::STR) return !v.str.empty();
        return !v.list.empty();
    }
    std::vector<JatToken> leftToks(toks.begin(), toks.begin()+foundIdx); leftToks.push_back(JatToken{});
    std::vector<JatToken> rightToks(toks.begin()+foundIdx+1, toks.end());
    JatParser lp(leftToks, 1); auto leftExpr = lp.parseExpr();
    JatParser rp(rightToks, 1); auto rightExpr = rp.parseExpr();
    JatValue l = JatEval(leftExpr, 1), r = JatEval(rightExpr, 1);
    if (l.type==JatValue::NUM && r.type==JatValue::NUM) {
        switch (opTy) {
            case JatToken::EQ: return l.num==r.num; case JatToken::NE: return l.num!=r.num;
            case JatToken::LT: return l.num<r.num;  case JatToken::GT: return l.num>r.num;
            case JatToken::LE: return l.num<=r.num; case JatToken::GE: return l.num>=r.num;
            default: return false;
        }
    }
    std::string ls = JatValueToString(l), rs = JatValueToString(r);
    if (opTy==JatToken::EQ) return ls==rs;
    if (opTy==JatToken::NE) return ls!=rs;
    throw std::runtime_error("문자열 비교는 ==, != 만 지원합니다");
}
struct AutoworkParser {
    std::string src; size_t pos=0;
    explicit AutoworkParser(std::string s): src(std::move(s)) {}
    void skipWs() { while (pos<src.size() && isspace((unsigned char)src[pos])) pos++; }
    bool peekKeyword(const std::string& kw) {
        skipWs();
        if (src.compare(pos, kw.size(), kw)==0) {
            char after = pos+kw.size()<src.size() ? src[pos+kw.size()] : ' ';
            if (!isalnum((unsigned char)after) && after!='_') return true;
        }
        return false;
    }
    std::string readBalanced(char open, char close) {
        skipWs();
        if (pos>=src.size() || src[pos]!=open) throw std::runtime_error(std::string("'")+open+"' 문자가 필요합니다");
        int depth=0; size_t start=pos; bool inQuote=false;
        for (; pos<src.size(); pos++) {
            char c = src[pos];
            if (c=='"' && (pos==0||src[pos-1]!='\\')) inQuote=!inQuote;
            if (inQuote) continue;
            if (c==open) depth++;
            else if (c==close) { depth--; if (depth==0) { pos++; return src.substr(start+1, pos-start-2); } }
        }
        throw std::runtime_error(std::string("'")+close+"' 로 닫히지 않았습니다");
    }
    std::string readLine() {
        skipWs();
        size_t start = pos;
        while (pos<src.size() && src[pos] != '\n') pos++;
        std::string line = src.substr(start, pos-start);
        if (pos<src.size()) pos++;
        return Trim(line);
    }
    void runStatements() {
        while (true) {
            skipWs();
            if (pos >= src.size() || src[pos]=='}') break;
            if (peekKeyword("if")) {
                pos += 2;
                std::string cond = readBalanced('(', ')');
                skipWs();
                std::string thenBlock = readBalanced('{', '}');
                std::string elseBlock; size_t save = pos; skipWs();
                if (peekKeyword("else")) { pos += 4; skipWs(); elseBlock = readBalanced('{', '}'); }
                else pos = save;
                bool condVal = EvalAutoworkCondition(cond);
                AutoworkParser sub(condVal ? thenBlock : elseBlock);
                sub.runStatements();
            } else {
                std::string line = readLine();
                if (!line.empty()) ExecuteLine(line);
            }
        }
    }
};
static void HandleAutoworkLine(const std::string& line, bool dryRun) {
    size_t p1 = line.find('(');
    if (p1 == std::string::npos) { Log("[ERROR] 사용법: Autowork( 명령어... )", 3); return; }
    int depth=0; size_t endIdx=std::string::npos; bool inQuote=false;
    for (size_t i=p1; i<line.size(); i++) {
        char c = line[i];
        if (c=='"' && (i==0||line[i-1]!='\\')) inQuote=!inQuote;
        if (inQuote) continue;
        if (c=='(') depth++; else if (c==')') { depth--; if (depth==0) { endIdx=i; break; } }
    }
    if (endIdx==std::string::npos) { Log("[ERROR] Autowork 괄호가 닫히지 않았습니다.", 3); return; }
    std::string body = line.substr(p1+1, endIdx-p1-1);
    if (dryRun) { Log("[DRY-RUN] Autowork 블록 실행 예정 (" + std::to_string(std::count(body.begin(),body.end(),'\n')+1) + "줄)", 2); return; }
    try { AutoworkParser parser(body); parser.runStatements(); Log("완료: Autowork 실행됨", 1); }
    catch (std::exception& e) { Log(std::string("[ERROR] Autowork: ") + e.what(), 3); }
}

// ============================================================================
// [터미널 UX] help/version/clear/history
// ============================================================================
static void CmdHelp() {
    Log(
        "=== jat Terminal v3 명령어 목록 (Windows + Linux) ===\n"
        "[파일] Create Update Copy Move Rename Open Duplicate\n"
        "[삭제] Delete(Lv.1) Remove(Lv.2 30일) Erase(Lv.3 영구)\n"
        "[정보] Info Tree Size Find Search Replace Analyze DuplicateFinder EmptyFinder\n"
        "[자동화] Clean Compress Extract Watch Schedule Macro Batch Alias\n"
        "[시스템] Status Monitor Run Kill Service Startup Doctor Repair Optimize Benchmark\n"
        "[네트워크] Download Upload Ping Network IP DNS Port API\n"
        "[보안] Checksum HashCompare Encrypt Decrypt\n"
        "[강력한 도구] Diff Snapshot Backup Lock Unlock Holder Risk Estimate Explain\n"
        "[편의] Cd Path Timestamp Env Random Count Sort Merge WordCount\n"
        "[파일탐색] file find <이름> | file ext find <확장자>  (공백구분, 상하위 폴더 전체)\n"
        "[자동입력] Wait(ms) MouseMoveX(n) MouseMoveY(n) CenterMouse() MouseLeft() MouseRight() Keyboard(\"key\")\n"
        "[Lua] Lua(\"script.lua\",\"인자들\") - 프로세스 격리 실행, 동적 로딩 없음, 30초 타임아웃\n"
        "[웹검색] Jweb(\"검색어\")(옵션)  옵션: video/image/news/read/code/doc\n"
        "[자동화블록] Autowork( 명령어들... if(조건){...} else {...} )  - 여러 줄 입력 가능\n"
        "[UX] help() version() clear() history()\n"
        "[안전기호] ?명령어(...) = 미리보기 / $명령어(...) = 무음 실행\n"
        "[특수문법] ^파이썬코드^ = python(3) 실행 (설치 필요)\n"
        "[jat코드] Console(값) / 변수=식 / list 변수=[\"a\",\"b\"] / run script.jat 로 실행\n"
        "※ Compress/Extract 무압축 저장방식만 지원. Encrypt/Decrypt는 AES가 아닌 SHA-256 스트림암호.\n"
        "※ Download/Upload/API/Ping/Jweb은 시스템 curl 또는 브라우저를 사용합니다.\n"
        "※ MouseMove/Mouse/Keyboard는 Linux에서 X11 세션이 필요합니다 (libx11-dev, libxtst-dev).",
        1);
}
static void CmdVersion() { Log("jat Terminal v5.0.0 (Cross-Platform CLI: Windows + Linux, C++17)", 1); }
static void CmdClear() {
#ifdef _WIN32
    int rc = system("cls"); (void)rc;
#else
    int rc = system("clear"); (void)rc;
#endif
}
static void CmdHistoryShow() {
    if (g_history.empty()) { Log("기록 없음.", 2); return; }
    std::string out;
    for (size_t i = 0; i < g_history.size(); ++i) out += std::to_string(i+1) + ". " + g_history[i] + "\n";
    Log(out, 1);
}

// ----------------------------------------------------------------------------
// 명령어 디스패치
// ----------------------------------------------------------------------------
static void ExecuteParsed(const ParsedCommand& pc) {
    std::string nl = ToLower(pc.name);
    auto args = pc.args;
    try {
        if (nl=="create") CmdCreateUpdate(args, pc.dryRun, false);
        else if (nl=="update") CmdCreateUpdate(args, pc.dryRun, true);
        else if (nl=="copy") CmdCopy(args, pc.dryRun);
        else if (nl=="move") CmdMove(args, pc.dryRun);
        else if (nl=="rename") CmdRename(args, pc.dryRun);
        else if (nl=="open") CmdOpen(args, pc.dryRun);
        else if (nl=="duplicate") CmdDuplicate(args, pc.dryRun);
        else if (nl=="delete") CmdDelete(args, pc.dryRun);
        else if (nl=="remove") CmdRemove(args, pc.dryRun);
        else if (nl=="erase") CmdErase(args, pc.dryRun);
        else if (nl=="info") CmdInfo(args, pc.dryRun);
        else if (nl=="tree") CmdTree(args, pc.dryRun);
        else if (nl=="size") CmdSize(args, pc.dryRun);
        else if (nl=="find") CmdFind(args, pc.dryRun);
        else if (nl=="search") CmdSearch(args, pc.dryRun);
        else if (nl=="replace") CmdReplace(args, pc.dryRun);
        else if (nl=="analyze") CmdAnalyze(args, pc.dryRun);
        else if (nl=="duplicatefinder") CmdDuplicateFinder(args, pc.dryRun);
        else if (nl=="emptyfinder") CmdEmptyFinder(args, pc.dryRun);
        else if (nl=="clean") CmdClean(args, pc.dryRun);
        else if (nl=="compress") CmdCompress(args, pc.dryRun);
        else if (nl=="extract") CmdExtract(args, pc.dryRun);
        else if (nl=="watch") CmdWatch(args, pc.dryRun);
        else if (nl=="schedule") CmdSchedule(args, pc.dryRun);
        else if (nl=="macro") CmdMacro(args, pc.dryRun);
        else if (nl=="batch") CmdBatch(args, pc.dryRun);
        else if (nl=="alias") CmdAlias(args, pc.dryRun);
        else if (nl=="status") CmdStatus(args, pc.dryRun);
        else if (nl=="monitor") CmdMonitor(args, pc.dryRun);
        else if (nl=="run") CmdRun(args, pc.dryRun);
        else if (nl=="kill") CmdKill(args, pc.dryRun);
        else if (nl=="service") CmdService(args, pc.dryRun);
        else if (nl=="startup") CmdStartup(args, pc.dryRun);
        else if (nl=="doctor") CmdDoctor(args, pc.dryRun);
        else if (nl=="repair") CmdRepair(args, pc.dryRun);
        else if (nl=="optimize") CmdOptimize(args, pc.dryRun);
        else if (nl=="benchmark") CmdBenchmark(args, pc.dryRun);
        else if (nl=="download") CmdDownload(args, pc.dryRun);
        else if (nl=="upload") CmdUpload(args, pc.dryRun);
        else if (nl=="ping") CmdPing(args, pc.dryRun);
        else if (nl=="network") CmdNetwork(args, pc.dryRun);
        else if (nl=="ip") CmdIP(args, pc.dryRun);
        else if (nl=="dns") CmdDNS(args, pc.dryRun);
        else if (nl=="port") CmdPort(args, pc.dryRun);
        else if (nl=="api") CmdApi(args, pc.dryRun);
        else if (nl=="checksum") CmdChecksum(args, pc.dryRun);
        else if (nl=="hashcompare") CmdHashCompare(args, pc.dryRun);
        else if (nl=="encrypt") CmdEncrypt(args, pc.dryRun);
        else if (nl=="decrypt") CmdDecrypt(args, pc.dryRun);
        else if (nl=="diff") CmdDiff(args, pc.dryRun);
        else if (nl=="snapshot") CmdSnapshot(args, pc.dryRun);
        else if (nl=="backup") CmdBackup(args, pc.dryRun);
        else if (nl=="lock") CmdLock(args, pc.dryRun);
        else if (nl=="unlock") CmdUnlock(args, pc.dryRun);
        else if (nl=="holder") CmdHolder(args, pc.dryRun);
        else if (nl=="risk") CmdRisk(args, pc.dryRun);
        else if (nl=="estimate") CmdEstimate(args, pc.dryRun);
        else if (nl=="explain") CmdExplain(args, pc.dryRun);
        else if (nl=="cd") CmdCd(args, pc.dryRun);
        else if (nl=="path") CmdPath(args, pc.dryRun);
        else if (nl=="timestamp") CmdTimestamp(args, pc.dryRun);
        else if (nl=="env") CmdEnv(args, pc.dryRun);
        else if (nl=="random") CmdRandom(args, pc.dryRun);
        else if (nl=="count") CmdCount(args, pc.dryRun);
        else if (nl=="sort") CmdSort(args, pc.dryRun);
        else if (nl=="merge") CmdMerge(args, pc.dryRun);
        else if (nl=="wordcount") CmdWordCount(args, pc.dryRun);
        else if (nl=="wait") CmdWait(args, pc.dryRun);
        else if (nl=="mousemovex") CmdMouseMoveX(args, pc.dryRun);
        else if (nl=="mousemovey") CmdMouseMoveY(args, pc.dryRun);
        else if (nl=="mouse") CmdMouse(args, pc.dryRun);
        else if (nl=="mouseleft") CmdMouseLeft(args, pc.dryRun);
        else if (nl=="mouseright") CmdMouseRight(args, pc.dryRun);
        else if (nl=="centermouse") CmdCenterMouse(args, pc.dryRun);
        else if (nl=="keyboard") CmdKeyboard(args, pc.dryRun);
        else if (nl=="lua") CmdLua(args, pc.dryRun);
        else if (nl=="help") CmdHelp();
        else if (nl=="version") CmdVersion();
        else if (nl=="clear") CmdClear();
        else if (nl=="history") CmdHistoryShow();
        else {
            auto mit = g_macros.find(pc.name);
            if (mit != g_macros.end()) {
                if (pc.dryRun) Log("[DRY-RUN] 매크로 실행 예정: " + pc.name + " (" + std::to_string(mit->second.size()) + "단계)", 2);
                else for (auto& step : mit->second) ExecuteLine(step);
            } else {
                std::string jatErr;
                auto res = TryRunJatCode(pc.raw, jatErr);
                if (res == JatTryResult::Ok) { /* jat코드로 처리됨 */ }
                else if (res == JatTryResult::Error) Log("[ERROR] " + jatErr, 3);
                else Log("[ERROR] 알 수 없는 명령어: " + pc.name, 3);
            }
        }
    } catch (std::exception& e) {
        Log(std::string("[ERROR] ") + e.what(), 3);
    }
}

static void ExecuteLine(const std::string& raw) {
    std::string line = Trim(raw);
    if (line.empty()) return;
    if (line.rfind("//", 0) == 0) return; // 주석 전용 줄은 무시

    // "file find <이름>" / "file ext find <확장자>" - 공백 구분 특수 명령
    std::string lowerLine = ToLower(line);
    if (lowerLine.rfind("file ", 0) == 0) { Log("jat> " + line, 1); HandleFileCommand(line); return; }

    // "run script.jat" - jat코드 스크립트 파일 실행 (괄호형 Run("프로그램")과는 별개)
    if (lowerLine.rfind("run ", 0) == 0) {
        std::string scriptPath = Trim(line.substr(4));
        if (!scriptPath.empty()) { Log("jat> " + line, 1); JatRunScript(fs::u8path(scriptPath)); return; }
    }

    // ^(코드)^ 파이썬 실행 특수 문법
    if (line.size() >= 2 && line.front() == '^' && line.back() == '^') {
        Log("jat> " + line, 1);
        RunPythonCode(line.substr(1, line.size() - 2));
        return;
    }

    // ?/$ 접두어를 뗀 뒤 Jweb( / Autowork( 특수 문법 확인
    {
        std::string chk = line; bool dry=false, silent=false;
        while (!chk.empty() && (chk[0]=='?' || chk[0]=='$')) { if (chk[0]=='?') dry=true; else silent=true; chk = Trim(chk.substr(1)); }
        std::string chkLower = ToLower(chk);
        if (chkLower.rfind("jweb(", 0) == 0) {
            if (!silent) Log("jat> " + line, 1);
            if (dry) Log("[DRY-RUN] 웹 검색 예정", 2); else HandleJwebLine(chk);
            return;
        }
        if (chkLower.rfind("autowork(", 0) == 0) {
            if (!silent) Log("jat> " + line, 1);
            HandleAutoworkLine(chk, dry);
            return;
        }
    }

    ParsedCommand pc = ParseCommand(line);
    pc.raw = line;
    if (!pc.silent) Log("jat> " + line, 1);
    if (!pc.valid) {
        std::string jatErr;
        auto res = TryRunJatCode(line, jatErr);
        if (res == JatTryResult::Ok) return;
        if (res == JatTryResult::Error) { Log("[ERROR] " + jatErr, 3); return; }
        Log("[ERROR] " + pc.error, 3);
        return;
    }

    auto eit = g_explain.find(ToLower(pc.name));
    if (eit != g_explain.end()) Log("[Explain] " + eit->second, 2);

    ExecuteParsed(pc);
}

// ----------------------------------------------------------------------------
// 진입점 (REPL 루프)
// ----------------------------------------------------------------------------
static void PrintBanner() {
    std::cout << C_GREEN <<
        "╔══════════════════════════════════════════╗\n"
        "║              jat Terminal v4               ║\n"
        "╚══════════════════════════════════════════╝\n" << C_RESET;
}

// 괄호/중괄호 균형 계산 (Autowork 등 여러 줄 블록 입력을 감지하기 위함)
static int ParenBraceBalance(const std::string& s) {
    int bal = 0; bool inQuote = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '"' && (i == 0 || s[i-1] != '\\')) inQuote = !inQuote;
        if (inQuote) continue;
        if (c == '(' || c == '{') bal++;
        else if (c == ')' || c == '}') bal--;
    }
    return bal;
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode)) SetConsoleMode(hOut, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/);
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif
    PurgeOldQuarantine();
    PrintBanner();
    Log("jat Terminal v4.0.0 - 준비 완료. help() 를 입력하세요.", 1);

    std::string line;
    while (true) {
        std::cout << C_GREEN << "jat> " << C_RESET << std::flush;
        if (!std::getline(std::cin, line)) break;
        std::string buffer = line;
        int bal = ParenBraceBalance(buffer);
        int guard = 0;
        while (bal > 0 && guard < 2000) {
            std::cout << C_GRAY << "...> " << C_RESET << std::flush;
            std::string more;
            if (!std::getline(std::cin, more)) break;
            buffer += "\n" + more;
            bal += ParenBraceBalance(more);
            guard++;
        }
        std::string trimmed = Trim(buffer);
        if (trimmed.empty()) continue;
        g_history.push_back(trimmed);
        ExecuteLine(trimmed);
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
