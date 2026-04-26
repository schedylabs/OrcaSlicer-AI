#ifndef slic3r_MakerWorldImportConverter_hpp_
#define slic3r_MakerWorldImportConverter_hpp_

// ORCA (fork): converts a freshly-loaded project whose embedded printer preset
// is a non-FlashForge model (typically a MakerWorld / Bambu 3mf) into this
// fork's default AD5X configuration. Runs after Plater::load_project() so it
// operates on the active preset bundle selection, mirroring what a user would
// do by picking AD5X + matching process/filaments from the sidebar.

class wxWindow;

namespace Slic3r {
namespace GUI {

// Public entry point. Safe to call on any load; returns early if the current
// printer is already an AD5X variant or if no sensible AD5X match exists.
void maybe_convert_loaded_project_to_ad5x(wxWindow* parent);

}} // namespace Slic3r::GUI

#endif
