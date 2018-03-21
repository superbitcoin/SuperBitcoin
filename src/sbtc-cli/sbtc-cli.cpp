#include "baseimpl.hpp"

CApp gApp;

appbase::IBaseApp *GetApp()
{
    return &gApp;
}

int main(int argc, char **argv)
{
    gApp.RelayoutArgs(argc, argv);
    gApp.Initialize(argc, argv) && gApp.Startup() && gApp.Run();
    gApp.Shutdown();

    return 0;
}
