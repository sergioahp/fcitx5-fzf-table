#include "fzftable/engine.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "fcitx-config/configuration.h"
#include "fcitx-config/iniparser.h"
#include "fcitx-config/option.h"
#include "fcitx-config/rawconfig.h"
#include "fcitx-module/dbus/dbus_public.h"
#include "fcitx-utils/capabilityflags.h"
#include "fcitx-utils/dbus/objectvtable.h"
#include "fcitx-utils/i18n.h"
#include "fcitx-utils/key.h"
#include "fcitx-utils/keysym.h"
#include "fcitx-utils/log.h"
#include "fcitx-utils/standardpaths.h"
#include "fcitx-utils/textformatflags.h"
#include "fcitx-utils/utf8.h"
#include "fcitx/addonfactory.h"
#include "fcitx/addoninstance.h"
#include "fcitx/addonmanager.h"
#include "fcitx/candidatelist.h"
#include "fcitx/event.h"
#include "fcitx/inputcontext.h"
#include "fcitx/inputcontextmanager.h"
#include "fcitx/inputcontextproperty.h"
#include "fcitx/inputpanel.h"
#include "fcitx/instance.h"
#include "fcitx/text.h"
#include "fcitx/userinterface.h"

// Hotkey-triggered fuzzy picker for the four fzf tables.
//
// Shape is a near-direct port of fcitx5-unicode (modules/unicode/unicode.cpp),
// which is the canonical fcitx5 pattern for a "press hotkey -> popup ->
// pick -> commit -> go away" addon. Differences from unicode:
//   * Four configurable trigger keys (one per table), not one.
//   * Matching is delegated to libs/fzftable (Session/Table/Match), the same
//     engine the sticky IM-style fcitx5-fzf-table addon uses.
//   * Per-IC state carries which table is currently active; when active_spec_
//     is nullptr the picker is off and the PreInputMethod watcher does
//     nothing, so the user's normal IM behaves normally.

namespace fcitx {

namespace {

struct TableSpec {
    const char *unique_name;
    const char *display_name;
    const char *file_name;
};

constexpr std::array<TableSpec, 4> kTableSpecs{{
    {"emoji",   "Emoji",   "emoji.tab"},
    {"kaomoji", "Kaomoji", "kaomoji.tab"},
    {"latex",   "LaTeX",   "latex.tab"},
    {"ipa",     "IPA",     "ipa.tab"},
}};

fzfmatch::CaseMode parse_case_mode(std::string_view value) {
    if (value == "Ignore" || value == "ignore") {
        return fzfmatch::CaseMode::Ignore;
    }
    if (value == "Respect" || value == "respect") {
        return fzfmatch::CaseMode::Respect;
    }
    return fzfmatch::CaseMode::Smart;
}

bool has_non_shift_modifier(const Key &key) {
    const auto states = key.states();
    return states.test(KeyState::Ctrl) || states.test(KeyState::Alt) ||
           states.test(KeyState::Super) || states.test(KeyState::Super2) ||
           states.test(KeyState::Hyper) || states.test(KeyState::Meta);
}

std::string text_input_for_key(const Key &key) {
    if (has_non_shift_modifier(key)) {
        return {};
    }
    auto text = Key::keySymToUTF8(key.sym());
    if (text.empty()) {
        return {};
    }
    if (text == "\n" || text == "\r" || text == "\t" || text == "\b" ||
        text == "\x1b" || text == "\x7f") {
        return {};
    }
    return text;
}

FCITX_CONFIGURATION(
    FzfPickerConfig,
    KeyListOption triggerEmoji{this, "TriggerEmoji",   _("Emoji trigger"),   {}, KeyListConstrain()};
    KeyListOption triggerKao  {this, "TriggerKaomoji", _("Kaomoji trigger"), {}, KeyListConstrain()};
    KeyListOption triggerLatex{this, "TriggerLatex",   _("LaTeX trigger"),   {}, KeyListConstrain()};
    KeyListOption triggerIpa  {this, "TriggerIPA",     _("IPA trigger"),     {}, KeyListConstrain()};
    Option<int, IntConstrain> pageSize{
        this, "PageSize", _("Page size"), 10, IntConstrain(1, 50)};
    Option<bool> normalize{
        this, "EnableNormalize", _("Normalize characters"), true};
    Option<std::string> caseMode{
        this, "CaseMode", _("Case mode"), "Smart"};
);

class FzfPicker;

class FzfPickerState : public InputContextProperty {
public:
    explicit FzfPickerState(FzfPicker *owner) : owner_(owner) {}

