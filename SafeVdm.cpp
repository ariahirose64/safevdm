#include <windows.h>
#include <stdint.h>
#include <wchar.h>
// all this is is an incomplete shim to foward msdos and win16 apps to emulators that specialize
// in running these, theoretically you could make one that if all fails it just runs the app normally

#define CONFIG_FILE L"launcher.cfg"

typedef struct {
    wchar_t dos[MAX_PATH];
    wchar_t win16[MAX_PATH];
} Config;

// apparently i tha holes at redmond banned the entire cpp lang and did the blow and hookers thing
// this isnt cpp anymore holy shit.
void safe_wcopy(wchar_t* dst, const wchar_t* src, size_t dstsize) {
    size_t i = 0;
    while (src[i] != 0 && i + 1 < dstsize) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

// Read config entirely in memory with elchapo lettus involved
int read_config(Config* cfg) {
    HANDLE hFile = CreateFileW(CONFIG_FILE, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return 0;

    char buffer[1024] = { 0 };
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
        CloseHandle(hFile);
        return 0;
    }
    CloseHandle(hFile);
    buffer[bytesRead] = 0;

    // Parse line by line and convert ASCII to wide strings
    size_t i = 0;
    while (i < bytesRead) {
        char* line = &buffer[i];
        size_t j = 0;
        while (i + j < bytesRead && buffer[i + j] != '\n' && buffer[i + j] != '\r') j++;

        char tmp = buffer[i + j];
        buffer[i + j] = 0;

        // Parse DOS=
        if (line[0] == 'D' && line[1] == 'O' && line[2] == 'S' && line[3] == '=') {
            size_t k = 0;
            while (line[4 + k] != 0 && k + 1 < MAX_PATH) {
                cfg->dos[k] = (wchar_t)line[4 + k];
                k++;
            }
            cfg->dos[k] = 0;
        }
        // Parse WIN16=
        else if (line[0] == 'W' && line[1] == 'I' && line[2] == 'N' && line[3] == '1' && line[4] == '6' && line[5] == '=') {
            size_t k = 0;
            while (line[6 + k] != 0 && k + 1 < MAX_PATH) {
                cfg->win16[k] = (wchar_t)line[6 + k];
                k++;
            }
            cfg->win16[k] = 0;
        }

        buffer[i + j] = tmp;
        i += j;
        while (i < bytesRead && (buffer[i] == '\n' || buffer[i] == '\r')) i++;
    }

    return 1;
}

// Detect DOS (0) or Win16 NE (1) or unknown (2)
int detect_app_type(const wchar_t* path) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return 2;

    uint8_t mz[2] = { 0 };
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, mz, 2, &bytesRead, NULL) || bytesRead != 2) {
        CloseHandle(hFile);
        return 2;
    }

    if (mz[0] != 'M' || mz[1] != 'Z') { CloseHandle(hFile); return 2; }

    // Read e_lfanew (offset to NE/PE header) at 0x3C
    DWORD off = 0;
    SetFilePointer(hFile, 0x3C, NULL, FILE_BEGIN);
    if (ReadFile(hFile, &off, sizeof(off), &bytesRead, NULL) && bytesRead == sizeof(off) && off > 0) {
        // Seek to signature at offset
        SetFilePointer(hFile, off, NULL, FILE_BEGIN);
        uint16_t sig = 0;
        if (ReadFile(hFile, &sig, sizeof(sig), &bytesRead, NULL) && bytesRead == sizeof(sig)) {
            if (sig == 0x454E) { // 'NE'
                CloseHandle(hFile);
                return 1; // Win16
            }
        }
    }

    CloseHandle(hFile);
    return 0; // All other MZ = DOS
}

