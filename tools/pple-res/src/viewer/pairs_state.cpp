#include "app.hpp"

#include <cstdio>

#include "pple/png_write.hpp"

namespace pple::viewer {

// The resumable round.  The shipped build keeps it in its own RGMH file next to the scores:
// ConcentrationGame::SaveGameState (0x100082B40) walks the round with ConRoundData::ToXml and
// hands the bytes to XmlStash::SaveXMLFile (0x100079A00), and the same container carries a
// thumbnail of the board ahead of the XML (WriteSpecialFolderFileRichGameHeader 0x10001D18
// copies the caller's thumbnail block in).  PuzzleManager then puts a "saved game" entry on the
// menu and LoadGameState restores it.
//
// The reconstruction keeps that shape -- RGMH container, PNG thumbnail, XML payload -- and
// records what a resume needs rather than the shipped schema's full statistics.  The board is
// stored as *dealt*: Board::ShuffleCards consumes the level's own rand() stream, so re-dealing
// from the seed could not be relied on to reproduce the same card order, while the dealt ids
// reproduce the board exactly.  `ConcentrationBoard::restore` is what puts them back.

namespace {
// A minimal XML writer: the values here are numbers and one short token, so there is nothing to
// escape.
const char* kDifficultyNames[3] = {"Beginner", "Intermediate", "Advanced"};
}  // namespace

std::string App::pairs_state_payload() const {
    std::string xml;
    xml += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
    xml += "<ConcentrationSave>\r\n";
    const char* difficulty = "Beginner";
    const int index = pairs_board.level().difficulty_index;
    if (index >= 0 && index < 3) difficulty = kDifficultyNames[index];
    char line[128];
    std::snprintf(line, sizeof(line), "  <Difficulty>%s</Difficulty>\r\n", difficulty);
    xml += line;
    std::snprintf(line, sizeof(line), "  <Level>%d</Level>\r\n", pairs_board.level_position());
    xml += line;
    std::snprintf(line, sizeof(line), "  <TicksLeft>%d</TicksLeft>\r\n",
                  static_cast<int>(pairs_board.ticks_remaining()));
    xml += line;
    std::snprintf(line, sizeof(line), "  <Tokens>%d</Tokens>\r\n", pairs_board.tokens());
    xml += line;
    std::snprintf(line, sizeof(line), "  <Score>%d</Score>\r\n", pairs_board.score());
    xml += line;
    std::snprintf(line, sizeof(line), "  <Matches>%d</Matches>\r\n", pairs_board.matches());
    xml += line;
    std::snprintf(line, sizeof(line), "  <CardsRemoved>%d</CardsRemoved>\r\n",
                  pairs_board.cards_removed_total());
    xml += line;
    std::snprintf(line, sizeof(line), "  <Clicks>%d</Clicks>\r\n", pairs_board.clicks());
    xml += line;
    std::snprintf(line, sizeof(line), "  <Pending>%d</Pending>\r\n", pairs_board.pending_reveal());
    xml += line;
    xml += "  <Cells>\r\n";
    for (int i = 0; i < pairs_board.cell_count(); ++i) {
        // id, face (0 hidden, 1 shown, 2 matched) and whether the bonus indicator is on it.
        std::snprintf(line, sizeof(line), "    <Cell index=\"%d\" id=\"%d\" face=\"%d\" bonus=\"%d\"/>\r\n",
                      i, pairs_board.card_id(i), static_cast<int>(pairs_board.face(i)),
                      pairs_board.sneak_card(i) ? 1 : 0);
        xml += line;
    }
    xml += "  </Cells>\r\n";
    xml += "</ConcentrationSave>\r\n";
    return xml;
}

// A small picture of the board for the resume entry.  The shipped thumbnail is a picture of the
// board as it stood (WriteSpecialFolderFileRichGameHeader 0x1000A1D18 copies the caller's block
// in).  This draws one from the board's own state -- each cell's face up, down or cleared -- with
// a colour keyed off the card's id, because a texture cannot be sampled back into a surface and
// the save has to work whatever frame the player left on.
std::vector<std::uint8_t> App::render_board_thumbnail(int width, int height) const {
    std::vector<std::uint8_t> out;
    if (width <= 0 || height <= 0 || !pairs_ready || pairs_board.cell_count() <= 0) return out;
    const int cols = std::max(1, pairs_board.columns());
    const int rows = std::max(1, pairs_board.rows());
    out.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    const auto put = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        std::uint8_t* p = out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                        static_cast<std::size_t>(x)) * 4u;
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = 255;
    };
    // The board's own surface, then the shelf around it.
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) put(x, y, 84, 70, 128);
    const int inset = std::max(2, std::min(width, height) / 16);
    const int board_w = width - inset * 2;
    const int board_h = height - inset * 2;
    for (int y = inset; y < inset + board_h; ++y)
        for (int x = inset; x < inset + board_w; ++x) put(x, y, 36, 32, 92);
    const double cell_w = static_cast<double>(board_w) / static_cast<double>(cols);
    const double cell_h = static_cast<double>(board_h) / static_cast<double>(rows);
    for (int i = 0; i < pairs_board.cell_count(); ++i) {
        const int col = i % cols, row = i / cols;
        const int x0 = inset + static_cast<int>(static_cast<double>(col) * cell_w) + 1;
        const int y0 = inset + static_cast<int>(static_cast<double>(row) * cell_h) + 1;
        const int x1 = inset + static_cast<int>(static_cast<double>(col + 1) * cell_w) - 1;
        const int y1 = inset + static_cast<int>(static_cast<double>(row + 1) * cell_h) - 1;
        std::uint8_t r = 198, g = 198, b = 226;  // face down: the level's card back
        if (pairs_board.face(i) == pple::CardFace::Matched) {
            r = 104; g = 116; b = 168;  // cleared: the BlankCard
        } else if (pairs_board.face(i) == pple::CardFace::Shown) {
            // A stable colour per card id, so the same pair reads as the same colour and a
            // resumed player can see which tiles are already up.
            const unsigned id = static_cast<unsigned>(std::max(0, pairs_board.card_id(i)));
            const unsigned h = (id * 2654435761u) >> 24;
            r = static_cast<std::uint8_t>(90 + (h * 5u) % 150u);
            g = static_cast<std::uint8_t>(90 + (h * 7u) % 150u);
            b = static_cast<std::uint8_t>(90 + (h * 11u) % 150u);
        }
        // The bonus indicator, which the shipped capture shows as the golden eye.
        if (pairs_board.sneak_card(i)) {
            r = 240; g = 190; b = 60;
        }
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) put(x, y, r, g, b);
    }
    return out;
}