    void reset(InputContext *ic) {
        active_spec_ = nullptr;
        session_.set_table(nullptr);
        ic->inputPanel().reset();
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    FzfPicker *       owner_;
    const TableSpec * active_spec_ = nullptr;  // nullptr == picker is off
    fzftable::Session session_;
};

class PickerCandidateWord : public CandidateWord {
public:
    PickerCandidateWord(FzfPicker *owner, const fzftable::Match &match);
    void select(InputContext *ic) const override;

private:
    FzfPicker * owner_;
    std::string value_;
};

// D-Bus surface for triggering the picker from outside fcitx5 (e.g. from a
// hotkey daemon like xremap that can run a one-shot `gdbus call`). Lives on
// the same well-known name fcitx5 already owns (org.fcitx.Fcitx5) at
// /fzfpicker, interface org.fcitx.Fcitx5.Addon.FzfPicker1.
class FzfPickerDBus : public dbus::ObjectVTable<FzfPickerDBus> {
public:
    explicit FzfPickerDBus(FzfPicker *parent) : parent_(parent) {}

    void triggerEmoji();
    void triggerKaomoji();
    void triggerLatex();
    void triggerIpa();

    FCITX_OBJECT_VTABLE_METHOD(triggerEmoji,   "TriggerEmoji",   "", "");
    FCITX_OBJECT_VTABLE_METHOD(triggerKaomoji, "TriggerKaomoji", "", "");
    FCITX_OBJECT_VTABLE_METHOD(triggerLatex,   "TriggerLatex",   "", "");
    FCITX_OBJECT_VTABLE_METHOD(triggerIpa,     "TriggerIpa",     "", "");

private:
    FzfPicker *parent_;
};

class FzfPicker : public AddonInstance {
    static constexpr char kConfigFile[] = "conf/fzfpicker.conf";

public:
    // Declared before the constructor so the auto-returning accessor's body
    // is in scope at the addObjectVTable call site below.
    FCITX_ADDON_DEPENDENCY_LOADER(dbus, instance_->addonManager());

    explicit FzfPicker(Instance *instance)
        : instance_(instance),
          factory_([this](InputContext &) { return new FzfPickerState(this); }) {
        instance_->inputContextManager().registerProperty("fzfPickerState",
                                                          &factory_);

        // Default-phase: triggers when the picker is off.
        event_handlers_.emplace_back(instance_->watchEvent(
            EventType::InputContextKeyEvent, EventWatcherPhase::Default,
            [this](Event &event) {
                auto &ke = static_cast<KeyEvent &>(event);
                if (ke.isRelease()) {
                    return;
                }
                on_trigger_phase(ke);
            }));

        // PreInputMethod-phase: eats every key while the picker is on so the
        // user's actual IM never sees them.
        event_handlers_.emplace_back(instance_->watchEvent(
            EventType::InputContextKeyEvent, EventWatcherPhase::PreInputMethod,
            [this](Event &event) {
                auto &ke = static_cast<KeyEvent &>(event);
                auto *state = ke.inputContext()->propertyFor(&factory_);
                if (state->active_spec_ == nullptr) {
                    return;
                }
                on_search_phase(ke);
            }));

        // Picker drops on focus loss / external IC reset / IM switch.
        auto resetHandler = [this](Event &event) {
            auto &ice = static_cast<InputContextEvent &>(event);
            auto *state = ice.inputContext()->propertyFor(&factory_);
            if (state->active_spec_) {
                state->reset(ice.inputContext());
            }
        };
        event_handlers_.emplace_back(instance_->watchEvent(
            EventType::InputContextFocusOut, EventWatcherPhase::Default,
            resetHandler));
        event_handlers_.emplace_back(instance_->watchEvent(
            EventType::InputContextReset, EventWatcherPhase::Default,
            resetHandler));
        event_handlers_.emplace_back(instance_->watchEvent(
            EventType::InputContextSwitchInputMethod,
            EventWatcherPhase::Default, resetHandler));

        reloadConfig();

        // Hook our D-Bus object onto fcitx5's already-owned bus name.
        // dbus is a hard dep (see fzfpicker.conf.in), so the call is safe.
        dbus_object_ = std::make_unique<FzfPickerDBus>(this);
        dbus()->call<IDBusModule::bus>()->addObjectVTable(
            "/fzfpicker", "org.fcitx.Fcitx5.Addon.FzfPicker1", *dbus_object_);
    }

    ~FzfPicker() override = default;

    void reloadConfig() override { readAsIni(config_, kConfigFile); }

    const Configuration *getConfig() const override { return &config_; }

