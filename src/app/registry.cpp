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
namespace laboratuvar {
app::SceneInfo info();
std::unique_ptr<app::Scene> make();
int cli(int argc, char** argv);
}
namespace koy {
app::SceneInfo info();
std::unique_ptr<app::Scene> make();
int cli(int argc, char** argv);
}
namespace heykel {
app::SceneInfo info();
std::unique_ptr<app::Scene> make();
int cli(int argc, char** argv);
}
namespace olcek {
app::SceneInfo info();
std::unique_ptr<app::Scene> make();
int cli(int argc, char** argv);
}
namespace denizalti {
app::SceneInfo info();
std::unique_ptr<app::Scene> make();
int cli(int argc, char** argv);
}
namespace ayna {
app::SceneInfo info();
std::unique_ptr<app::Scene> make();
int cli(int argc, char** argv);
}
namespace kuyu {
app::SceneInfo info();
std::unique_ptr<app::Scene> make();
int cli(int argc, char** argv);
}
namespace orman {
app::SceneInfo info();
std::unique_ptr<app::Scene> make();
int cli(int argc, char** argv);
}
namespace ates {
app::SceneInfo info();
std::unique_ptr<app::Scene> make();
int cli(int argc, char** argv);
}
namespace dag {
app::SceneInfo info();
std::unique_ptr<app::Scene> make();
int cli(int argc, char** argv);
}
namespace ruya {
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
        {laboratuvar::info, laboratuvar::make, laboratuvar::cli},
        {koy::info, koy::make, koy::cli},
        {heykel::info, heykel::make, heykel::cli},
        {olcek::info, olcek::make, olcek::cli},
        {denizalti::info, denizalti::make, denizalti::cli},
        {ayna::info, ayna::make, ayna::cli},
        {kuyu::info, kuyu::make, kuyu::cli},
        {orman::info, orman::make, orman::cli},
        {ates::info, ates::make, ates::cli},
        {dag::info, dag::make, dag::cli},
        {ruya::info, ruya::make, ruya::cli},
    };
    return scenes;
}

}   // namespace app
