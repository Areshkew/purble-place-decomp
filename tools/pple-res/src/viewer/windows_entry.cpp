// Windows GUI bootstrap. Resource/game lifetime remains owned by main's App.
// Inherited redirected output is preserved for captures and regression scripts.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv);

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    bool console = false;
    for (int i = 1; i < __argc; ++i)
        if (std::strcmp(__argv[i], "--console") == 0) console = true;

    const bool have_out = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN;
    const bool have_err = GetFileType(GetStdHandle(STD_ERROR_HANDLE)) != FILE_TYPE_UNKNOWN;
    if (console && !GetConsoleWindow()) {
        if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
            if (!have_out) std::freopen("CONOUT$", "w", stdout);
            if (!have_err) std::freopen("CONOUT$", "w", stderr);
            std::freopen("CONIN$", "r", stdin);
        }
    }
    const int result = main(__argc, __argv);
    if (result != 0 && !console && !have_err)
        MessageBoxA(nullptr,
                    "The game could not start. Check the game resource files.\n"
                    "Run with --console to see the error details.",
                    "Purble Place", MB_OK | MB_ICONERROR);
    return result;
}
