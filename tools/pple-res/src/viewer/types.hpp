#pragma once
#include "resources.hpp"

namespace pple::viewer {
    // Scene mode: a flat draw list in scene coordinates, plus the scene's extent.
    struct DrawItem {
        SDL_Texture* texture = nullptr;
        SDL_FRect src{
        };
        SDL_FRect dst{
        };
        int alpha = 255;
        float rotation = 0.0f;
        // degrees clockwise, about the centre of dst
    };
    // The progress meter: /ProgressMeterContainer (665,40) and one indicator per <Level>, stacked
    // /ProgressMeterIndicators/yOffSet (55) apart, drawn from the CardBack <progpath> sprites.
    struct MeterDraw {
        SDL_Texture* tex = nullptr;
        SDL_FRect src_unlit{};
        SDL_FRect src_lit{};
        SDL_FRect dst{};
    };
    // The jar's tray: one coin sprite per <Level> (`CardBacks.xml`'s /imagepath, drawn at the 38
    // percent scale Jar::DoFiller sets) placed at that level's *_Points.xml coordinates.
    struct JarCoin {
        SDL_Texture* tex = nullptr;
        SDL_FRect src{};
        float w = 0.0f, h = 0.0f;
        float reg_x = 0.0f, reg_y = 0.0f;  // already scaled by 0.38
        float x = 0.0f, y = 0.0f;          // inside /JarContainer
        float rotation = 0.0f;
    };
    // The jar's flying coins.  Board::DoFlyIntoJar (0x10008AF54) spawns one ArcBehavior per
    // matched card and ArcBehavior::Init (0x100083E48) gives it three points -- source, an apex,
    // destination -- which ArcBehavior::Update (0x100084128) walks as a quadratic Bezier over
    // /ArcAnimation/Frames ticks.  All three are in the ArcContainer's own space, which
    // Jar::InitUI (0x100083218) creates as a plain "Base" child of /JarContainer at 0,0, so the
    // scene coordinates below are /JarContainer's origin plus the point.
    struct ArcCoin {
        SDL_Texture* tex = nullptr;
        SDL_FRect src{};
        float sprite_w = 0.0f, sprite_h = 0.0f;
        float reg_x = 0.0f, reg_y = 0.0f;
        float sx = 0.0f, sy = 0.0f;  // source: the card's scene position (+2 in y)
        float cx = 0.0f, cy = 0.0f;  // the Bezier control point
        float dx = 0.0f, dy = 0.0f;  // destination: (LidOffSet + CurJar.w/2, CurJar.h/2)
        float scale0 = 100.0f;       // Board+0x170 + 10, in percent
        int tick = 0;
    };
    // Jar::Update (0x10008373C) in three states: the coins fly, then the level's burst movie
    // plays, then Jar::StopExplosion (0x100083A20) ends the board.
    enum class JarPhase { Idle, Flying, Burst };
    // The level's burst movie: CardBacks.xml's /moviepath, loaded by Jar::LoadMovies
    // (0x1000835D0) and parented to /JarMovieContainer at -15,-425 inside /JarContainer.
    struct BurstMovie {
        bool ok = false;
        pple::Movie movie;
    };
    // The card draw list is built once, from a table of resolved sprites, and only the face /
    // highlight choice is rebuilt afterwards.  Before this, every tick of the reveal timer
    // re-parsed ~25 card node definitions and reloaded the sheets, up to 30 times a second.
    struct CardCache {
        SDL_Texture* tex_face = nullptr;
        SDL_FRect src_face{};
        SDL_Texture* tex_back = nullptr;
        SDL_FRect src_back{};
        float base_x = 0.0f, base_y = 0.0f;  // scene position of the cell's top-left
        float w = 0.0f, h = 0.0f;            // drawn size at the card's own scale
        float cell_w = 0.0f, cell_h = 0.0f;  // the click target and the flip's centring reference
        int index = -1;
    };
    // Card flip, from Card::UpdateFlipForward (0x10007EA20) and UpdateFlipBackward (0x10007EFEC):
    // the visible side narrows by 17 percent per tick to a 15 percent floor, then the other side
    // widens by 17; the shadow tracks at 5 and the vertical scale drifts by 2 either way.  The
    // phase decides which side is drawn.
    enum class FlipPhase { None, Collapse, Expand };
    struct CardFlip {
        FlipPhase phase = FlipPhase::None;
        int scale_x = 100;
        int scale_y = 100;
        bool to_face = true;  // forward flip: the back collapses and the face expands
    };
    // The match animation, which the engine splits over four functions:
    //
    //   Card::DoForceMatch (0x10007D114)  arms `Card+0x3C = 10` and calls Card::InitExplosion,
    //                                     which shows the four sparks and fills their two point
    //                                     arrays from the card's rect and its rect +- 30
    //   Card::AnimateExplosion (0x10007E6D0)  runs them for 30 ticks, then hides them
    //   Card::Update's tail               when the countdown runs out, Card::FlipBack
    //   Card::OnHide (0x10007F538)        the flip-back's end: the art is hidden, the level's
    //                                     "BlankCard" is shown in its place, and
    //                                     Board::DoFlyIntoJar sends the coin
    struct CardFx {
        int countdown = -1;    // Card+0x3C; -1 when no match is pending
        int spark_tick = -1;   // Card+0x118; -1 when there is no explosion
        int flip = 0;          // the flip-back's own tick, 0 when it has not started
        bool doomed = false;   // Card+0x4C
        bool blank = false;    // state 2: the art is gone and the BlankCard shows
    };
    // The time bar is engine-made, not scene-made: <TimeBarGreen>/<TimeBarRed> are 226 1-pixel
    // segments stacked by CreateTimeSlice (0x10008C640), and /TimeBarBkg is a plain draw.
    struct TimeBarDraw {
        bool ok = false;
        SDL_Texture* green = nullptr;
        SDL_FRect green_src{};
        SDL_Texture* red = nullptr;
        SDL_FRect red_src{};
        float x_green = 0.0f, x_red = 0.0f, y_top = 0.0f;
        int segments = 0;
    };
    // The Professor is engine-built too: Professor::InitUI (0x10008BD94) creates a Base from
    // /ProfessorContainer and loads all six Professor movies as its children, so the movie's own
    // coordinate space is anchored at the container's position.  The scene's nine movie slots
    // carry no position at all.
    struct ProfessorDraw {
        bool ok = false;
        pple::Movie movie;
        std::string which;
        float anchor_x = 0.0f, anchor_y = 0.0f;
        float time = 0.0f;
    };
    // Which screen is up, and the clickable rectangles on each.  A hot-spot rect is the engine's
    // node rect: (x, y) - registration, sized SourceWidth x SourceHeight.
    enum class Screen { Other, Menu, Loading, Concentration };
    struct Hotspot {
        std::string name;
        SDL_FRect rect{};
    };
} // namespace pple::viewer
