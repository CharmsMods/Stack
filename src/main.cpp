#include "App/AppShell.h"
#include <iostream>

int main(int argc, char** argv) {
    AppShell app;
    
    std::cout << "Starting Modular Studio: Stack..." << std::endl;

    if (!app.Initialize("Modular Studio Stack", 1280, 800)) {
        std::cerr << "Failed to initialize Application Shell!" << std::endl;
        return -1;
    }

    app.Run();
    app.Shutdown();

    return 0;
}
