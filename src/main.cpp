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
        std::ofstream err("error.log");
        err << "Fatal: " << e.what() << "\n";
        logMsg(std::string("Fatal: ") + e.what());
        return EXIT_FAILURE;
    } catch (...) {
        std::ofstream err("error.log");
        err << "Fatal: unknown exception\n";
        logMsg("Fatal: unknown exception");
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
