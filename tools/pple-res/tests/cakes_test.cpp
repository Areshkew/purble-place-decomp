#include "pple/cakes.hpp"
#include "pple/scene.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
std::vector<std::uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }
void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}

int main() {
    pple::CakeModel model;
    std::string vars = "<Root>";
    const char* names[] = {"Beginner", "Intermediate", "Expert"};
    for (int d = 0; d < 3; ++d) {
        const std::string prefix = names[d];
        for (const auto& [key, value] : std::vector<std::pair<std::string, int>>{
                 {"MinLayers", d + 1}, {"MaxLayers", d + 1},
                 {"MinToppings", 2}, {"MaxToppings", d == 2 ? 4 : 2}})
            vars += "<" + prefix + key + ">" + std::to_string(value) + "</" + prefix + key + ">";
    }
    vars += "</Root>";
    std::string layers = "<Root>";
    for (int i = 0; i < 18; ++i)
        layers += "<Layer><Delta>-" + std::to_string(i + 1) + "</Delta><Sprite>fixture.xml</Sprite></Layer>";
    layers += "</Root>";
    std::string error;
    require(model.load_vars(bytes(vars), error), "load variables");
    require(model.load_layers(bytes(layers), error), "load layer definitions");
    for (int d = 0; d < 3; ++d) {
        for (std::uint32_t seed = 1; seed <= 1000; ++seed) {
            std::uint32_t state = seed;
            const auto rng = [&state] { state = state * 1664525u + 1013904223u; return int(state >> 1); };
            const auto target = model.generate_correct(d, rng);
            pple::Cake built;
            for (const auto& move : target.moves) {
                require(d != 0 || move.station != 3, "Beginner never needs filling");
                require(d == 2 || move.station != 4, "only Advanced needs final touch");
                pple::CakeLayer added;
                model.use_tool(built, move.station, move.index, added, rng);
            }
            require(model.compare(built, target) == pple::CakeCompare::Exact, "target is buildable");
            int height = 0;
            for (const auto& layer : target.layers) height += model.layer(layer.index)->delta;
            require(height == target.height_delta, "generated height counted exactly once");
            if (d < 2) require(target.layers.size() == std::size_t(d == 0 ? 3 : 5), "difficulty layer count");
        }
    }
    // Resource resolution must not discard the named widget owner, or filtering/placement
    // silently applies to no machines despite the scene having the correct widget path.
    pple::Scene scene;
    const auto fetch = [](const std::string& name, std::vector<std::uint8_t>& out) {
        out = bytes(name == "tool.xml"
            ? "<Root><ButtonTool Name=\"CakePanRoundButton\"><Desc><Type>3</Type><Button>button.xml</Button></Desc></ButtonTool></Root>"
            : "<Root Type=\"Button\"><Animation><Texture>sheet.png</Texture><SourceWidth>10</SourceWidth><SourceHeight>10</SourceHeight></Animation></Root>");
        return true;
    };
    require(pple::scene_load(bytes("<Root><Tools><Tool Name=\"CakePanTool\"><X>75</X><Y>298</Y><Desc>tool.xml</Desc></Tool></Tools></Root>"), fetch, scene, error), "load widget fixture");
    require(scene.items.size() == 1, "one fixture button");
    require(scene.items[0].widget_name == "CakePanTool", "named owner survives resolution");
    require(scene.items[0].widget_path == "Tool/", "widget path preserved");
    require(scene.items[0].tool_type == 3, "button type preserved");
    std::puts("PASS: 3000 generated cakes and named widget ownership");
}
