#include <windows.h>

#include "openrcv/ui/Application.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    return openrcv::ui::RunApplication(instance, showCommand);
}
