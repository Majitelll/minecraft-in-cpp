#include "app.h"
#include "log.h"
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>

#if defined(_DEBUG) || !defined(NDEBUG)
  #define DEBUG_BUILD 1
#endif

#ifdef DEBUG_BUILD
static std::ofstream gLog("debug.log");
#endif

void logMsg(const std::string& msg) {
#ifdef DEBUG_BUILD
    gLog << msg << "\n";
    gLog.flush();
    std::cerr << msg << "\n";
#else
    (void)msg;
#endif
}

static int run() {
    logMsg("main() started");
    try {
        logMsg("creating App...");
        App app;
        logMsg("calling app.run()...");
        app.run();
        logMsg("app.run() returned normally");
    } catch (const std::exception& e) {
        std::string msg = std::string("Fatal: ") + e.what() + "\n";
        std::cerr << msg;
        std::ofstream err("error.log");
        if (!err.is_open()) err.open("/tmp/minecraft-error.log");
        if (err.is_open()) err << msg;
        logMsg(msg);
        return EXIT_FAILURE;
    } catch (...) {
        const char* msg = "Fatal: unknown exception\n";
        std::cerr << msg;
        std::ofstream err("error.log");
        if (!err.is_open()) err.open("/tmp/minecraft-error.log");
        if (err.is_open()) err << msg;
        logMsg(msg);
        return EXIT_FAILURE;
    }
    logMsg("clean exit");
    return EXIT_SUCCESS;
}

int main() {
    return run();
}

// Windows Release builds use WINDOWS subsystem which requires WinMain
#if defined(_WIN32) && !defined(_DEBUG) && !defined(__EMSCRIPTEN__)
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return run();
}
#endif