    void setConfig(const RawConfig &raw) override {
        config_.load(raw, true);
        safeSaveAsIni(config_, kConfigFile);
    }

    // PickerCandidateWord::select reaches back through here.
    void reset_context(InputContext *ic) {
        ic->propertyFor(&factory_)->reset(ic);
    }

private:
    const TableSpec *match_trigger(const Key &key) const {
        if (key.checkKeyList(*config_.triggerEmoji)) return &kTableSpecs[0];
        if (key.checkKeyList(*config_.triggerKao))   return &kTableSpecs[1];
        if (key.checkKeyList(*config_.triggerLatex)) return &kTableSpecs[2];
        if (key.checkKeyList(*config_.triggerIpa))   return &kTableSpecs[3];
        return nullptr;
    }

    const fzftable::Table *ensure_table(const TableSpec &spec) {
        if (auto iter = tables_.find(spec.unique_name); iter != tables_.end()) {
            return iter->second.get();
        }
        if (table_errors_.contains(spec.unique_name)) {
            return nullptr;
        }
        const auto relative_path =
            std::filesystem::path("fzf-table") / spec.file_name;
        auto full_path = StandardPaths::global().locate(
            StandardPathsType::PkgData, relative_path);
        if (full_path.empty()) {
            auto message = "missing table data: " + relative_path.string();
            table_errors_.emplace(spec.unique_name, std::move(message));
            return nullptr;
        }
        try {
            auto table = std::make_shared<fzftable::Table>(
                fzftable::Table::load_file(full_path));
            auto *raw = table.get();
            tables_.emplace(spec.unique_name, std::move(table));
            return raw;
        } catch (const std::exception &error) {
            table_errors_.emplace(spec.unique_name, error.what());
            return nullptr;
        }
    }

    fzftable::SearchOptions search_options() const {
        // Decoupled from pageSize so the candidate list has multiple pages
        // to scroll through; CommonCandidateList auto-pages and cycles on
        // nextCandidate, so all we have to do is feed it enough rows.
        return {
            .limit = 2000,
            .match_options =
                {
                    .case_mode = parse_case_mode(config_.caseMode.value()),
                    .normalize = config_.normalize.value(),
                    .forward = true,
                },
        };
    }

    void on_trigger_phase(KeyEvent &ke) {
        const auto *spec = match_trigger(ke.key());
        if (!spec) {
            return;
        }
        activate_picker(ke.inputContext(), spec);
        ke.filterAndAccept();
    }

    // Sets up the picker on `ic`. Used by the trigger-key handler AND by the
    // D-Bus methods (which receive no key event, only an InputContext lookup).
    // Re-trigger semantics: swap tables if the picker was already open.
    void activate_picker(InputContext *ic, const TableSpec *spec) {
        if (!ic || !spec) {
            return;
        }
        auto *state = ic->propertyFor(&factory_);
        state->active_spec_ = spec;
        state->session_.set_table(ensure_table(*spec));
        state->session_.clear();
        state->session_.rerank(search_options());
        update_ui(ic, state, /*show_trigger_hint=*/true);
    }

public:
    // Entry points for the D-Bus interface. Use lastFocusedInputContext so
    // the picker opens on whatever window the user is currently typing in.
    void triggerOnFocused(std::size_t index) {
        if (index >= kTableSpecs.size()) {
            return;
        }
        activate_picker(instance_->inputContextManager().lastFocusedInputContext(),
                        &kTableSpecs[index]);
    }

private:

    void on_search_phase(KeyEvent &ke) {
        // Mirror the unicode addon: stamp the event so nothing downstream
        // touches it, then maybe accept based on what we do with the key.
        ke.filter();
        if (ke.isRelease()) {
            return;
        }
        handle_search(ke);
    }

