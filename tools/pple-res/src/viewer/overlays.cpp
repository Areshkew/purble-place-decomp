#include "app.hpp"
#include <cctype>

namespace pple::viewer {

// Two screens the shipped game opens as separate Win32 dialogs.  A web or mobile build has no
// second window, and the owner's instruction for this milestone was to move both inside the
// game, so they are drawn in the same 800x496 scene as everything else:
//
//   the difficulty chooser   the dialog whose strings are MUI 213 "Select Difficulty",
//                            214 the question, 215..217 the three options with their grid
//                            descriptions and 219 the note about changing it later
//   the end-of-round panel   DialogBoxMCE template 221 (236 under Media Center), opened by
//                            OnWinAnimStop (0x100082670) / OnLoseAnimStop (0x10008272C); its
//                            strings are 59071 "Game Points: %1!d!", 57409 "Final Score: %1!d!",
//                            the Rank: titles from RANKS.XML, 42695 "New Game" and
//                            56503 "Main Menu"
//
// The MUI ids are only available with --mui, so each carries the English text the shipped .mui
// holds as its fallback.  All wording below is the shipped wording; "Change Difficulty" is the
// reconstruction's own label for the Options -> Difficulty path the 219 note describes, because
// that menu item lives in a Win32 menu resource, not in a string table.

namespace {

struct DialogString {
    int id;
    const char* english;
};

const DialogString kSelectDifficulty{213, "Select Difficulty"};
const DialogString kDifficultyQuestion{214, "What level of difficulty do you want to play?"};
const DialogString kDifficultyNote{
    219, "Note: You can change the difficulty level later by clicking Options on the Game menu."};
const DialogString kOptionBeginner{215, "&Beginner \n 5x5 grid, easy images"};
const DialogString kOptionIntermediate{216, "&Intermediate \n 6x6 grid, more challenging images"};
const DialogString kOptionAdvanced{217, "&Advanced \n 8x8 grid, very challenging images"};
const DialogString kGamePoints{59071, "Game Points: %1!d!"};
const DialogString kFinalScore{57409, "Final Score: %1!d!"};
const DialogString kNewGame{42695, "New Game"};
const DialogString kMainMenu{56503, "Main Menu"};

std::string trim(const std::string& text) {
    std::size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

// A dialog label's "&" marks its access key and its first line is the option's name, so
// "&Intermediate \n 6x6 grid, more challenging images" becomes that pair.
void split_option(const std::string& text, std::string& label, std::string& detail) {
    const std::size_t newline = text.find('\n');
    label = trim(newline == std::string::npos ? text : text.substr(0, newline));
    detail = newline == std::string::npos ? std::string{} : trim(text.substr(newline + 1));
    label.erase(std::remove(label.begin(), label.end(), '&'), label.end());
}

// "Final Score: %1!d!" with the number in place of the placeholder.  The .mui carries
// printf-style placeholders because the shipped dialog filled them in.
std::string fill_count(const std::string& text, int value) {
    const std::string number = std::to_string(value);
    const std::size_t at = text.find("%1!d!");
    if (at == std::string::npos) return text + " " + number;
    return text.substr(0, at) + number + text.substr(at + 5);
}

} // namespace

// A choice button drawn from the same green 9-slice as the tip bubbles: the shipped button art
// (NewGameButton.xml's 500,369 / 753,369 pieces) is transparent in this build -- the Media
// Center host draws those two header buttons -- so an in-canvas button needs the panel style
// instead.  Hover is the style's own vocabulary: the title colour and a bold pass, which is
// what <TitleFontBold> would switch on in the shipped style.
void App::append_button(std::vector<DrawItem>& out, const UiButton& button, float label_size) {
    append_panel(out, button.rect.x, button.rect.y, button.rect.w, button.rect.h);
    const std::uint32_t label_colour = button.hover ? tip_style.title_colour : 0x00494A4Du;
    int label_w = 0, label_h = 0;
    measure_text("Segoe UI", label_size, button.label, 0.0f, label_w, label_h);
    const float centre_x = button.rect.x + (button.rect.w - static_cast<float>(label_w)) * 0.5f;
    const float centre_y = button.detail.empty()
                               ? button.rect.y + (button.rect.h - static_cast<float>(label_h)) * 0.5f
                               : button.rect.y + 7.0f;
    append_text(out, "Segoe UI", label_size, label_colour, button.label, centre_x, centre_y, 0.0f);
    if (button.hover)  // a 1-pixel second pass is the only bold this viewer has
        append_text(out, "Segoe UI", label_size, label_colour, button.label, centre_x + 1.0f,
                    centre_y, 0.0f);
    if (!button.detail.empty()) {
        int detail_w = 0, detail_h = 0;
        measure_text("Segoe UI", 8.0f, button.detail, 0.0f, detail_w, detail_h);
        append_text(out, "Segoe UI", 8.0f, tip_style.body_colour, button.detail,
                    button.rect.x + (button.rect.w - static_cast<float>(detail_w)) * 0.5f,
                    centre_y + static_cast<float>(label_h) + 2.0f, 0.0f);
    }
}

// The chooser: the classroom behind, the Professor waiting on the left, the panel and the three
// options.  It is rebuilt when the idle animation moves to its next frame, not every frame, so
// the label textures are not re-rendered 30 times a second.
void App::build_difficulty() {
    picker_buttons.clear();
    picker_time = 0.0f;
    picker_frame = -1;
    picker_last_tick = SDL_GetTicks();
    struct Choice {
        const char* difficulty;
        const DialogString* text;
    };
    const Choice choices[3] = {
        {"Beginner", &kOptionBeginner},
        {"Intermediate", &kOptionIntermediate},
        {"Advanced", &kOptionAdvanced},
    };
    for (int i = 0; i < 3; ++i) {
        UiButton button;
        button.id = choices[i].difficulty;
        button.rect = SDL_FRect{274.0f, 150.0f + static_cast<float>(i) * 62.0f, 251.0f, 51.0f};
        const std::string text = mui_text(choices[i].text->id);
        split_option(text.empty() ? choices[i].text->english : text, button.label, button.detail);
        picker_buttons.push_back(button);
    }
    screen = Screen::Difficulty;
    draw_difficulty();
    std::printf("difficulty: Select Difficulty, %zu option(s)%s\n", picker_buttons.size(),
                mui_strings.empty() ? " (no --mui: the shipped English fallbacks are used)" : "");
}

// Redraw of the chooser: background, movie, panel, text and buttons.
void App::draw_difficulty() {
    free_direct(picker_draws);
    picker_w = static_cast<float>(pple::kScreenWidth);
    picker_h = static_cast<float>(pple::kScreenHeight);
    {
        // The classroom is UI\Concentration\hearts.xml's own sprite -- the same Background.png
        // the board screen puts behind the cards (ConcentrationGame::LoadGameUI 0x100081042).
        const Sprite back = resolve_sprite(bundle, "UI\\Concentration\\hearts.xml", 0);
        std::string how;
        const auto* slot = back.ok ? bundle.resolve(back.requested, how) : nullptr;
        if (slot != nullptr) {
            std::string note;
            if (SDL_Texture* tex = load_sheet_texture(back.requested, slot->entry->name, note)) {
                DrawItem d;
                d.texture = tex;
                pple::SceneItem geometry;
                geometry.src_x = back.src.x;
                geometry.src_y = back.src.y;
                geometry.src_w = back.src.w;
                geometry.src_h = back.src.h;
                geometry.frames = back.frames;
                geometry.per_row = back.per_row;
                geometry.prepped = back.prepped;
                float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
                pple::sprite_frame_rect(geometry, 0, fx, fy, fw, fh);
                d.src = SDL_FRect{fx, fy, fw, fh};
                d.dst = SDL_FRect{0.0f, 0.0f, fw, fh};
                d.alpha = 255;
                picker_draws.push_back(d);
            }
        }
    }
    // The Professor waits in the classroom while the player chooses, which is the same
    // /ProfessorContainer anchor (295,70) the board uses.
    if (load_professor_movie("ProfessorIdleAnimation")) {
        const pple::Movie& idle = prof_movie_cache["ProfessorIdleAnimation"];
        const float fps = idle.parts.empty() ? 12.0f : pple::movie_frame_rate(idle, idle.parts.front());
        const int mframe = (idle.frame_count > 1 && fps > 0.0f)
                               ? static_cast<int>(picker_time * fps) % idle.frame_count
                               : 0;
        float extent_w = 0.0f, extent_h = 0.0f;
        append_movie(idle, mframe, 295.0f, 70.0f, picker_draws, extent_w, extent_h);
    }
    if (load_tip_style()) {
        append_panel(picker_draws, 180.0f, 40.0f, 440.0f, 300.0f);
        const std::string title = mui_text(kSelectDifficulty.id);
        const std::string question = mui_text(kDifficultyQuestion.id);
        const std::string note = mui_text(kDifficultyNote.id);
        append_text(picker_draws, tip_style.title_face, 14.0f, tip_style.title_colour,
                    title.empty() ? kSelectDifficulty.english : title, 194.0f, 56.0f, 0.0f);
        append_text(picker_draws, tip_style.body_face, 10.0f, 0x00303030u,
                    question.empty() ? kDifficultyQuestion.english : question, 194.0f, 82.0f,
                    412.0f);
        append_text(picker_draws, tip_style.body_face, 8.0f, 0x00606060u,
                    note.empty() ? kDifficultyNote.english : note, 194.0f, 104.0f, 412.0f);
    }
    for (const UiButton& button : picker_buttons) append_button(picker_draws, button, 12.0f);
}

// The end-of-round summary, drawn over the live board.  `won` picks the title from the same
// TIPS.XML entries the Professor's own win and lose bubbles use ("You Won!" / "Lost Game").
void App::build_result_panel(bool won) {
    load_tip_style();
    load_tips();
    free_direct(result_draws);
    result_buttons.clear();
    result_shown = true;
    result_won = won;

    const float panel_x = 200.0f, panel_y = 86.0f, panel_w = 400.0f, panel_h = 300.0f;
    append_panel(result_draws, panel_x, panel_y, panel_w, panel_h);

    const TipDef* title_tip = find_tip(won ? "ProWin1" : "ProLose1");
    const std::string title = title_tip != nullptr ? title_tip->title
                             : (won ? std::string("You Won!") : std::string("Lost Game"));
    append_text(result_draws, tip_style.title_face, 16.0f, tip_style.title_colour, title,
                panel_x + 16.0f, panel_y + 14.0f, 0.0f);

    float y = panel_y + 46.0f;
    const auto line = [&](const DialogString& source, int value) {
        const std::string text = mui_text(source.id);
        append_text(result_draws, tip_style.body_face, 11.0f, 0x00303030u,
                    fill_count(text.empty() ? source.english : text, value), panel_x + 16.0f, y,
                    0.0f);
        y += 20.0f;
    };
    line(kGamePoints, pairs_board.score());
    if (won && last_end_ok) {
        line(kFinalScore, last_end.total);
        append_text(result_draws, tip_style.body_face, 11.0f, tip_style.title_colour, last_end.title,
                    panel_x + 16.0f, y, 0.0f);
        y += 20.0f;
    } else if (!won) {
        append_text(result_draws, tip_style.body_face, 9.0f, 0x00606060u,
                    "Try again, or pick another difficulty.", panel_x + 16.0f, y, 0.0f);
        y += 20.0f;
    }

    const std::string new_game = mui_text(kNewGame.id);
    const std::string main_menu = mui_text(kMainMenu.id);
    UiButton again;
    again.id = "PlayAgain";
    again.rect = SDL_FRect{274.0f, 216.0f, 251.0f, 51.0f};
    again.label = new_game.empty() ? kNewGame.english : new_game;
    UiButton difficulty;
    difficulty.id = "ChangeDifficulty";
    difficulty.rect = SDL_FRect{274.0f, 272.0f, 251.0f, 51.0f};
    difficulty.label = "Change Difficulty";
    UiButton menu;
    menu.id = "MainMenu";
    menu.rect = SDL_FRect{274.0f, 328.0f, 251.0f, 51.0f};
    menu.label = main_menu.empty() ? kMainMenu.english : main_menu;
    result_buttons = {again, difficulty, menu};
    for (const UiButton& button : result_buttons) append_button(result_draws, button, 12.0f);
    rebuild_overlay();
    std::printf("  result panel: %s, score %d%s\n", won ? "WON" : "LOST", pairs_board.score(),
                (won && last_end_ok)
                    ? (", " + std::to_string(last_end.total) + " -> " + last_end.title).c_str()
                    : "");
}

// ConcentrationGame::NewGame (0x100081E4C): a fresh round at a difficulty, optionally with the
// Professor's entrance and his two intro bubbles.
void App::start_game(const std::string& difficulty, bool intro) {
    picker_choice = difficulty;
    hide_tip();
    free_direct(result_draws);
    result_buttons.clear();
    result_shown = false;
    last_end_ok = false;
    pairs_spec = difficulty + ",0";
    screen = Screen::Concentration;
    menu_ready = false;
    pairs_ready = false;
    build_pairs();
    if (intro) {
        // The first game in a session gets the "How To Play" pair; later ones get the single
        // ProWelcome bubble, which is what DisplayIntro (0x10008C3F0) chooses between.
        professor.tip_name = intro_seen ? "ProWelcome" : "ProIntro";
        intro_seen = true;
        professor.anim_kind = 0;
        professor.tip_y = 150.0f;
        professor_show("ProfessorEnterAnimation", ProfPhase::Enter, false);
    }
    rebuild_overlay();
}

void App::game_to_menu() {
    hide_tip();
    free_direct(result_draws);
    result_buttons.clear();
    result_shown = false;
    pairs_ready = false;
    screen = Screen::Menu;
    menu_hover_name.clear();
    build_menu();
    rebuild_overlay();
}

} // namespace pple::viewer
