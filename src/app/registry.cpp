// The scenes of the application, in gallery order. A new scene: a folder under src/scenes with info(), make() and
// (optionally) cli() in its namespace, and one line here.
#include "app/scene.h"

namespace altinsaat {
app::SceneInfo info();
std::unique_ptr<app::Scene> make();
int cli(int argc, char** argv);
}
namespace fener {
app::SceneInfo info();
std::unique_ptr<app::Scene> make();
int cli(int argc, char** argv);
}
namespace yikim {
app::SceneInfo info();
std::unique_ptr<app::Scene> make();
int cli(int argc, char** argv);
}
namespace tanecik {
app::SceneInfo info();
std::unique_ptr<app::Scene> make();
int cli(int argc, char** argv);
}

namespace app {

const std::vector<Entry>& registry() {
    static const std::vector<Entry> scenes = {
        {altinsaat::info, altinsaat::make, altinsaat::cli},
        {fener::info, fener::make, fener::cli},
        {yikim::info, yikim::make, yikim::cli},
        {tanecik::info, tanecik::make, tanecik::cli},
    };
    return scenes;
}

}   // namespace app
