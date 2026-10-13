// pple-view entry point. See viewer/README.md for responsibility and state ownership.
#define SDL_MAIN_HANDLED
#include "viewer/app.hpp"

int main(int argc, char** argv) {
#ifdef __EMSCRIPTEN__
    // The browser build hands the frame loop to the page: emscripten_set_main_loop unwinds this
    // stack instead of returning through it, so the session has to outlive main() rather than be
    // a local that the unwind would destroy.
    static pple::viewer::App app;
#else
    pple::viewer::App app;
#endif
    return app.run(argc, argv);
}
