#include "fzfmatch/query.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "fcitx-config/configuration.h"
#include "fcitx-config/iniparser.h"
#include "fcitx-config/option.h"
#include "fcitx-config/rawconfig.h"
#include "fcitx-module/dbus/dbus_public.h"
#include "fcitx-utils/capabilityflags.h"
#include "fcitx-utils/dbus/objectvtable.h"
#include "fcitx-utils/eventloopinterface.h"
#include "fcitx-utils/i18n.h"
#include "fcitx-utils/key.h"
#include "fcitx-utils/keysym.h"
#include "fcitx-utils/log.h"
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

// Clipboard history fuzzy picker.
//
// v0 listener strategy: spawn `wl-paste --watch sh -c '<filter>'` as a
// subprocess. The filter aborts on entries marked with
// x-kde-passwordManagerHint (the same mime-type upstream fcitx5-clipboard
// uses to recognize password-manager copies), otherwise prints the entry
// to stdout NUL-terminated. We read the pipe through the fcitx5 event loop
// and push entries into a bounded LRU history.
//
// This runs side-by-side with the upstream clipboard addon (the user already
// has it; both observe the same wlr_data_control events). The duplication
// is intentional: it lets us iterate on this addon without patching upstream.
// The native wayland listener (replacing the subprocess) is a clean swap
// of the watcher_/spawn logic; the UI and D-Bus surface stay the same.

#ifndef WL_PASTE_PATH
#define WL_PASTE_PATH "wl-paste"
#endif

namespace fcitx {

namespace {

FCITX_DEFINE_LOG_CATEGORY(fzfclipboard_log, "fzfclipboard");

#define CLIP_DEBUG() FCITX_LOGC(fzfclipboard_log, Debug)
#define CLIP_INFO()  FCITX_LOGC(fzfclipboard_log, Info)
#define CLIP_WARN()  FCITX_LOGC(fzfclipboard_log, Warn)

fzfmatch::CaseMode parse_case_mode(std::string_view value) {
    if (value == "Ignore" || value == "ignore") return fzfmatch::CaseMode::Ignore;
    if (value == "Respect" || value == "respect") return fzfmatch::CaseMode::Respect;
    return fzfmatch::CaseMode::Smart;
}

bool has_non_shift_modifier(const Key &key) {
    const auto states = key.states();
    return states.test(KeyState::Ctrl) || states.test(KeyState::Alt) ||
           states.test(KeyState::Super) || states.test(KeyState::Super2) ||
           states.test(KeyState::Hyper) || states.test(KeyState::Meta);
}

std::string text_input_for_key(const Key &key) {
    if (has_non_shift_modifier(key)) return {};
    auto text = Key::keySymToUTF8(key.sym());
    if (text.empty()) return {};
    if (text == "\n" || text == "\r" || text == "\t" || text == "\b" ||
        text == "\x1b" || text == "\x7f") {
        return {};
    }
    return text;
}

// The shell script handed to `wl-paste --watch sh -c <...>`. Runs once per
// clipboard change. Filters out password-manager entries (same mime-type
// upstream uses) and prints the entry NUL-terminated to stdout so the parent
// can read variable-length, newline-containing payloads unambiguously.
constexpr const char *kFilterScript = R"sh(
if "$WL_PASTE" -l 2>/dev/null | grep -qx 'x-kde-passwordManagerHint'; then
    exit 0
fi
content=$("$WL_PASTE" -n 2>/dev/null) || exit 0
[ -z "$content" ] && exit 0
printf '%s\0' "$content"
)sh";

FCITX_CONFIGURATION(
    FzfClipboardConfig,
    Option<int, IntConstrain> historyMax{
        this, "HistoryMax", _("Max history entries"), 100, IntConstrain(1, 1000)};
    Option<int, IntConstrain> pageSize{
        this, "PageSize", _("Page size"), 10, IntConstrain(1, 50)};
    Option<bool> normalize{
        this, "EnableNormalize", _("Normalize characters"), true};
    Option<std::string> caseMode{
        this, "CaseMode", _("Case mode"), "Smart"};
);

class FzfClipboard;

struct MatchedEntry {
    const std::string *text  = nullptr;
    int                score = 0;
};

class FzfClipboardState : public InputContextProperty {
public:
    explicit FzfClipboardState(FzfClipboard *owner) : owner_(owner) {}