// Launch program using CreateProcessW safely
    void launch_prog(const wchar_t* templateLine, const wchar_t* inputExe) {
    const size_t CMD_SLOTS = 4096;
    wchar_t cmd[CMD_SLOTS];
    for (size_t i = 0; i < CMD_SLOTS; ++i) cmd[i] = 0;

    // quoted inputExe: "C:\path\to\exe"
    wchar_t quoted[MAX_PATH * 2] = { 0 };
    safe_wcopy(quoted, L"\"", sizeof(quoted) / sizeof(quoted[0]));
    safe_wcopy(quoted + 1, inputExe, (sizeof(quoted) / sizeof(quoted[0])) - 1);
    size_t qlen = wcslen(quoted);
    if (qlen + 1 < sizeof(quoted) / sizeof(quoted[0])) quoted[qlen] = L'"', quoted[qlen + 1] = 0;

    if (templateLine && templateLine[0] != 0) {
        const wchar_t* ph = wcsstr(templateLine, L"%s");
        if (ph) {
            // copy prefix
            size_t prefixLen = (size_t)(ph - templateLine);
            if (prefixLen >= CMD_SLOTS) prefixLen = CMD_SLOTS - 1;
            wcsncpy_s(cmd, CMD_SLOTS, templateLine, prefixLen);

            // append quoted inputExe
            size_t cur = wcslen(cmd);
            if (cur < CMD_SLOTS) {
                wcsncpy_s(cmd + cur, CMD_SLOTS - cur, quoted, _TRUNCATE);
            }

            // append suffix after %s
            const wchar_t* suffix = ph + 2;
            cur = wcslen(cmd);
            if (cur < CMD_SLOTS && suffix && *suffix) {
                wcsncpy_s(cmd + cur, CMD_SLOTS - cur, suffix, _TRUNCATE);
            }
        } else {
            // no placeholder: copy template then append space + quoted inputExe
            wcsncpy_s(cmd, CMD_SLOTS, templateLine, _TRUNCATE);
            size_t cur = wcslen(cmd);
            if (cur + 1 < CMD_SLOTS && (cur == 0 || cmd[cur - 1] != L' ')) {
                cmd[cur] = L' ';
                cmd[cur + 1] = 0;
                cur++;
            }
            if (cur < CMD_SLOTS) wcsncpy_s(cmd + cur, CMD_SLOTS - cur, quoted, _TRUNCATE);
        }
    } else {
        // template empty: just quoted input exe
        wcsncpy_s(cmd, CMD_SLOTS, quoted, _TRUNCATE);
    }

    // Ensure first token (launcher) is quoted so CreateProcessW(NULL, ...) resolves it correctly
    if (cmd[0] != L'"') {
        wchar_t* sp = wcschr(cmd, L' ');
        wchar_t tmp[CMD_SLOTS];
        for (size_t i = 0; i < CMD_SLOTS; ++i) tmp[i] = 0;
        if (sp) {
            size_t firstLen = (size_t)(sp - cmd);
            tmp[0] = L'"';
            if (firstLen > 0) wcsncpy_s(tmp + 1, CMD_SLOTS - 1, cmd, firstLen);
            size_t pos = 1 + firstLen;
            if (pos < CMD_SLOTS) tmp[pos] = L'"', tmp[pos + 1] = 0;
            // append remainder
            safe_wcopy(tmp + pos + 1, sp, (CMD_SLOTS - (pos + 1)));
        } else {
            // single token - quote whole thing
            tmp[0] = L'"';
            safe_wcopy(tmp + 1, cmd, (CMD_SLOTS - 1));
            size_t pos = wcslen(tmp);
            if (pos + 1 < CMD_SLOTS) tmp[pos] = L'"', tmp[pos + 1] = 0;
        }
        safe_wcopy(cmd, tmp, CMD_SLOTS);
    }

    // Working directory = folder of inputExe (helps many launchers)
    wchar_t curDir[MAX_PATH] = { 0 };
    const wchar_t* lastSlash = wcsrchr(inputExe, L'\\');
    if (lastSlash) {
        size_t len = (size_t)(lastSlash - inputExe);
        if (len < MAX_PATH) {
            wcsncpy_s(curDir, MAX_PATH, inputExe, len);
            curDir[len] = 0;
        }
    }

    // Debug: show constructed command and working dir
    wprintf(L"Constructed command: %ls\n", cmd);
    if (curDir[0]) wprintf(L"Working directory: %ls\n", curDir);

    // Launch
    STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL,
                        (curDir[0] != 0) ? curDir : NULL, &si, &pi)) {
        wprintf(L"CreateProcess failed: %lu\n", GetLastError());
        return;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        wprintf(L"Usage: %s <exe>\n", argv[0]);
        return 1;
    }

    Config cfg = { 0 };
    if (!read_config(&cfg)) {
        wprintf(L"Failed to read config file.\n");
        return 1;
    }

    int t = detect_app_type(argv[1]);
    if (t == 0) {
        wprintf(L"DOS app detected.\n");
        launch_prog(cfg.dos, argv[1]);
    }
    else if (t == 1) {
        wprintf(L"Win16 app detected.\n");
        launch_prog(cfg.win16, argv[1]);
    }
    else {
        wprintf(L"Unknown executable type.\n");
    }

    return 0;
}

// overall i litteraly could have done this in gcc and not had the ghost of bill gates tell me
// im doing what i have done for 20 years all wrong reqrite it in rust you cunt!