#include <cstdlib>

#include "engine/core/App.hpp"

int main()
{
    engine::core::App app;
    return app.Run() ? EXIT_SUCCESS : EXIT_FAILURE;
}