    void reset(InputContext *ic) {
        active_  = false;
        query_.clear();
        matches_.clear();
        ic->inputPanel().reset();
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    FzfClipboard *            owner_;
    bool                      active_ = false;
    std::string               query_;
    std::vector<MatchedEntry> matches_;
};

class ClipboardCandidateWord : public CandidateWord {
public:
    ClipboardCandidateWord(FzfClipboard *owner, std::string text);
    void select(InputContext *ic) const override;

private:
    FzfClipboard *owner_;
    std::string   text_;
};

class FzfClipboard;

// D-Bus surface: org.fcitx.Fcitx5 /fzfclipboard
// org.fcitx.Fcitx5.Addon.FzfClipboard1.Trigger
class FzfClipboardDBus : public dbus::ObjectVTable<FzfClipboardDBus> {
public:
    explicit FzfClipboardDBus(FzfClipboard *parent) : parent_(parent) {}

    void trigger();
    void clearAll();
    void clearLast();

    FCITX_OBJECT_VTABLE_METHOD(trigger,   "Trigger",   "", "");
    FCITX_OBJECT_VTABLE_METHOD(clearAll,  "ClearAll",  "", "");
    FCITX_OBJECT_VTABLE_METHOD(clearLast, "ClearLast", "", "");

private:
    FzfClipboard *parent_;
};

class FzfClipboard : public AddonInstance {
    static constexpr char kConfigFile[] = "conf/fzfclipboard.conf";

public:
    FCITX_ADDON_DEPENDENCY_LOADER(dbus, instance_->addonManager());

    explicit FzfClipboard(Instance *instance)
        : instance_(instance),
          factory_(
              [this](InputContext &) { return new FzfClipboardState(this); }) {
        instance_->inputContextManager().registerProperty("fzfClipboardState",
                                                          &factory_);

        // Eats keys when the picker is up.
        event_handlers_.emplace_back(instance_->watchEvent(
            EventType::InputContextKeyEvent, EventWatcherPhase::PreInputMethod,
            [this](Event &event) {
                auto &ke    = static_cast<KeyEvent &>(event);
                auto *state = ke.inputContext()->propertyFor(&factory_);
                if (!state->active_) return;
                on_search_phase(ke);
            }));

        // Picker drops on focus loss / IC reset / IM switch.
        auto resetHandler = [this](Event &event) {
            auto &ice   = static_cast<InputContextEvent &>(event);
            auto *state = ice.inputContext()->propertyFor(&factory_);
            if (state->active_) state->reset(ice.inputContext());
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
        start_watcher();

        dbus_object_ = std::make_unique<FzfClipboardDBus>(this);
        dbus()->call<IDBusModule::bus>()->addObjectVTable(
            "/fzfclipboard", "org.fcitx.Fcitx5.Addon.FzfClipboard1",
            *dbus_object_);
    }

    ~FzfClipboard() override { stop_watcher(); }

    void reloadConfig() override { readAsIni(config_, kConfigFile); }
    const Configuration *getConfig() const override { return &config_; }
    void setConfig(const RawConfig &raw) override {
        config_.load(raw, true);
        safeSaveAsIni(config_, kConfigFile);
    }

    void reset_context(InputContext *ic) {
        ic->propertyFor(&factory_)->reset(ic);
    }

    // D-Bus entry: open picker on whatever IC is focused.
    void triggerOnFocused() {
        auto *ic = instance_->inputContextManager().lastFocusedInputContext();
        if (!ic) {
            CLIP_DEBUG() << "trigger: no focused input context";
            return;
        }
        activate_picker(ic);
    }

    // D-Bus entry: wipe the entire history.
    void clearAllHistory() {
        history_.clear();
        CLIP_INFO() << "history cleared via dbus";
    }

    // D-Bus entry: drop only the most recent entry.
    void clearLastHistory() {
        if (!history_.empty()) {
            history_.pop_front();
            CLIP_INFO() << "last entry cleared via dbus";
        }
    }

private:
    // ----- subprocess listener -----

    void start_watcher() {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
            CLIP_WARN() << "pipe2 failed: " << std::strerror(errno);
            return;
        }
        pid_t pid = fork();
        if (pid < 0) {
            CLIP_WARN() << "fork failed: " << std::strerror(errno);
            close(fds[0]);
            close(fds[1]);
            return;
        }
        if (pid == 0) {
            // Child: stdout -> write end of pipe, then exec wl-paste --watch.
            dup2(fds[1], STDOUT_FILENO);
            close(fds[0]);
            close(fds[1]);
            // Clear FD_CLOEXEC on stdout (dup2 cleared it; explicit just in case)
            int flags = fcntl(STDOUT_FILENO, F_GETFD);
            if (flags != -1) fcntl(STDOUT_FILENO, F_SETFD, flags & ~FD_CLOEXEC);
            setenv("WL_PASTE", WL_PASTE_PATH, 1);
            const char *argv[] = {
                WL_PASTE_PATH, "--watch", "sh", "-c", kFilterScript, nullptr};
            execvp(argv[0], const_cast<char *const *>(argv));
            // execvp returns only on error.
            _exit(127);
        }
        close(fds[1]);
        watcher_pid_ = pid;
        watcher_fd_  = fds[0];
        watcher_event_ = instance_->eventLoop().addIOEvent(
            watcher_fd_, IOEventFlag::In,
            [this](EventSourceIO *, int fd, IOEventFlags flags) {
                if (flags.test(IOEventFlag::Err) ||
                    flags.test(IOEventFlag::Hup)) {
                    CLIP_WARN() << "watcher pipe closed/error; stopping";
                    stop_watcher();
                    return true;
                }
                on_watcher_readable(fd);
                return true;
            });
        CLIP_INFO() << "watcher started, pid=" << watcher_pid_;
    }

