// Scene 4 "Laboratuvar" inside the application (stub while the experiments are built; the full live scene follows).
#include "app/lang.h"
#include "app/scene.h"
#include "scenes/laboratuvar/laboratuvar_app.h"

#include <memory>

namespace laboratuvar {

app::SceneInfo info() {
    app::SceneInfo i;
    i.id = "laboratuvar";
    i.number = "04";
    i.title = app::L("Laboratuvar", "Laboratory");
    i.subtitle = app::L("Sıvı ve tanecik fiziği, ağır çekim", "Liquids and particles in slow motion");
    i.description = app::L("Yapım aşamasında.", "Under construction.");
    i.image = "assets/gallery/laboratuvar.jpg";
    i.accent = 0x6FC2B4;
    return i;
}
std::unique_ptr<app::Scene> make() { return nullptr; }
int cli(int argc, char** argv) { return runLaboratuvarCli(argc, argv); }

}   // namespace laboratuvar
