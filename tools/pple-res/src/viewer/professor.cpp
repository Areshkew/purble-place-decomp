#include "app.hpp"

namespace pple::viewer {

// The card layer: only the face choice, the highlight and the flip progress change after a
// build, so this is all that runs per tick.  No XML, no sheet loads.
// Assigned after build_pairs; the Professor is a movie the scene document does not place.
bool App::load_professor(const std::string& instance) {
    if (!loaded_scene) {
        std::printf("  professor %s: no scene\n", instance.c_str());
        return false;
    }
    for (const pple::SceneItem& item : loaded_scene->items) {
        if (item.instance != instance && item.name != instance) continue;
        std::string m_how;
        const auto* m_slot = bundle.resolve(item.ref, m_how);
        if (m_slot == nullptr) {
            std::printf("  professor %s: movie resource not found: %s\n", instance.c_str(),
                        item.ref.c_str());
            return false;
        }
        pple::Movie movie;
        std::string m_error;
        if (!pple::movie_load(bundle.read(*m_slot), fetch_resource, movie, m_error)) {
            std::printf("  professor %s: %s\n", instance.c_str(), m_error.c_str());
            return false;
        }
        professor.movie = std::move(movie);
        professor.which = instance;
        professor.ok = true;
        professor.time = 0.0f;
        return true;
    }
    std::printf("  professor %s: no such movie slot in the scene\n", instance.c_str());
    return false;
}

} // namespace pple::viewer