    void stop_watcher() {
        watcher_event_.reset();
        if (watcher_fd_ >= 0) {
            close(watcher_fd_);
            watcher_fd_ = -1;
        }
        if (watcher_pid_ > 0) {
            kill(watcher_pid_, SIGTERM);
            int status = 0;
            // Best-effort reap.
            waitpid(watcher_pid_, &status, WNOHANG);
            watcher_pid_ = 0;
        }
    }

    void on_watcher_readable(int fd) {
        char buf[4096];
        while (true) {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n > 0) {
                read_buffer_.insert(read_buffer_.end(), buf, buf + n);
                continue;
            }
            if (n == 0) {
                CLIP_WARN() << "watcher EOF";
                stop_watcher();
                return;
            }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            CLIP_WARN() << "read failed: " << std::strerror(errno);
            stop_watcher();
            return;
        }
        // Slice NUL-terminated records out of the accumulated buffer.
        std::size_t start = 0;
        for (std::size_t i = 0; i < read_buffer_.size(); ++i) {
            if (read_buffer_[i] == '\0') {
                push_history(std::string(read_buffer_.data() + start, i - start));
                start = i + 1;
            }
        }
        if (start > 0) {
            read_buffer_.erase(read_buffer_.begin(),
                               read_buffer_.begin() + start);
        }
    }

    void push_history(std::string text) {
        if (text.empty()) return;
        // Dedup: if already present, move to front.
        auto it = std::find(history_.begin(), history_.end(), text);
        if (it != history_.end()) {
            if (it != history_.begin()) {
                history_.splice(history_.begin(), history_, it);
            }
            return;
        }
        history_.push_front(std::move(text));
        while (history_.size() >
               static_cast<std::size_t>(config_.historyMax.value())) {
            history_.pop_back();
        }
    }

    // ----- picker activation + key handling -----

    void activate_picker(InputContext *ic) {
        auto *state    = ic->propertyFor(&factory_);
        state->active_ = true;
        state->query_.clear();
        rerank(state);
        update_ui(ic, state, /*show_trigger_hint=*/true);
    }

    void on_search_phase(KeyEvent &ke) {
        ke.filter();
        if (ke.isRelease()) return;
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
            // Tab commits the highlighted entry. Enter is intentionally not
            // bound - this picker often opens over a chat app, so an
            // accidental Enter must not send anything.
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

        // Shift+Delete: clear all history (stay in picker so the empty
        // state is visible; user can Esc out).
        if (ke.key().check(FcitxKey_Delete, KeyState::Shift)) {
            ke.accept();
            history_.clear();
            rerank(state);
            update_ui(ic, state, false);
            return;
        }

        // Delete: drop the currently-highlighted entry. Useful when something
        // sensitive made it into the history and you want to scrub just it.
        if (ke.key().check(FcitxKey_Delete)) {
            ke.accept();
            if (auto cl = ic->inputPanel().candidateList(); cl && !cl->empty()) {
                const auto cursor = cl->cursorIndex();
                if (cursor >= 0 && cursor < cl->size() &&
                    cursor < static_cast<int>(state->matches_.size())) {
                    // Copy text out before invalidating matches_/history_ pointers.
                    const std::string target = *state->matches_[cursor].text;
                    history_.remove(target);
                }
            }
            rerank(state);
            update_ui(ic, state, false);
            return;
        }

        if (ke.key().check(FcitxKey_BackSpace)) {
            ke.accept();
            if (!state->query_.empty()) {
                // Remove last UTF-8 codepoint, not last byte.
                auto cp_len =
                    utf8::lengthValidated(state->query_.begin(),
                                          state->query_.end());
                if (cp_len > 0) {
                    auto it = state->query_.end();
                    utf8::nextChar(--it);
                    state->query_.erase(it, state->query_.end());
                }
            }
            rerank(state);
            update_ui(ic, state, false);
            return;
        }

        if (ke.key().isModifier() || has_non_shift_modifier(ke.key())) {
            return;
        }

        auto text = text_input_for_key(ke.key());
        if (text.empty()) return;
        ke.accept();
        state->query_.append(text);
        rerank(state);
        update_ui(ic, state, false);
    }

