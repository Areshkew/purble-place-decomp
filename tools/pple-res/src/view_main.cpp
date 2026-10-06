// pple-view entry point. See viewer/README.md for responsibility and state ownership.
#define SDL_MAIN_HANDLED
#include "viewer/app.hpp"

int main(int argc, char** argv) {
    pple::viewer::App app;
    return app.run(argc, argv);
}
