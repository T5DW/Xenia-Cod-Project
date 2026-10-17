#include "features.h"
#include "dvar.h"

#include <cstdio>

void Features::Init()
{
    Dvar::Init();
}

bool Features::Ready()
{
    return Dvar::Ready();
}

void Features::SetTimescale(float scale)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", scale);
    Dvar::SetString("timescale", buffer);
}

void Features::SetDeveloper(bool enabled)
{
    Dvar::SetString("developer", enabled ? "1" : "0");
}