bool App::write_pair_state(const std::string& path) {
    if (path.empty() || !pairs_ready) return false;
    const std::string xml = pairs_state_payload();
    // UTF-16LE with the BOM, which is how XmlStash::SaveXMLFile writes the payload.
    std::vector<std::uint8_t> payload;
    payload.push_back(0xFF);
    payload.push_back(0xFE);
    for (char c : xml) {
        payload.push_back(static_cast<std::uint8_t>(c));
        payload.push_back(0);
    }
    pple::RgmhFile container;
    container.project_name = u"Purble Place";
    container.file_name = u"ConcentrationStateSave.xml";
    std::string png_error;
    const std::vector<std::uint8_t> rgba = render_board_thumbnail(256, 192);
    if (!rgba.empty() && pple::png_write(rgba, 256, 192, container.thumbnail, png_error)) {
        // Self-check: the encoder's stored-DEFLATE stream is decoded back by the core decoder, so
        // a corrupt thumbnail cannot be written silently.
        pple::PngImage check;
        if (!pple::png_decode(container.thumbnail, check, png_error) || check.width != 256 ||
            check.height != 192) {
            std::printf("  state: thumbnail failed to decode back (%s)\n", png_error.c_str());
            container.thumbnail.clear();
        }
    }
    container.xml = payload;
    container.xml_length = static_cast<std::uint32_t>(payload.size());
    const std::vector<std::uint8_t> bytes = pple::rgmh_write(container);
    if (!write_file(path, bytes)) {
        std::printf("  state: could not write %s\n", path.c_str());
        return false;
    }
    std::printf("  state: wrote %s, %zu bytes, board %dx%d, thumbnail %zu bytes\n", path.c_str(),
                bytes.size(), pairs_board.columns(), pairs_board.rows(),
                container.thumbnail.size());
    return true;
}

namespace {
// The value of <Name> in an XML payload, or an empty string.
std::string element_text(const std::string& xml, const std::string& name) {
    const std::string open = "<" + name + ">";
    const std::string close = "</" + name + ">";
    const std::size_t at = xml.find(open);
    if (at == std::string::npos) return {};
    const std::size_t begin = at + open.size();
    const std::size_t end = xml.find(close, begin);
    if (end == std::string::npos) return {};
    return xml.substr(begin, end - begin);
}

int attribute_int(const std::string& tag, const std::string& name) {
    const std::string key = name + "=\"";
    const std::size_t at = tag.find(key);
    if (at == std::string::npos) return 0;
    return std::atoi(tag.c_str() + at + key.size());
}
}  // namespace

