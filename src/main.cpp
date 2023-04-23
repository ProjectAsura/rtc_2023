
#include <App.h>

int main(int argc, char** argv)
{
    RenderDesc desc = {};
    desc.RenderTimeSec      = 10.0 * 59.9;
    desc.Width              = 2560;
    desc.Height             = 1440;
    desc.FPS                = 60.0;
    desc.AnimationTimeSec   = 10.0;

    App(desc).Run();

    return 0;
}

