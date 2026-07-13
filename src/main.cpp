#include "runtime/Application.h"

int main(int argc, char* argv[]) {
    noix::runtime::Application app(argc, argv);
    return app.run();
}