// Restores a round written by write_pair_state.  `pairs_board` must already have been reset for
// the same difficulty, because ConRoundData's own fields (the level list, the constants and the
// pools) come from the scene documents, not from the save.
bool App::load_pair_state(const std::string& path) {
    if (!resume_state_valid) {
        if (!read_pair_state(path)) return false;
    }
    if (!pairs_board.restore(resume_state)) {
        std::printf("  state: %s does not fit this board\n", path.c_str());
        return false;
    }
    std::printf("  state: resumed %s at level %zu/%d, score %d, matches %d, %d token(s)\n",
                path.c_str(), resume_state.level_position + 1, pairs_board.level_count(),
                resume_state.score, resume_state.matches, resume_state.tokens);
    return true;
}

// Reads the container, its XML and its thumbnail once, so the resume screen and the board
// restore agree on what was saved.
bool App::read_pair_state(const std::string& path) {
    resume_state_valid = false;
    resume_thumbnail.clear();
    resume_summary.clear();
    const std::vector<std::uint8_t> bytes = slurp_file(path);
    if (bytes.empty()) return false;
    pple::RgmhFile container;
    std::string error;
    if (!pple::rgmh_read(bytes, container, error)) {
        std::printf("  state: %s is not an RGMH container (%s)\n", path.c_str(), error.c_str());
        return false;
    }
    // The XML is UTF-16LE behind a BOM; read it back as ASCII, which every value here is.
    std::string xml;
    for (std::size_t i = 2; i + 1 < container.xml.size(); i += 2)
        xml.push_back(static_cast<char>(container.xml[i]));
    resume_state = pple::ConcentrationBoard::RestoreState{};
    resume_state.level_position = static_cast<std::size_t>(std::atoi(element_text(xml, "Level").c_str()));
    resume_state.score = std::atoi(element_text(xml, "Score").c_str());
    resume_state.matches = std::atoi(element_text(xml, "Matches").c_str());
    resume_state.cards_removed_total = std::atoi(element_text(xml, "CardsRemoved").c_str());
    resume_state.clicks = std::atoi(element_text(xml, "Clicks").c_str());
    resume_state.ticks_left = std::atoi(element_text(xml, "TicksLeft").c_str());
    resume_state.tokens = std::atoi(element_text(xml, "Tokens").c_str());
    resume_state.pending = std::atoi(element_text(xml, "Pending").c_str());
    const std::size_t cells_at = xml.find("<Cells>");
    const std::size_t cells_end = xml.find("</Cells>");
    if (cells_at == std::string::npos || cells_end == std::string::npos) {
        std::printf("  state: %s has no <Cells>\n", path.c_str());
        return false;
    }
    std::size_t at = cells_at;
    while (true) {
        const std::size_t tag_at = xml.find("<Cell ", at);
        if (tag_at == std::string::npos || tag_at > cells_end) break;
        const std::size_t tag_end = xml.find("/>", tag_at);
        if (tag_end == std::string::npos) break;
        const std::string tag = xml.substr(tag_at, tag_end - tag_at);
        resume_state.card_ids.push_back(attribute_int(tag, "id"));
        const int face = attribute_int(tag, "face");
        resume_state.faces.push_back(face == 2   ? pple::CardFace::Matched
                                     : face == 1 ? pple::CardFace::Shown
                                                 : pple::CardFace::Hidden);
        resume_state.sneak_icon.push_back(attribute_int(tag, "bonus") != 0);
        at = tag_end + 2;
    }
    if (resume_state.card_ids.empty()) {
        std::printf("  state: %s has no <Cell> entries\n", path.c_str());
        return false;
    }
    resume_thumbnail = container.thumbnail;
    resume_difficulty = element_text(xml, "Difficulty");
    if (resume_difficulty.empty()) resume_difficulty = "Beginner";
    resume_summary = resume_difficulty;
    const std::string score = element_text(xml, "Score");
    if (!score.empty()) resume_summary += ", score " + score;
    const std::string matches = element_text(xml, "Matches");
    if (!matches.empty()) resume_summary += ", " + matches + " match(es)";
    resume_state_valid = true;
    std::printf("  state: %s holds a %s round, %zu cell(s), thumbnail %zu bytes\n", path.c_str(),
                resume_summary.c_str(), resume_state.card_ids.size(), resume_thumbnail.size());
    return true;
}

}  // namespace pple::viewer
