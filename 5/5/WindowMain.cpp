#include "Application.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, PSTR, int)
{
    AllocConsole();
    FILE* dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);

    Application app(hInstance);
    if (!app.Initialize()) { return 0; }

    return app.Run();
}