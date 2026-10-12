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
    std::string glazes = "<Root>";
    for (int i = 0; i < 3; ++i)
        glazes += "<Layer><Delta>-16</Delta><Sprite>glaze" + std::to_string(i) + ".xml</Sprite></Layer>";
    glazes += "</Root>";
    require(model.load_glazes(bytes(glazes), error), "load glaze definitions");
    for (int kind : {13, 14}) {
        pple::Cake cake;
        pple::CakeLayer added;
        const auto rng = [] { return 0; };
        model.use_tool(cake, 1, 3, added, rng);
        model.use_tool(cake, 0, 0, added, rng);
        model.use_tool(cake, 5, 15, added, rng);
        model.use_tool(cake, 2, 6, added, rng);
        require(model.use_tool(cake, 4, kind, added, rng) != pple::CakeUse::Refused,
                "last machine works after decoration");
        require(cake.layers.back().index == 6, "final touch preserves visible decoration");
        require(cake.layers[cake.layers.size() - 2].index == kind, "finish below decoration");
        if (kind == 13) {
            require(cake.layers[1].glaze_icing == 15, "glaze remembers icing colour");
            require(model.layer(cake.layers[1])->sprite == "glaze0.xml", "glaze uses separate artwork");
            auto wrong_colour = cake;
            wrong_colour.layers[1].glaze_icing = 16;
            require(model.compare(cake, wrong_colour) != pple::CakeCompare::Exact,
                    "glazed icing colour matters to judging");
        }
        const auto size = cake.layers.size();
        model.use_tool(cake, 4, kind, added, rng);
        require(cake.layers.size() == size, "repeated final touch does not duplicate finish");
    }
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
            for (const auto& layer : target.layers) height += model.layer(layer)->delta_for(layer.shape);
            require(height == target.height_delta, "generated height counted exactly once");
            if (d < 2) require(target.layers.size() == std::size_t(d == 0 ? 3 : 5), "difficulty layer count");
        }
    }
    // A shaped definition keeps all three deltas. Round/rectangle are 4px taller
    // than heart; replacement must subtract the removed layer's own shape too.
    std::string shaped = "<Root>";
    for (int i = 0; i < 18; ++i) {
        if (i < 3) shaped += "<Layer><Shape><Delta>-23</Delta><Sprite>round.xml</Sprite></Shape>"
                            "<Shape><Delta>-23</Delta><Sprite>rectangle.xml</Sprite></Shape>"
                            "<Shape><Delta>-19</Delta><Sprite>heart.xml</Sprite></Shape></Layer>";
        else shaped += "<Layer><Delta>-4</Delta><Sprite>fixture.xml</Sprite></Layer>";
    }
    shaped += "</Root>";
    require(model.load_layers(bytes(shaped), error), "load shape-specific deltas");
    for (int shape = 0; shape < 3; ++shape) {
        pple::Cake cake;
        pple::CakeLayer added;
        const auto rng = [] { return 0; };
        const int height = shape == 2 ? -19 : -23;
        require(model.use_tool(cake, 1, 3 + shape, added, rng) == pple::CakeUse::Added, "initial pan");
        model.use_tool(cake, 0, 0, added, rng);
        require(cake.height_delta == height, "batter height follows pan shape");
        model.use_tool(cake, 0, 1, added, rng);
        require(cake.height_delta == height, "replacement subtracts shaped batter height");
        model.use_tool(cake, 5, 15, added, rng);
        require(cake.height_delta == height - 4, "icing uses shaped top height");
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
