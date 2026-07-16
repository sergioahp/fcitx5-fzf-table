#include "fzftable/engine.hpp"

#include <algorithm>
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
#include "fcitx-utils/capabilityflags.h"
#include "fcitx-utils/i18n.h"
#include "fcitx-utils/key.h"
#include "fcitx-utils/keysym.h"
#include "fcitx-utils/log.h"
#include "fcitx-utils/standardpaths.h"
#include "fcitx-utils/textformatflags.h"
#include "fcitx-utils/utf8.h"
#include "fcitx/addonfactory.h"
#include "fcitx/addonmanager.h"
#include "fcitx/candidatelist.h"
#include "fcitx/event.h"
#include "fcitx/inputcontext.h"
#include "fcitx/inputcontextmanager.h"
#include "fcitx/inputcontextproperty.h"
#include "fcitx/inputmethodengine.h"
#include "fcitx/inputpanel.h"
#include "fcitx/instance.h"
#include "fcitx/text.h"
#include "fcitx/userinterface.h"

namespace fcitx {

namespace {

FCITX_DEFINE_LOG_CATEGORY(fzftable_log, "fzftable");

struct TableSpec {
    const char *unique_name;
    const char *display_name;
    const char *input_method_name;
    const char *file_name;
    const char *icon;
    const char *label;
    const char *lang_code;
    // When non-zero, the IM stays fully transparent (no preedit, no
    // candidates) until the user types this character. Only that character
    // enters the query buffer; everything else flows to the focused app,
    // including BackSpace for editing previously-typed text. Once a query
    // is in progress, the normal capture-everything behavior takes over.
    // LaTeX uses '\\' to mimic the upstream fcitx5-table-other latex table.
    char trigger_char;
};

constexpr std::array<TableSpec, 5> kTableSpecs{{
    {"fzf-emoji", "Emoji", "Emoji (FZF)", "emoji.tab", "fcitx_emoji", "emo",
     "*", '\0'},
    {"fzf-kaomoji", "Kaomoji", "Kaomoji (FZF)", "kaomoji.tab",
     "input-keyboard", "kao", "*", '\0'},
    {"fzf-latex", "LaTeX", "LaTeX (FZF)", "latex.tab", "fcitx5-fzf-latex",
      "tex", "*", '\\'},
    {"fzf-ipa", "IPA", "IPA (FZF)", "ipa.tab", "input-keyboard", "ipa", "*",
      '\0'},
    {"fzf-typst", "Typst", "Typst (FZF)", "typst.tab", "fcitx5-fzf-typst",
      "typ", "*", '\\'},
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
    FZFTableConfig,
    Option<int, IntConstrain> pageSize{
        this, "PageSize", _("Page size"), 10, IntConstrain(1, 50)};
    Option<bool> normalize{
        this, "EnableNormalize", _("Normalize characters"), true};
    Option<std::string> caseMode{
        this, "CaseMode", _("Case mode"), "Smart"};
);

enum class TypstVariantMode {
    Normal,
    Bold,
    Italic,
    BoldItalic,
    Cal,
    BoldCal,
    Bb,
    Frak,
    BoldFrak,
};

constexpr std::array<TypstVariantMode, 9> kTypstVariants = {
    TypstVariantMode::Normal,     TypstVariantMode::Bold,
    TypstVariantMode::Italic,     TypstVariantMode::BoldItalic,
    TypstVariantMode::Cal,        TypstVariantMode::BoldCal,
    TypstVariantMode::Bb,         TypstVariantMode::Frak,
    TypstVariantMode::BoldFrak,
};

constexpr std::string_view kTypstUniqueName = "fzf-typst";

bool is_typst_spec(const TableSpec *spec) {
    return spec && std::string_view(spec->unique_name) == kTypstUniqueName;
}

std::string_view typst_variant_label(TypstVariantMode mode) {
    switch (mode) {
    case TypstVariantMode::Normal:
        return "normal";
    case TypstVariantMode::Bold:
        return "bold";
    case TypstVariantMode::Italic:
        return "italic";
    case TypstVariantMode::BoldItalic:
        return "bold italic";
    case TypstVariantMode::Cal:
        return "cal";
    case TypstVariantMode::BoldCal:
        return "bold cal";
    case TypstVariantMode::Bb:
        return "bb";
    case TypstVariantMode::Frak:
        return "frak";
    case TypstVariantMode::BoldFrak:
        return "bold frak";
    }
    return "normal";
}

std::string_view typst_variant_keyword(TypstVariantMode mode) {
    switch (mode) {
    case TypstVariantMode::Bold:
        return "@variant:bold";
    case TypstVariantMode::Italic:
        return "@variant:italic";
    case TypstVariantMode::BoldItalic:
        return "@variant:bolditalic";
    case TypstVariantMode::Cal:
        return "@variant:cal";
    case TypstVariantMode::BoldCal:
        return "@variant:boldcal";
    case TypstVariantMode::Bb:
        return "@variant:bb";
    case TypstVariantMode::Frak:
        return "@variant:frak";
    case TypstVariantMode::BoldFrak:
        return "@variant:boldfrak";
    case TypstVariantMode::Normal:
        return {};
    }
    return {};
}

bool typst_variant_transforms_text(TypstVariantMode mode) {
    return mode != TypstVariantMode::Normal;
}

char32_t typst_style_codepoint(unsigned char ch, TypstVariantMode mode) {
    const bool upper = ch >= 'A' && ch <= 'Z';
    const bool lower = ch >= 'a' && ch <= 'z';
    const bool digit = ch >= '0' && ch <= '9';
    const auto alpha_index =
        upper ? ch - 'A' : lower ? ch - 'a' : static_cast<unsigned char>(0);
    const auto digit_index = digit ? ch - '0' : static_cast<unsigned char>(0);

    constexpr std::array<char32_t, 26> kCalUpper = {
        0x1D49C, 0x212C,  0x1D49E, 0x1D49F, 0x2130,  0x2131,  0x1D4A2,
        0x210B,  0x2110,  0x1D4A5, 0x1D4A6, 0x2112,  0x2133,  0x1D4A9,
        0x1D4AA, 0x1D4AB, 0x1D4AC, 0x211B,  0x1D4AE, 0x1D4AF, 0x1D4B0,
        0x1D4B1, 0x1D4B2, 0x1D4B3, 0x1D4B4, 0x1D4B5,
    };
    constexpr std::array<char32_t, 26> kCalLower = {
        0x1D4B6, 0x1D4B7, 0x1D4B8, 0x1D4B9, 0x212F,  0x1D4BB, 0x210A,
        0x1D4BD, 0x1D4BE, 0x1D4BF, 0x1D4C0, 0x1D4C1, 0x1D4C2, 0x1D4C3,
        0x2134,  0x1D4C5, 0x1D4C6, 0x1D4C7, 0x1D4C8, 0x1D4C9, 0x1D4CA,
        0x1D4CB, 0x1D4CC, 0x1D4CD, 0x1D4CE, 0x1D4CF,
    };
    constexpr std::array<char32_t, 26> kBbUpper = {
        0x1D538, 0x1D539, 0x2102,  0x1D53B, 0x1D53C, 0x1D53D, 0x1D53E,
        0x210D,  0x1D540, 0x1D541, 0x1D542, 0x1D543, 0x1D544, 0x2115,
        0x1D546, 0x2119,  0x211A,  0x211D,  0x1D54A, 0x1D54B, 0x1D54C,
        0x1D54D, 0x1D54E, 0x1D54F, 0x1D550, 0x2124,
    };
    constexpr std::array<char32_t, 26> kFrakUpper = {
        0x1D504, 0x1D505, 0x212D,  0x1D507, 0x1D508, 0x1D509, 0x1D50A,
        0x210C,  0x2111,  0x1D50D, 0x1D50E, 0x1D50F, 0x1D510, 0x1D511,
        0x1D512, 0x1D513, 0x1D514, 0x211C,  0x1D516, 0x1D517, 0x1D518,
        0x1D519, 0x1D51A, 0x1D51B, 0x1D51C, 0x2128,
    };

    switch (mode) {
    case TypstVariantMode::Bold:
        if (upper) return 0x1D400 + alpha_index;
        if (lower) return 0x1D41A + alpha_index;
        if (digit) return 0x1D7CE + digit_index;
        break;
    case TypstVariantMode::Italic:
        if (upper) return 0x1D434 + alpha_index;
        if (lower) return ch == 'h' ? 0x210E : 0x1D44E + alpha_index;
        break;
    case TypstVariantMode::BoldItalic:
        if (upper) return 0x1D468 + alpha_index;
        if (lower) return 0x1D482 + alpha_index;
        break;
    case TypstVariantMode::Cal:
        if (upper) return kCalUpper[alpha_index];
        if (lower) return kCalLower[alpha_index];
        break;
    case TypstVariantMode::BoldCal:
        if (upper) return 0x1D4D0 + alpha_index;
        if (lower) return 0x1D4EA + alpha_index;
        break;
    case TypstVariantMode::Bb:
        if (upper) return kBbUpper[alpha_index];
        if (lower) return 0x1D552 + alpha_index;
        if (digit) return 0x1D7D8 + digit_index;
        break;
    case TypstVariantMode::Frak:
        if (upper) return kFrakUpper[alpha_index];
        if (lower) return 0x1D51E + alpha_index;
        break;
    case TypstVariantMode::BoldFrak:
        if (upper) return 0x1D56C + alpha_index;
        if (lower) return 0x1D586 + alpha_index;
        break;
    case TypstVariantMode::Normal:
        break;
    }
    return ch;
}

void append_utf8_codepoint(std::string &out, char32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string typst_transform_text(std::string_view text, TypstVariantMode mode) {
    if (!typst_variant_transforms_text(mode)) {
        return std::string(text);
    }

    std::string out;
    out.reserve(text.size() * 4);
    for (unsigned char ch : text) {
        if (ch < 0x80) {
            append_utf8_codepoint(out, typst_style_codepoint(ch, mode));
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

bool typst_variant_for_direct_key(const Key &key, TypstVariantMode &mode) {
    const auto states = key.states();
    if (!states.test(KeyState::Ctrl) || !states.test(KeyState::Alt)) {
        return false;
    }

    switch (key.sym()) {
    case FcitxKey_0:
        mode = TypstVariantMode::Normal;
        return true;
    case FcitxKey_1:
        mode = TypstVariantMode::Bold;
        return true;
    case FcitxKey_2:
        mode = TypstVariantMode::Italic;
        return true;
    case FcitxKey_3:
        mode = TypstVariantMode::BoldItalic;
        return true;
    case FcitxKey_4:
        mode = TypstVariantMode::Cal;
        return true;
    case FcitxKey_5:
        mode = TypstVariantMode::BoldCal;
        return true;
    case FcitxKey_6:
        mode = TypstVariantMode::Bb;
        return true;
    case FcitxKey_7:
        mode = TypstVariantMode::Frak;
        return true;
    case FcitxKey_8:
        mode = TypstVariantMode::BoldFrak;
        return true;
    default:
        return false;
    }
}

class FZFTableEngine;

class FZFTableState : public InputContextProperty {
public:
    explicit FZFTableState(FZFTableEngine *owner) : owner_(owner) {}

    void clear_query(InputContext *input_context) {
        session_.clear();
        visible_matches_.clear();
        input_context->inputPanel().reset();
        input_context->updatePreedit();
        input_context->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    void reset(InputContext *input_context) {
        active_spec_ = nullptr;
        variant_mode_ = TypstVariantMode::Normal;
        session_.set_table(nullptr);
        clear_query(input_context);
    }

    FZFTableEngine *    owner_;
    const TableSpec *   active_spec_ = nullptr;
    fzftable::Session   session_;
    std::vector<fzftable::Match> visible_matches_;
    TypstVariantMode variant_mode_ = TypstVariantMode::Normal;
};

class TableCandidateWord : public CandidateWord {
public:
    TableCandidateWord(FZFTableEngine *owner, const fzftable::Match &match);
    void select(InputContext *input_context) const override;

private:
    FZFTableEngine *owner_;
    std::string value_;
};

class FZFTableEngine final : public InputMethodEngine {
    static constexpr char kConfigFile[] = "conf/fzftable.conf";

public:
    explicit FZFTableEngine(Instance *instance)
        : instance_(instance),
          factory_([this](InputContext &) { return new FZFTableState(this); }) {
        instance_->inputContextManager().registerProperty("fzfTableState",
                                                          &factory_);
        reloadConfig();
    }

    std::vector<InputMethodEntry> listInputMethods() override {
        std::vector<InputMethodEntry> entries;
        entries.reserve(kTableSpecs.size());
        for (const auto &spec : kTableSpecs) {
            InputMethodEntry entry(spec.unique_name, spec.input_method_name,
                                   spec.lang_code, "fzftable");
            entry.setIcon(spec.icon);
            entry.setLabel(spec.label);
            entry.setConfigurable(true);
            entries.push_back(std::move(entry));
        }
        return entries;
    }

    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override {
        auto *state = event.inputContext()->propertyFor(&factory_);
        sync_state_with_entry(entry, state);
        // Trigger-mode IMs stay transparent on activation - no preedit, no
        // aux line - so they don't visually steal the cursor before the
        // trigger char fires. Non-trigger IMs paint the empty-state UI as
        // they always have.
        if (state->active_spec_ && state->active_spec_->trigger_char != '\0') {
            return;
        }
        update_ui(event.inputContext(), state, true);
    }

    void deactivate(const InputMethodEntry &,
                    InputContextEvent &event) override {
        event.inputContext()->propertyFor(&factory_)->reset(event.inputContext());
    }

    void reset(const InputMethodEntry &, InputContextEvent &event) override {
        event.inputContext()->propertyFor(&factory_)->reset(event.inputContext());
    }

    void keyEvent(const InputMethodEntry &entry, KeyEvent &key_event) override {
        auto *input_context = key_event.inputContext();
        auto *state = input_context->propertyFor(&factory_);
        sync_state_with_entry(entry, state);

        if (key_event.isRelease()) {
            return;
        }

        if (handle_typst_variant_keys(state, key_event)) {
            return;
        }

        // Trigger-mode IMs: while the session is idle, only the configured
        // trigger char is captured. Everything else (letters, BackSpace,
        // Escape, modified keys, etc.) passes through to the focused app -
        // so the LaTeX IM feels like a regular keyboard until you hit `\`.
        // Typst additionally has a styled-typing idle mode: when a style is
        // active, printable text is committed directly after transformation.
        const char trigger =
            state->active_spec_ ? state->active_spec_->trigger_char : '\0';
        if (trigger != '\0' && state->session_.query().empty()) {
            auto text = text_input_for_key(key_event.key());
            if (text.size() == 1 && text[0] == trigger) {
                key_event.filterAndAccept();
                state->session_.append(text);
                rerank_matches(state);
                update_ui(input_context, state, false);
            } else if (is_typst_spec(state->active_spec_) &&
                       typst_variant_transforms_text(state->variant_mode_) &&
                       !text.empty()) {
                key_event.filterAndAccept();
                input_context->commitString(
                    typst_transform_text(text, state->variant_mode_));
                update_ui(input_context, state, false);
            }
            return;
        }

        if (auto candidate_list = input_context->inputPanel().candidateList();
            candidate_list && !candidate_list->empty()) {
            if (key_event.key().checkKeyList(
                    instance_->globalConfig().defaultPrevPage())) {
                key_event.filterAndAccept();
                candidate_list->toPageable()->prev();
                input_context->updateUserInterface(
                    UserInterfaceComponent::InputPanel);
                return;
            }
            if (key_event.key().checkKeyList(
                    instance_->globalConfig().defaultNextPage())) {
                key_event.filterAndAccept();
                candidate_list->toPageable()->next();
                input_context->updateUserInterface(
                    UserInterfaceComponent::InputPanel);
                return;
            }
            if (key_event.key().checkKeyList(
                    instance_->globalConfig().defaultPrevCandidate())) {
                key_event.filterAndAccept();
                candidate_list->toCursorMovable()->prevCandidate();
                input_context->updateUserInterface(
                    UserInterfaceComponent::InputPanel);
                return;
            }
            if (key_event.key().checkKeyList(
                    instance_->globalConfig().defaultNextCandidate())) {
                key_event.filterAndAccept();
                candidate_list->toCursorMovable()->nextCandidate();
                input_context->updateUserInterface(
                    UserInterfaceComponent::InputPanel);
                return;
            }
            // Bare Tab commits the highlighted candidate. Enter is
            // intentionally NOT a commit key: muscle-memory Enter in a chat
            // app would otherwise send a half-typed message the moment the
            // candidate list pops up. Shift+Tab is left alone (it falls
            // through to defaultPrevCandidate / the app).
            if (key_event.key().check(FcitxKey_Tab)) {
                key_event.filterAndAccept();
                const auto cursor = candidate_list->cursorIndex();
                if (cursor >= 0 && cursor < candidate_list->size()) {
                    candidate_list->candidate(cursor).select(input_context);
                }
                return;
            }
        }

        if (key_event.key().check(FcitxKey_Escape)) {
            key_event.filterAndAccept();
            state->clear_query(input_context);
            update_ui(input_context, state, false);
            return;
        }

        if (key_event.key().check(FcitxKey_BackSpace)) {
            key_event.filterAndAccept();
            state->session_.backspace();
            rerank_matches(state);
            update_ui(input_context, state, false);
            return;
        }

        auto text = text_input_for_key(key_event.key());
        if (!text.empty()) {
            key_event.filterAndAccept();
            state->session_.append(text);
            rerank_matches(state);
            update_ui(input_context, state, false);
        }
    }

    void reloadConfig() override { readAsIni(config_, kConfigFile); }

    const Configuration *getConfig() const override { return &config_; }

    void setConfig(const RawConfig &config) override {
        config_.load(config, true);
        safeSaveAsIni(config_, kConfigFile);
    }

    void reset_context(InputContext *input_context) {
        input_context->propertyFor(&factory_)->clear_query(input_context);
    }

private:
    const TableSpec *find_spec(std::string_view unique_name) const {
        for (const auto &spec : kTableSpecs) {
            if (unique_name == spec.unique_name) {
                return &spec;
            }
        }
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
            FCITX_WARN() << table_errors_.at(spec.unique_name);
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
            FCITX_WARN() << "failed to load " << spec.file_name << ": "
                         << error.what();
            return nullptr;
        }
    }

    void sync_state_with_entry(const InputMethodEntry &entry,
                               FZFTableState *         state) {
        const auto *spec = find_spec(entry.uniqueName());
        if (!spec) {
            return;
        }
        if (state->active_spec_ == spec && state->session_.table() != nullptr) {
            return;
        }
        state->active_spec_ = spec;
        state->session_.set_table(ensure_table(*spec));
        state->visible_matches_.clear();
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

    void rerank_matches(FZFTableState *state) const {
        state->session_.rerank(search_options());
        state->visible_matches_ = state->session_.matches();
        if (!is_typst_spec(state->active_spec_)) {
            return;
        }
        const auto preferred = typst_variant_keyword(state->variant_mode_);
        if (preferred.empty()) {
            return;
        }
        std::stable_partition(
            state->visible_matches_.begin(), state->visible_matches_.end(),
            [preferred](const fzftable::Match &match) {
                for (const auto &keyword : match.row->keywords) {
                    if (keyword == preferred) {
                        return true;
                    }
                }
                return false;
            });
    }

    // Typst style cycle is on Ctrl+Tab / Ctrl+Shift+Tab. Direct selection is
    // Ctrl+Alt+0..8, where 0 means normal. Bare Tab is the commit key (see
    // keyEvent), so the cycle chord has to add Ctrl to disambiguate.
    bool handle_typst_variant_keys(FZFTableState *state, KeyEvent &key_event) {
        if (!is_typst_spec(state->active_spec_)) {
            return false;
        }

        TypstVariantMode direct_mode = TypstVariantMode::Normal;
        if (typst_variant_for_direct_key(key_event.key(), direct_mode)) {
            key_event.filterAndAccept();
            state->variant_mode_ = direct_mode;
            rerank_matches(state);
            update_ui(key_event.inputContext(), state, false);
            return true;
        }

        auto advance_variant = [state](int delta) {
            auto iter = std::find(kTypstVariants.begin(), kTypstVariants.end(),
                                  state->variant_mode_);
            auto index = iter == kTypstVariants.end()
                             ? 0
                             : static_cast<int>(iter - kTypstVariants.begin());
            index = (index + delta + static_cast<int>(kTypstVariants.size())) %
                    static_cast<int>(kTypstVariants.size());
            state->variant_mode_ = kTypstVariants[index];
        };

        const bool forward = key_event.key().check(FcitxKey_Tab, KeyState::Ctrl);
        const bool backward =
            key_event.key().check(FcitxKey_Tab,
                                  KeyStates{KeyState::Ctrl, KeyState::Shift}) ||
            key_event.key().check(FcitxKey_ISO_Left_Tab, KeyState::Ctrl);
        if (forward || backward) {
            key_event.filterAndAccept();
            advance_variant(forward ? +1 : -1);
            rerank_matches(state);
            update_ui(key_event.inputContext(), state, false);
            return true;
        }
        return false;
    }

    void update_ui(InputContext *input_context, FZFTableState *state,
                   bool show_empty) {
        input_context->inputPanel().reset();

        const bool typst_styled_idle =
            is_typst_spec(state->active_spec_) &&
            typst_variant_transforms_text(state->variant_mode_) &&
            state->session_.query().empty();
        const bool transparent_idle =
            state->active_spec_ && state->active_spec_->trigger_char != '\0' &&
            state->session_.query().empty() && !show_empty &&
            !typst_styled_idle;
        if (transparent_idle) {
            input_context->updatePreedit();
            input_context->updateUserInterface(UserInterfaceComponent::InputPanel);
            return;
        }

        if (state->active_spec_) {
            Text aux_up(_("FZF Table: "));
            aux_up.append(state->active_spec_->display_name);
            if (is_typst_spec(state->active_spec_)) {
                aux_up.append(" [");
                aux_up.append(std::string(typst_variant_label(state->variant_mode_)));
                aux_up.append("]");
            }
            input_context->inputPanel().setAuxUp(aux_up);

            if (auto error = table_errors_.find(state->active_spec_->unique_name);
                error != table_errors_.end()) {
                input_context->inputPanel().setAuxDown(Text(error->second));
            }
        }

        if (!state->visible_matches_.empty()) {
            auto candidate_list = std::make_unique<CommonCandidateList>();
            candidate_list->setPageSize(config_.pageSize.value());
            candidate_list->setLayoutHint(CandidateLayoutHint::Vertical);
            candidate_list->setCursorPositionAfterPaging(
                CursorPositionAfterPaging::ResetToFirst);
            for (const auto &match : state->visible_matches_) {
                candidate_list->append<TableCandidateWord>(this, match);
            }
            if (!candidate_list->empty()) {
                candidate_list->setGlobalCursorIndex(0);
            }
            input_context->inputPanel().setCandidateList(
                std::move(candidate_list));
        }

        if (show_empty || !state->session_.query().empty()) {
            const bool use_client_preedit = input_context->capabilityFlags().test(
                CapabilityFlag::Preedit);
            TextFormatFlags format{
                use_client_preedit ? TextFormatFlag::Underline
                                   : TextFormatFlag::NoFlag};
            Text preedit;
            preedit.append(state->session_.query(), format);
            preedit.setCursor(utf8::length(state->session_.query()));
            if (use_client_preedit) {
                input_context->inputPanel().setClientPreedit(preedit);
            } else {
                input_context->inputPanel().setPreedit(preedit);
            }
        }

        input_context->updatePreedit();
        input_context->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    Instance *instance_;
    FZFTableConfig config_;
    FactoryFor<FZFTableState> factory_;
    std::unordered_map<std::string, std::shared_ptr<fzftable::Table>> tables_;
    std::unordered_map<std::string, std::string> table_errors_;
};

TableCandidateWord::TableCandidateWord(FZFTableEngine *owner,
                                       const fzftable::Match &match)
    : CandidateWord(Text(match.row->value)),
      owner_(owner),
      value_(match.row->value) {
    if (!match.row->comment.empty()) {
        setComment(Text(match.row->comment));
    }
}

void TableCandidateWord::select(InputContext *input_context) const {
    input_context->commitString(value_);
    owner_->reset_context(input_context);
}

class FZFTableEngineFactory : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override {
        return new FZFTableEngine(manager->instance());
    }
};

} // namespace

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(fzftable, fcitx::FZFTableEngineFactory);
