#include "Utils/ScopedComApartment.h"

#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void TestApartmentLifetime() {
    APTTYPE type;
    APTTYPEQUALIFIER qualifier;
    Require(CoGetApartmentType(&type, &qualifier) == CO_E_NOTINITIALIZED,
        "A new dialog thread should start without COM initialization");
    {
        Stack::Win32::ScopedComApartment apartment;
        Require(apartment.Result() == S_OK,
            "Dialogs must initialize COM without an optional titlebar");
        Require(SUCCEEDED(CoGetApartmentType(&type, &qualifier)) &&
                (type == APTTYPE_STA || type == APTTYPE_MAINSTA),
            "Shell dialogs need a single-threaded apartment");
        {
            Stack::Win32::ScopedComApartment nested;
            Require(nested.Result() == S_FALSE,
                "An already initialized UI thread must remain usable");
        }
        Require(SUCCEEDED(CoGetApartmentType(&type, &qualifier)),
            "Closing a nested dialog must retain its caller's apartment");
        {
            Stack::Win32::ScopedComApartment wrongModel(COINIT_MULTITHREADED);
            Require(wrongModel.Result() == RPC_E_CHANGED_MODE && !wrongModel,
                "Incompatible apartment requests must report failure");
        }
        Require(SUCCEEDED(CoGetApartmentType(&type, &qualifier)),
            "Failed initialization must not release the caller's COM reference");
    }
    Require(CoGetApartmentType(&type, &qualifier) == CO_E_NOTINITIALIZED,
        "Closing the final dialog scope must balance COM initialization");
}

} // namespace

int main() {
    std::packaged_task<void()> test(TestApartmentLifetime);
    auto result = test.get_future();
    std::thread thread(std::move(test));
    thread.join();
    try {
        result.get();
        std::cout << "COM dialog apartment lifecycle tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
