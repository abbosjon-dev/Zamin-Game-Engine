#pragma once

#include "Zamin/Core/Application.h"

// Include this header exactly once (in the client's main translation unit)
// and implement Zamin::CreateApplication().
int main(int argc, char** argv)
{
    Zamin::Application* app = Zamin::CreateApplication({ argc, argv });
    app->Run();
    delete app;
    return 0;
}