    void handle_search(KeyEvent &ke) {
        auto *ic    = ke.inputContext();
        auto *state = ic->propertyFor(&factory_);

        if (auto cl = ic->inputPanel().candidateList(); cl && !cl->empty()) {
            if (ke.key().checkKeyList(
                    instance_->globalConfig().defaultPrevPage())) {
                ke.filterAndAccept();
                cl->toPageable()->prev();
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                return;
            }
            if (ke.key().checkKeyList(
                    instance_->globalConfig().defaultNextPage())) {
                ke.filterAndAccept();
                cl->toPageable()->next();
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                return;
            }
            if (ke.key().checkKeyList(
                    instance_->globalConfig().defaultPrevCandidate())) {
                ke.filterAndAccept();
                cl->toCursorMovable()->prevCandidate();
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                return;
            }
            if (ke.key().checkKeyList(
                    instance_->globalConfig().defaultNextCandidate())) {
                ke.filterAndAccept();
                cl->toCursorMovable()->nextCandidate();
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                return;
            }
            // Tab commits the highlighted candidate. Enter is intentionally
            // not bound - this picker often opens over a chat app and we
            // don't want an accidental Enter to send a half-typed message.
            if (ke.key().check(FcitxKey_Tab)) {
                ke.filterAndAccept();
                const auto cursor = cl->cursorIndex();
                if (cursor >= 0 && cursor < cl->size()) {
                    cl->candidate(cursor).select(ic);
                }
                return;
            }
        }

        if (ke.key().check(FcitxKey_Escape)) {
            ke.accept();
            state->reset(ic);
            return;
        }

        if (ke.key().check(FcitxKey_BackSpace)) {
            ke.accept();
            state->session_.backspace();
            state->session_.rerank(search_options());
            update_ui(ic, state, false);
            return;
        }

        // Swallow modifier-only / unrecognized modified keys silently.
        if (ke.key().isModifier() || has_non_shift_modifier(ke.key())) {
            return;
        }

        auto text = text_input_for_key(ke.key());
        if (text.empty()) {
            return;
        }
        ke.accept();
        state->session_.append(text);
        state->session_.rerank(search_options());
        update_ui(ic, state, false);
    }

    void update_ui(InputContext *ic, FzfPickerState *state,
                   bool show_trigger_hint) {
        ic->inputPanel().reset();

        if (state->active_spec_) {
            Text aux_up(_("FZF Picker: "));
            aux_up.append(state->active_spec_->display_name);
            if (show_trigger_hint && state->session_.query().empty()) {
                aux_up.append(_(" (type to search, Esc to cancel)"));
            }
            ic->inputPanel().setAuxUp(aux_up);

            if (auto err = table_errors_.find(state->active_spec_->unique_name);
                err != table_errors_.end()) {
                ic->inputPanel().setAuxDown(Text(err->second));
            }
        }

        if (!state->session_.matches().empty()) {
            auto cl = std::make_unique<CommonCandidateList>();
            cl->setPageSize(config_.pageSize.value());
            cl->setLayoutHint(CandidateLayoutHint::Vertical);
            cl->setCursorPositionAfterPaging(
                CursorPositionAfterPaging::ResetToFirst);
            for (const auto &match : state->session_.matches()) {
                cl->append<PickerCandidateWord>(this, match);
            }
            if (!cl->empty()) {
                cl->setGlobalCursorIndex(0);
            }
            ic->inputPanel().setCandidateList(std::move(cl));
        }

        if (show_trigger_hint || !state->session_.query().empty()) {
            const bool use_client_preedit =
                ic->capabilityFlags().test(CapabilityFlag::Preedit);
            TextFormatFlags format{
                use_client_preedit ? TextFormatFlag::Underline
                                   : TextFormatFlag::NoFlag};
            Text preedit;
            preedit.append(state->session_.query(), format);
            preedit.setCursor(utf8::length(state->session_.query()));
            if (use_client_preedit) {
                ic->inputPanel().setClientPreedit(preedit);
            } else {
                ic->inputPanel().setPreedit(preedit);
            }
        }

        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    Instance *instance_;
    FzfPickerConfig config_;
    FactoryFor<FzfPickerState> factory_;
    std::vector<std::unique_ptr<HandlerTableEntry<EventHandler>>>
        event_handlers_;
    std::unordered_map<std::string, std::shared_ptr<fzftable::Table>> tables_;
    std::unordered_map<std::string, std::string> table_errors_;
    std::unique_ptr<FzfPickerDBus> dbus_object_;
};

void FzfPickerDBus::triggerEmoji()   { parent_->triggerOnFocused(0); }
void FzfPickerDBus::triggerKaomoji() { parent_->triggerOnFocused(1); }
void FzfPickerDBus::triggerLatex()   { parent_->triggerOnFocused(2); }
void FzfPickerDBus::triggerIpa()     { parent_->triggerOnFocused(3); }

PickerCandidateWord::PickerCandidateWord(FzfPicker *owner,
                                         const fzftable::Match &match)
    : CandidateWord(Text(match.row->value)),
      owner_(owner),
      value_(match.row->value) {
    if (!match.row->comment.empty()) {
        setComment(Text(match.row->comment));
    }
}

void PickerCandidateWord::select(InputContext *ic) const {
    ic->commitString(value_);
    owner_->reset_context(ic);
}

class FzfPickerFactory : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override {
        return new FzfPicker(manager->instance());
    }
};

} // namespace

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(fzfpicker, fcitx::FzfPickerFactory);
