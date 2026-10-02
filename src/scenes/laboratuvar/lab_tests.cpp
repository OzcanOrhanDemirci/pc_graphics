// Scene 4 "Laboratuvar": the experiments' measurements (--scene laboratuvar --lab-test [name]), each with its
// criterion: PASS / FAIL.
#include "scenes/laboratuvar/laboratuvar_app.h"

#include <cstdio>
#include <string>

int runLabTests(LaboratuvarApp& app, const std::string& only) {
    (void)app;
    std::printf("laboratuvar deney testleri: henuz yok (%s)\n", only.c_str());
    return 0;
}
