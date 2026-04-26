#include "MakerWorldImportConverter.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/log/trivial.hpp>

#include <wx/msgdlg.h>
#include <wx/string.h>
#include <wx/window.h>

#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "format.hpp"

namespace Slic3r { namespace GUI {

namespace {

constexpr const char* AD5X_PRINTER_PRESET = "Flashforge AD5X 0.4 nozzle";
constexpr const char* AD5X_COMPAT_HINT    = "Flashforge AD5X";
constexpr double      BED_W_MM            = 220.0;
constexpr double      BED_H_MM            = 220.0;

struct FilamentMapping {
    size_t      slot_idx;
    std::string source_preset;
    std::string target_preset;
    std::string filament_type;
    std::string color;
};

struct ConversionPlan {
    std::string              source_printer;
    std::string              source_process;
    std::string              target_printer = AD5X_PRINTER_PRESET;
    std::string              target_process;
    std::vector<FilamentMapping> filaments;
    double                   source_layer_height = 0.0;
    bool                     bed_exceeded        = false;
};

static double get_layer_height(const Preset& preset)
{
    if (auto* opt = preset.config.option<ConfigOptionFloat>("layer_height"))
        return opt->value;
    return 0.0;
}

static bool is_ad5x_printer_preset(const std::string& printer_preset_name)
{
    return boost::starts_with(printer_preset_name, "Flashforge AD5X");
}

static bool preset_is_compatible_with_ad5x(const Preset& preset)
{
    if (auto* opt = preset.config.option<ConfigOptionStrings>("compatible_printers")) {
        for (const auto& v : opt->values)
            if (boost::icontains(v, AD5X_COMPAT_HINT))
                return true;
    }
    return false;
}

// Find the AD5X process preset whose layer_height is closest to `target_lh`.
static const Preset* find_matching_process(PresetBundle& bundle, double target_lh)
{
    const Preset* best = nullptr;
    double best_diff = 1e9;
    for (const Preset& p : bundle.prints.get_presets()) {
        if (!p.is_visible)
            continue;
        if (!preset_is_compatible_with_ad5x(p))
            continue;
        double lh = get_layer_height(p);
        if (lh <= 0.0)
            continue;
        double diff = std::abs(lh - target_lh);
        // Prefer profiles named "Standard" when tied.
        if (diff < best_diff ||
            (std::abs(diff - best_diff) < 1e-6 && boost::icontains(p.name, "Standard"))) {
            best_diff = diff;
            best = &p;
        }
    }
    return best;
}

// Find an AD5X filament preset for a given filament_type (e.g. "PLA").
// Prefer "Flashforge Generic <type>" first, then any AD5X-compatible match.
static const Preset* find_matching_filament(PresetBundle& bundle, const std::string& filament_type)
{
    if (filament_type.empty())
        return nullptr;

    const Preset* generic_match = nullptr;
    const Preset* any_match     = nullptr;

    for (const Preset& p : bundle.filaments.get_presets()) {
        if (!p.is_visible)
            continue;
        if (!preset_is_compatible_with_ad5x(p))
            continue;
        auto* type_opt = p.config.option<ConfigOptionStrings>("filament_type");
        if (!type_opt || type_opt->values.empty())
            continue;
        if (!boost::iequals(type_opt->values.front(), filament_type))
            continue;

        if (!any_match)
            any_match = &p;
        if (boost::icontains(p.name, "Generic") && !generic_match)
            generic_match = &p;
    }
    return generic_match ? generic_match : any_match;
}

static ConversionPlan build_plan(PresetBundle& bundle)
{
    ConversionPlan plan;

    plan.source_printer = bundle.printers.get_edited_preset().name;
    plan.source_process = bundle.prints.get_edited_preset().name;
    plan.source_layer_height = get_layer_height(bundle.prints.get_edited_preset());

    // Check bed bounds against AD5X's 220×220 using printable_area.
    if (auto* opt = bundle.printers.get_edited_preset().config.option<ConfigOptionPoints>("printable_area")) {
        double max_x = 0.0, max_y = 0.0;
        for (const auto& p : opt->values) {
            max_x = std::max(max_x, p.x());
            max_y = std::max(max_y, p.y());
        }
        if (max_x > BED_W_MM + 0.5 || max_y > BED_H_MM + 0.5)
            plan.bed_exceeded = true;
    }

    if (const Preset* proc = find_matching_process(bundle, plan.source_layer_height))
        plan.target_process = proc->name;

    // Read filament colors from project_config (user-set on sidebar).
    std::vector<std::string> colors;
    if (auto* opt = bundle.project_config.option<ConfigOptionStrings>("filament_colour"))
        colors = opt->values;

    for (size_t i = 0; i < bundle.filament_presets.size(); ++i) {
        const std::string& src_name = bundle.filament_presets[i];
        const Preset*      src      = bundle.filaments.find_preset(src_name);
        std::string        ftype    = "PLA";
        if (src) {
            if (auto* t = src->config.option<ConfigOptionStrings>("filament_type"))
                if (!t->values.empty()) ftype = t->values.front();
        }
        FilamentMapping m;
        m.slot_idx       = i;
        m.source_preset  = src_name;
        m.filament_type  = ftype;
        m.color          = (i < colors.size()) ? colors[i] : std::string();
        if (const Preset* tgt = find_matching_filament(bundle, ftype))
            m.target_preset = tgt->name;
        plan.filaments.push_back(std::move(m));
    }
    return plan;
}

static wxString format_summary(const ConversionPlan& plan)
{
    wxString s;
    s += _L("The project was designed for another printer and has been prepared for your AD5X.") + "\n\n";
    s += wxString::Format("  %s:\n    %s  %s  %s\n",
                          _L("Printer"),
                          from_u8(plan.source_printer),
                          wxString::FromUTF8("\xE2\x86\x92"),
                          from_u8(plan.target_printer));
    if (!plan.target_process.empty())
        s += wxString::Format("  %s (%.2f mm):\n    %s  %s  %s\n",
                              _L("Process"), plan.source_layer_height,
                              from_u8(plan.source_process),
                              wxString::FromUTF8("\xE2\x86\x92"),
                              from_u8(plan.target_process));
    else
        s += wxString::Format("  %s: %s\n",
                              _L("Process"),
                              _L("no matching profile found — keeping current"));

    if (!plan.filaments.empty()) {
        s += "\n";
        s += wxString::Format("  %s:\n", _L("Filaments"));
        for (const auto& f : plan.filaments) {
            wxString color_note = f.color.empty() ? wxString() : wxString::Format(" [%s]", f.color);
            if (!f.target_preset.empty())
                s += wxString::Format("    %zu. %s%s  %s  %s\n",
                                      f.slot_idx + 1, from_u8(f.filament_type), color_note,
                                      wxString::FromUTF8("\xE2\x86\x92"),
                                      from_u8(f.target_preset));
            else
                s += wxString::Format("    %zu. %s%s  %s  %s\n",
                                      f.slot_idx + 1, from_u8(f.filament_type), color_note,
                                      wxString::FromUTF8("\xE2\x86\x92"),
                                      _L("no matching AD5X filament — keeping current"));
        }
    }

    if (plan.bed_exceeded) {
        s += "\n";
        s += wxString::Format("%s\n",
                              _L("Warning: the source bed is larger than AD5X's 220x220 mm. "
                                 "Check that your model fits before slicing."));
    }
    return s;
}

static void apply_plan(const ConversionPlan& plan)
{
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return;

    // 1) Switch printer. This causes compatible filaments/process to auto-select
    //    to AD5X system defaults, which we then override below.
    // `force_select=true` silences the "preset dirty" prompt — the imported 3mf
    // always leaves the current preset dirty, and the user already consented.
    if (Tab* printer_tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
        printer_tab->select_preset(plan.target_printer, /*delete_current*/ false, /*last_selected_ph_printer_name*/ "", /*force_select*/ true);

    // Snapshot colors; printer-switch may reset the project filament list.
    std::vector<std::string> colors;
    colors.reserve(plan.filaments.size());
    for (const auto& f : plan.filaments)
        colors.push_back(f.color);

    // 2) Resize the filament-slot count to match the project. Printer-switch
    //    typically collapses to a single slot, so without this the next steps
    //    would skip slots >= filament_presets.size() and the flush_volumes_matrix
    //    would stay sized for 1 filament — slicing then throws
    //    "Flush volumes matrix do not match to the correct size!".
    //    set_num_filaments also calls update_multi_material_filament_presets,
    //    so the matrix is rebuilt to filament_count^2 * extruder_count.
    if (!plan.filaments.empty())
        bundle->set_num_filaments(static_cast<unsigned>(plan.filaments.size()));

    // 3) Override filament slots with the mapped presets. If no mapping was
    //    found for a slot, leave whatever the printer-switch picked.
    for (const auto& f : plan.filaments) {
        if (f.target_preset.empty() || f.slot_idx >= bundle->filament_presets.size())
            continue;
        bundle->set_filament_preset(f.slot_idx, f.target_preset);
    }

    // 4) Restore the user-chosen colors, which the printer-switch may have reset.
    if (auto* color_opt = bundle->project_config.option<ConfigOptionStrings>("filament_colour", true)) {
        if (color_opt->values.size() < colors.size())
            color_opt->values.resize(colors.size());
        for (size_t i = 0; i < colors.size(); ++i)
            if (!colors[i].empty() && i < color_opt->values.size())
                color_opt->values[i] = colors[i];
    }

    // 5) Override process preset last (after filament updates settle).
    if (!plan.target_process.empty()) {
        if (Tab* print_tab = wxGetApp().get_tab(Preset::TYPE_PRINT))
            print_tab->select_preset(plan.target_process, false, "", true);
    }

    bundle->export_selections(*wxGetApp().app_config);

    // Refresh filament tab and sidebar combos so UI reflects the swap.
    if (Plater* plater = wxGetApp().plater()) {
        plater->sidebar().on_filament_count_change(bundle->filament_presets.size());
        plater->sidebar().update_all_preset_comboboxes();
        for (size_t i = 0; i < bundle->filament_presets.size(); ++i)
            plater->on_filament_change(i);
        plater->update_project_dirty_from_presets();
        // Auto-arrange on the new printer's bed: the source bed (typically a
        // Bambu) is usually larger than the AD5X, so the original positions
        // can leave parts off-bed. Defer to next tick so the bed shape update
        // from the printer-switch is fully applied first.
        plater->CallAfter([plater] {
            if (plater->can_arrange())
                plater->arrange();
        });
    }
}

} // namespace

void maybe_convert_loaded_project_to_ad5x(wxWindow* parent)
{
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return;

    const std::string current_printer = bundle->printers.get_edited_preset().name;
    if (is_ad5x_printer_preset(current_printer))
        return;

    // Need the AD5X printer preset installed; abort gracefully otherwise.
    if (!bundle->printers.find_preset(AD5X_PRINTER_PRESET)) {
        BOOST_LOG_TRIVIAL(info) << "MakerWorldImportConverter: AD5X preset not installed, skipping.";
        return;
    }

    ConversionPlan plan = build_plan(*bundle);

    wxString msg = format_summary(plan) + "\n" +
                   _L("Apply this conversion? (No keeps the project's original presets.)");
    MessageDialog dlg(parent, msg,
                      _L("Convert project to AD5X"),
                      wxYES_NO | wxYES_DEFAULT | wxICON_INFORMATION);
    if (dlg.ShowModal() != wxID_YES)
        return;

    apply_plan(plan);
}

}} // namespace Slic3r::GUI
