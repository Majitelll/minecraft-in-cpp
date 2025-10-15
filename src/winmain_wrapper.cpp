#ifdef _WIN32
#include <windows.h>

int main(); // forward declaration

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main();
}
#endif