    fzfmatch::Options match_options() const {
        return {
            .case_mode = parse_case_mode(config_.caseMode.value()),
            .normalize = config_.normalize.value(),
            .forward   = true,
        };
    }

    void rerank(FzfClipboardState *state) {
        state->matches_.clear();
        if (state->query_.empty()) {
            // Empty query: show recency order, top of LRU first.
            state->matches_.reserve(history_.size());
            for (const auto &entry : history_) {
                state->matches_.push_back({&entry, 0});
            }
            return;
        }
        auto query = fzfmatch::Query::parse(state->query_, match_options());
        for (const auto &entry : history_) {
            auto result = query.match(entry);
            if (!result.matched) continue;
            state->matches_.push_back({&entry, result.score});
        }
        std::stable_sort(state->matches_.begin(), state->matches_.end(),
                         [](const MatchedEntry &a, const MatchedEntry &b) {
                             return a.score > b.score;
                         });
    }

    void update_ui(InputContext *ic, FzfClipboardState *state,
                   bool show_trigger_hint) {
        ic->inputPanel().reset();

        Text aux_up(_("FZF Clipboard"));
        if (show_trigger_hint && state->query_.empty()) {
            aux_up.append(
                _(" (filter, Del=drop, Shift+Del=wipe, Esc=cancel)"));
        }
        ic->inputPanel().setAuxUp(aux_up);

        if (history_.empty()) {
            ic->inputPanel().setAuxDown(Text(_("History is empty.")));
        } else if (state->matches_.empty()) {
            ic->inputPanel().setAuxDown(Text(_("No matches.")));
        } else {
            auto cl = std::make_unique<CommonCandidateList>();
            cl->setPageSize(config_.pageSize.value());
            cl->setLayoutHint(CandidateLayoutHint::Vertical);
            cl->setCursorPositionAfterPaging(
                CursorPositionAfterPaging::ResetToFirst);
            for (const auto &m : state->matches_) {
                cl->append<ClipboardCandidateWord>(this, *m.text);
            }
            if (!cl->empty()) cl->setGlobalCursorIndex(0);
            ic->inputPanel().setCandidateList(std::move(cl));
        }

        if (show_trigger_hint || !state->query_.empty()) {
            const bool use_client_preedit =
                ic->capabilityFlags().test(CapabilityFlag::Preedit);
            TextFormatFlags format{use_client_preedit
                                       ? TextFormatFlag::Underline
                                       : TextFormatFlag::NoFlag};
            Text preedit;
            preedit.append(state->query_, format);
            preedit.setCursor(utf8::length(state->query_));
            if (use_client_preedit) {
                ic->inputPanel().setClientPreedit(preedit);
            } else {
                ic->inputPanel().setPreedit(preedit);
            }
        }

        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    Instance *                 instance_;
    FzfClipboardConfig         config_;
    FactoryFor<FzfClipboardState> factory_;
    std::vector<std::unique_ptr<HandlerTableEntry<EventHandler>>>
        event_handlers_;

    // Bounded LRU; front is most recent.
    std::list<std::string> history_;

    // Subprocess + pipe state.
    pid_t                          watcher_pid_ = 0;
    int                            watcher_fd_  = -1;
    std::unique_ptr<EventSourceIO> watcher_event_;
    std::vector<char>              read_buffer_;

    std::unique_ptr<FzfClipboardDBus> dbus_object_;
};

ClipboardCandidateWord::ClipboardCandidateWord(FzfClipboard *owner,
                                               std::string   text)
    : owner_(owner), text_(std::move(text)) {
    // Candidate display: trim to a single line for the popup, but keep the
    // full string in text_ for commit.
    std::string display = text_;
    if (auto nl = display.find_first_of("\r\n"); nl != std::string::npos) {
        display.erase(nl);
        display.append(" ...");
    }
    constexpr std::size_t kDisplayMax = 80;
    if (display.size() > kDisplayMax) {
        display.resize(kDisplayMax);
        display.append(" ...");
    }
    setText(Text(std::move(display)));
}

void ClipboardCandidateWord::select(InputContext *ic) const {
    ic->commitString(text_);
    owner_->reset_context(ic);
}

void FzfClipboardDBus::trigger()   { parent_->triggerOnFocused(); }
void FzfClipboardDBus::clearAll()  { parent_->clearAllHistory(); }
void FzfClipboardDBus::clearLast() { parent_->clearLastHistory(); }

class FzfClipboardFactory : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override {
        return new FzfClipboard(manager->instance());
    }
};

} // namespace

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(fzfclipboard, fcitx::FzfClipboardFactory);
