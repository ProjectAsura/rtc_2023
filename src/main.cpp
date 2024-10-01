#include <rtcApp.h>
#include <mimalloc-new-delete.h>

int main(int argc, char** argv)
{
    rtc::Config config = {};
    config.Width      = 1920;
    config.Height     = 1080;
    config.AnimFPS    = 60.0;
    config.RenderTime = 255.9;

    rtc::App().Run(config);

    return 0;
}

