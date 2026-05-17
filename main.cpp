#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "src/ui/console_ui.hpp"
#include <iostream>
#include <exception>

int main() {
    try {
        PhotoManager::UI::ConsoleUI app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "\nFatal error: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "\nUnknown fatal error.\n";
        return 2;
    }
    return 0;
}