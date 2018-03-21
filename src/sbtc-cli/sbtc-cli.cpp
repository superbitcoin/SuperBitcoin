#include "baseimpl.hpp"

CApp gApp;

appbase::IBaseApp *GetApp()
{
    return &gApp;
}

int main(int argc, char *argv[])
{
    gApp.Initialize(argc, argv) && gApp.Startup() && gApp.Run(argc, argv);
    gApp.Shutdown();

    return 0;
}
