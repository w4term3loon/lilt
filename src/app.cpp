// SPDX-License-Identifier: GPL-3.0-or-later
#include "app.hpp"
#include "migration.hpp"
#include "text.hpp"
#include <algorithm>
#include <filesystem>
#include <functional>
#include <fstream>
#include <thread>
#include <unistd.h>
#include <csignal>

namespace ren {
// GApplication must export the dictation interface before acquiring its bus
// name: the first activated call can arrive before the startup signal.
struct NativeApplication {
    GtkApplication parent_instance;
    App* owner;
    static gboolean register_bus(GApplication*, GDBusConnection*, const gchar*, GError**);
    static void unregister_bus(GApplication*, GDBusConnection*, const gchar*);
};
struct NativeApplicationClass { GtkApplicationClass parent_class; };
G_DEFINE_TYPE(NativeApplication, native_application, GTK_TYPE_APPLICATION)

static void native_application_init(NativeApplication* self) { self->owner = nullptr; }
static void native_application_class_init(NativeApplicationClass* klass) {
    auto* application_class = G_APPLICATION_CLASS(klass);
    application_class->dbus_register = NativeApplication::register_bus;
    application_class->dbus_unregister = NativeApplication::unregister_bus;
}
gboolean NativeApplication::register_bus(GApplication* application, GDBusConnection* connection,
                                        const gchar* path, GError** error) {
    auto* parent = G_APPLICATION_CLASS(native_application_parent_class);
    if (!parent->dbus_register(application, connection, path, error)) return FALSE;
    if (reinterpret_cast<NativeApplication*>(application)->owner->register_bus(connection, error)) return TRUE;
    parent->dbus_unregister(application, connection, path);
    return FALSE;
}
void NativeApplication::unregister_bus(GApplication* application, GDBusConnection* connection, const gchar* path) {
    reinterpret_cast<NativeApplication*>(application)->owner->unregister_bus();
    G_APPLICATION_CLASS(native_application_parent_class)->dbus_unregister(application, connection, path);
}

namespace {
constexpr auto kPath = "/io/github/ren/Dictation";
constexpr auto kInterface = "io.github.ren.Dictation";
constexpr struct { const char* id; const char* title; guint64 bytes; } kModels[] = {
    {"tiny.en-q5_1", "Tiny", 32166155}, {"base.en-q5_1", "Base", 59721011},
    {"small.en-q5_1", "Small", 190098681}, {"medium.en-q5_0", "Medium", 539225533},
};
constexpr auto kXml = R"(<node><interface name="io.github.ren.Dictation">
<method name="Toggle"/><method name="Stop"/><method name="Cancel"/>
<method name="ShowPreferences"/><method name="Quit"/><method name="Attach"/><method name="Detach"/>
<method name="GetLastTranscript"><arg type="s" direction="out" name="text"/></method>
<method name="ReportError"><arg type="s" direction="in" name="message"/></method>
<property name="State" type="s" access="read"/><property name="Level" type="d" access="read"/>
<property name="Message" type="s" access="read"/><property name="Shortcut" type="s" access="read"/>
<property name="FinishShortcut" type="s" access="read"/>
<property name="LivePreview" type="b" access="read"/>
<property name="CopyToClipboard" type="b" access="read"/>
<property name="VoiceCommands" type="b" access="read"/>
<property name="HasTranscript" type="b" access="read"/><property name="InputWarning" type="s" access="read"/>
<signal name="PartialTranscript"><arg type="s" name="text"/><arg type="u" name="stable_bytes"/></signal>
<signal name="Transcript"><arg type="s" name="text"/></signal>
</interface></node>)";
std::string take(gchar* s) { std::string out = s ? s : ""; g_free(s); return out; }
bool modifier_key(guint key) {
    switch (key) {
    case GDK_KEY_Shift_L: case GDK_KEY_Shift_R:
    case GDK_KEY_Control_L: case GDK_KEY_Control_R:
    case GDK_KEY_Alt_L: case GDK_KEY_Alt_R:
    case GDK_KEY_Super_L: case GDK_KEY_Super_R:
    case GDK_KEY_Meta_L: case GDK_KEY_Meta_R:
    case GDK_KEY_Hyper_L: case GDK_KEY_Hyper_R:
    case GDK_KEY_Caps_Lock: case GDK_KEY_Shift_Lock: case GDK_KEY_Num_Lock:
    case GDK_KEY_ISO_Level3_Shift: case GDK_KEY_Mode_switch:
        return true;
    default: return false;
    }
}
bool valid_shortcut(guint key, GdkModifierType mods, bool finish) {
    if (!key || key == GDK_KEY_VoidSymbol || key == GDK_KEY_Escape || modifier_key(key)) return false;
    constexpr guint allowed = GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_MOD4_MASK;
    if (static_cast<guint>(mods) & ~allowed) return false;
    if (finish) return true; // Only captured during recording, so a plain key is fine.
    return (mods & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_MOD4_MASK)) &&
        gtk_accelerator_valid(key, mods) && key != GDK_KEY_Return && key != GDK_KEY_KP_Enter;
}
std::string shortcut_label(const std::string& shortcut) {
    guint key; GdkModifierType mods;
    gtk_accelerator_parse(shortcut.c_str(), &key, &mods);
    if (key == GDK_KEY_Return && !mods) return "Enter";
    return take(gtk_accelerator_get_label(key, mods));
}
GtkWidget* label(const char* text, const char* css = nullptr) {
    auto* w = gtk_label_new(text);
    gtk_widget_set_halign(w, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(w), TRUE);
    gtk_label_set_xalign(GTK_LABEL(w), 0);
    if (css) gtk_style_context_add_class(gtk_widget_get_style_context(w), css);
    return w;
}
void pack(GtkWidget* box, GtkWidget* child, int padding = 0) {
    gtk_box_pack_start(GTK_BOX(box), child, FALSE, FALSE, padding);
}
}

App::App() : alive_(std::make_shared<std::atomic_bool>(true)) {
    config_path_ = std::string(g_get_user_config_dir()) + "/ren/config.ini";
    data_path_ = std::string(g_get_user_data_dir()) + "/ren";
    const auto migration_error = detail::migrate_legacy_state(g_get_user_config_dir(), g_get_user_data_dir());
    if (!migration_error.empty()) {
        g_printerr("ren: Legacy settings/model migration could not finish: %s\n", migration_error.c_str());
        set_state("error", "Could not import previous settings or models: " + migration_error);
    }
    char buffer[4096];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len > 0) { buffer[len] = 0; executable_ = buffer; }
    load_config();
}
App::~App() {
    *alive_ = false;
    engine_.cancel();
    if (download_) { g_subprocess_send_signal(download_, SIGTERM); g_clear_object(&download_); }
    g_clear_object(&app_);
    unregister_bus();
    if (info_) g_dbus_node_info_unref(info_);
}

int App::run(int argc, char** argv) {
    auto* native = reinterpret_cast<NativeApplication*>(g_object_new(native_application_get_type(),
        "application-id", kInterface, "flags", G_APPLICATION_HANDLES_COMMAND_LINE, nullptr));
    native->owner = this;
    app_ = GTK_APPLICATION(native);
    const GOptionEntry entries[] = {
        {"daemon", 0, 0, G_OPTION_ARG_NONE, nullptr, "Run quietly in the top bar", nullptr},
        {"toggle", 0, 0, G_OPTION_ARG_NONE, nullptr, "Start or finish dictation", nullptr},
        {"stop", 0, 0, G_OPTION_ARG_NONE, nullptr, "Finish recording", nullptr},
        {"cancel", 0, 0, G_OPTION_ARG_NONE, nullptr, "Discard recording", nullptr},
        {"quit", 0, 0, G_OPTION_ARG_NONE, nullptr, "Quit ren", nullptr},
        {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr}
    };
    g_application_add_main_option_entries(G_APPLICATION(app_), entries);
    g_signal_connect(app_, "startup", G_CALLBACK(+[](GApplication*, gpointer d) { static_cast<App*>(d)->startup(); }), this);
    g_signal_connect(app_, "activate", G_CALLBACK(+[](GApplication*, gpointer d) { static_cast<App*>(d)->show(); }), this);
    g_signal_connect(app_, "command-line", G_CALLBACK(+[](GApplication*, GApplicationCommandLine* cmd, gpointer d) -> int {
        auto* self = static_cast<App*>(d);
        auto* options = g_application_command_line_get_options_dict(cmd);
        if (g_variant_dict_contains(options, "quit")) { self->cancel_session(); g_application_quit(G_APPLICATION(self->app_)); }
        else if (g_variant_dict_contains(options, "toggle")) self->toggle();
        else if (g_variant_dict_contains(options, "stop")) self->engine_.stop();
        else if (g_variant_dict_contains(options, "cancel")) self->cancel_session();
        else if (!g_variant_dict_contains(options, "daemon")) self->show();
        return 0;
    }), this);
    return g_application_run(G_APPLICATION(app_), argc, argv);
}

void App::startup() {
    g_application_hold(G_APPLICATION(app_));
    // GtkApplication has initialized the display by this point. Accelerator
    // parsing can consult its keymap and is not safe in the App constructor.
    guint key; GdkModifierType mods;
    gtk_accelerator_parse(shortcut_.c_str(), &key, &mods);
    if (!valid_shortcut(key, mods, false)) shortcut_ = "<Control><Super>space";
    gtk_accelerator_parse(finish_shortcut_.c_str(), &key, &mods);
    if (!valid_shortcut(key, mods, true)) finish_shortcut_ = "Return";
    publish();
}

bool App::register_bus(GDBusConnection* connection, GError** error) {
    bus_ = G_DBUS_CONNECTION(g_object_ref(connection));
    if (!info_) info_ = g_dbus_node_info_new_for_xml(kXml, error);
    if (!info_) { unregister_bus(); return false; }
    static const GDBusInterfaceVTable vtable = { method_call, get_property, nullptr, {nullptr} };
    registration_ = g_dbus_connection_register_object(bus_, kPath, info_->interfaces[0], &vtable, this, nullptr, error);
    if (!registration_) {
        unregister_bus();
        return false;
    }
    owner_watch_ = g_dbus_connection_signal_subscribe(bus_, "org.freedesktop.DBus", "org.freedesktop.DBus",
        "NameOwnerChanged", "/org/freedesktop/DBus", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        +[](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* args, gpointer d) {
            auto* self = static_cast<App*>(d);
            const char *name, *old_owner, *new_owner;
            g_variant_get(args, "(&s&s&s)", &name, &old_owner, &new_owner);
            if (self->shell_owner_ == name && !*new_owner) {
                self->shell_owner_.clear(); self->cancel_session(); self->refresh();
            }
        }, this, nullptr);
    return true;
}

void App::unregister_bus() {
    if (bus_ && registration_) g_dbus_connection_unregister_object(bus_, registration_);
    if (bus_ && owner_watch_) g_dbus_connection_signal_unsubscribe(bus_, owner_watch_);
    registration_ = owner_watch_ = 0;
    g_clear_object(&bus_);
}

void App::dispatch(std::function<void()> fn) {
    struct Task { std::shared_ptr<std::atomic_bool> alive; std::function<void()> fn; };
    g_idle_add_full(G_PRIORITY_DEFAULT, +[](gpointer d) -> gboolean {
        auto* task = static_cast<Task*>(d);
        if (*task->alive) task->fn();
        return G_SOURCE_REMOVE;
    }, new Task{alive_, std::move(fn)}, +[](gpointer d) { delete static_cast<Task*>(d); });
}

void App::toggle() {
    if (state_ == "recording") { engine_.stop(); return; }
    if (engine_.busy() || download_) { publish(); return; }
    if (shell_owner_.empty()) {
        set_state("error", "Enable the ren GNOME extension, then log out and back in if needed.");
        show(); return;
    }
    if (!g_file_test(model_path().c_str(), G_FILE_TEST_IS_REGULAR)) {
        set_state("error", "Choose or download a voice model first."); show(); return;
    }
    const auto generation = ++generation_;
    const auto owner = shell_owner_;
    last_transcript_.clear();
    input_warning_.clear();
    microphone_name_.clear();
    microphone_muted_ = heard_input_ = false;
    quiet_levels_ = 0;
    if (window_) gtk_widget_hide(window_);
    Engine::Callbacks cb;
    cb.on_state = [this, generation](std::string state, std::string message) {
        dispatch([this, generation, state, message] { if (generation == generation_) set_state(state, message); else if (state == "idle" || state == "error") { refresh(); publish(); } });
    };
    cb.on_level = [this, generation](double level) {
        dispatch([this, generation, level] {
            if (generation == generation_ && state_ == "recording") {
                if (level >= 0.001) heard_input_ = true;
                else if (quiet_levels_ < 60) ++quiet_levels_;
                if (update_input_warning()) refresh();
                level_ = level; publish();
            }
        });
    };
    cb.on_microphone = [this, generation](std::string name, bool muted) {
        dispatch([this, generation, name = std::move(name), muted] {
            if (generation != generation_) return;
            if ((!microphone_name_.empty() && microphone_name_ != name) || microphone_muted_ != muted) {
                heard_input_ = false; quiet_levels_ = 0;
            }
            microphone_name_ = name;
            microphone_muted_ = muted;
            if (update_input_warning()) { refresh(); publish(); }
        });
    };
    cb.on_partial = [this, generation, owner](std::string text, std::size_t stable_bytes) {
        dispatch([this, generation, owner, text = std::move(text), stable_bytes] {
            if (generation != generation_ || shell_owner_ != owner || owner.empty()) return;
            if (state_ != "recording" && state_ != "transcribing") return;
            const auto prefix = static_cast<guint32>(std::min(stable_bytes, text.size()));
            g_dbus_connection_emit_signal(bus_, owner.c_str(), kPath, kInterface,
                "PartialTranscript", g_variant_new("(su)", text.c_str(), prefix), nullptr);
        });
    };
    cb.on_result = [this, generation, owner, commands = voice_commands_](std::string text) {
        dispatch([this, generation, owner, commands, text] {
            if (generation != generation_ || shell_owner_ != owner || owner.empty()) return;
            if (!commands || !g_regex_match_simple("^open browser(?:[\\s.!?,]|$)", text.c_str(), G_REGEX_CASELESS, G_REGEX_MATCH_DEFAULT))
                last_transcript_ = text;
            if (!text.empty()) g_dbus_connection_emit_signal(bus_, owner.c_str(),
                kPath, kInterface, "Transcript", g_variant_new("(s)", text.c_str()), nullptr);
        });
    };
    set_state("loading", "Opening microphone…");
    try {
        engine_.start(model_path(), std::max(1u, std::min(4u, std::thread::hardware_concurrency())),
                      std::move(cb), true, vocabulary_);
    } catch (const std::exception& error) { set_state("error", error.what()); }
}

void App::cancel_session(bool discard_transcript) {
    ++generation_;
    engine_.cancel();
    if (discard_transcript) last_transcript_.clear();
    input_warning_.clear();
    set_state("idle", "Cancelled");
}

bool App::update_input_warning() {
    if (state_ != "recording") return false;
    const auto name = microphone_name_.empty() ? "Microphone" : microphone_name_;
    const auto warning = microphone_muted_ ? name + " is muted." :
        !heard_input_ && quiet_levels_ >= 60 ? "Very little sound from " + name + "." : "";
    if (warning == input_warning_) return false;
    input_warning_ = warning;
    return true;
}

void App::set_state(const std::string& state, const std::string& message) {
    state_ = state; message_ = message;
    update_input_warning();
    if (state != "recording") level_ = 0;
    publish(); refresh();
}
void App::publish() {
    if (!bus_ || !registration_) return;
    GVariantBuilder changed, invalidated;
    g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&changed, "{sv}", "State", g_variant_new_string(state_.c_str()));
    g_variant_builder_add(&changed, "{sv}", "Message", g_variant_new_string(message_.c_str()));
    g_variant_builder_add(&changed, "{sv}", "Level", g_variant_new_double(level_));
    g_variant_builder_add(&changed, "{sv}", "Shortcut", g_variant_new_string(shortcut_.c_str()));
    g_variant_builder_add(&changed, "{sv}", "FinishShortcut", g_variant_new_string(finish_shortcut_.c_str()));
    g_variant_builder_add(&changed, "{sv}", "LivePreview", g_variant_new_boolean(live_preview_));
    g_variant_builder_add(&changed, "{sv}", "CopyToClipboard", g_variant_new_boolean(copy_to_clipboard_));
    g_variant_builder_add(&changed, "{sv}", "VoiceCommands", g_variant_new_boolean(voice_commands_));
    g_variant_builder_add(&changed, "{sv}", "HasTranscript", g_variant_new_boolean(!last_transcript_.empty()));
    g_variant_builder_add(&changed, "{sv}", "InputWarning", g_variant_new_string(input_warning_.c_str()));
    g_variant_builder_init(&invalidated, G_VARIANT_TYPE("as"));
    g_dbus_connection_emit_signal(bus_, nullptr, kPath, "org.freedesktop.DBus.Properties", "PropertiesChanged",
        g_variant_new("(sa{sv}as)", kInterface, &changed, &invalidated), nullptr);
}
GVariant* App::get_property(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* property, GError**, gpointer d) {
    auto* self = static_cast<App*>(d);
    if (g_str_equal(property, "State")) return g_variant_new_string(self->state_.c_str());
    if (g_str_equal(property, "Message")) return g_variant_new_string(self->message_.c_str());
    if (g_str_equal(property, "Shortcut")) return g_variant_new_string(self->shortcut_.c_str());
    if (g_str_equal(property, "FinishShortcut")) return g_variant_new_string(self->finish_shortcut_.c_str());
    if (g_str_equal(property, "LivePreview")) return g_variant_new_boolean(self->live_preview_);
    if (g_str_equal(property, "CopyToClipboard")) return g_variant_new_boolean(self->copy_to_clipboard_);
    if (g_str_equal(property, "VoiceCommands")) return g_variant_new_boolean(self->voice_commands_);
    if (g_str_equal(property, "HasTranscript")) return g_variant_new_boolean(!self->last_transcript_.empty());
    if (g_str_equal(property, "InputWarning")) return g_variant_new_string(self->input_warning_.c_str());
    if (g_str_equal(property, "Level")) return g_variant_new_double(self->level_);
    return nullptr;
}
void App::method_call(GDBusConnection*, const gchar* sender, const gchar*, const gchar*, const gchar* method,
                     GVariant* args, GDBusMethodInvocation* invocation, gpointer d) {
    auto* self = static_cast<App*>(d);
    if (g_str_equal(method, "Attach")) {
        GVariant* owner = g_dbus_connection_call_sync(self->bus_, "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", "GetNameOwner", g_variant_new("(s)", "org.gnome.Shell"), G_VARIANT_TYPE("(s)"),
            G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
        const char* name = "";
        if (owner) g_variant_get(owner, "(&s)", &name);
        bool valid = g_str_equal(sender, name);
        if (owner) g_variant_unref(owner);
        if (!valid) { g_dbus_method_invocation_return_dbus_error(invocation, "io.github.ren.Error", "Only GNOME Shell can attach."); return; }
        self->shell_owner_ = sender; self->refresh();
    } else if (g_str_equal(method, "Detach")) {
        if (self->shell_owner_ == sender) { self->shell_owner_.clear(); self->cancel_session(); self->refresh(); }
    } else if (g_str_equal(method, "ReportError")) {
        if (self->shell_owner_ == sender) {
            const char* message; g_variant_get(args, "(&s)", &message);
            self->cancel_session(false); self->set_state("error", message); self->show();
        }
    } else if (g_str_equal(method, "GetLastTranscript")) {
        if (self->shell_owner_ != sender) {
            g_dbus_method_invocation_return_dbus_error(invocation, "io.github.ren.Error", "Only the attached shell can copy dictation.");
            return;
        }
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(s)", self->engine_.busy() ? "" : self->last_transcript_.c_str()));
        return;
    } else if (g_str_equal(method, "Toggle")) self->toggle();
    else if (g_str_equal(method, "Stop")) self->engine_.stop();
    else if (g_str_equal(method, "Cancel")) self->cancel_session();
    else if (g_str_equal(method, "ShowPreferences")) self->show();
    else if (g_str_equal(method, "Quit")) { self->cancel_session(); g_application_quit(G_APPLICATION(self->app_)); }
    g_dbus_method_invocation_return_value(invocation, nullptr);
}

std::string App::model_path() const { return model_ == "custom" ? custom_model_ : data_path_ + "/models/ggml-" + model_ + ".bin"; }
void App::load_config() {
    GKeyFile* file = g_key_file_new();
    const bool loaded = g_key_file_load_from_file(file, config_path_.c_str(), G_KEY_FILE_NONE, nullptr);
    bool migrate = false;
    if (loaded) {
        // Preserve settings from the old project name and the interim branding.
        const char* group = "ren";
        for (const char* candidate : {"ren", "lilt", "LILT", "PTT"}) {
            if (g_key_file_has_group(file, candidate)) { group = candidate; break; }
        }
        auto read = [&](const char* key, std::string& out) { auto v = take(g_key_file_get_string(file, group, key, nullptr)); if (!v.empty()) out = v; };
        read("custom_model", custom_model_); read("model", model_); read("shortcut", shortcut_);
        migrate = std::string(group) != "ren" || take(g_key_file_get_string(file, group, "language", nullptr)) != "en";
        read("finish_shortcut", finish_shortcut_);
        read("vocabulary", vocabulary_);
        auto read_flag = [&](const char* key, bool& out) {
            GError* error = nullptr;
            const bool value = g_key_file_get_boolean(file, group, key, &error);
            if (!error) out = value;
            g_clear_error(&error);
        };
        read_flag("live_preview", live_preview_);
        read_flag("copy_to_clipboard", copy_to_clipboard_);
        read_flag("voice_commands", voice_commands_);
    }
    const auto previous_model = model_;
    vocabulary_ = normalize_vocabulary(vocabulary_);
    const std::string legacy[] = {"tiny-q5_1", "base-q5_1", "small-q5_1", "medium-q5_0"};
    if (std::find(std::begin(legacy), std::end(legacy), model_) != std::end(legacy))
        model_.insert(model_.find('-'), ".en");
    if (model_ != "custom" && std::none_of(std::begin(kModels), std::end(kModels),
        [this](const auto& model) { return model_ == model.id; })) model_ = "small.en-q5_1";
    g_key_file_unref(file);
    if (loaded && (migrate || model_ != previous_model)) save_config();
}
void App::save_config() {
    g_mkdir_with_parents((std::string(g_get_user_config_dir()) + "/ren").c_str(), 0700);
    GKeyFile* file = g_key_file_new();
    g_key_file_set_string(file, "ren", "model", model_.c_str());
    g_key_file_set_string(file, "ren", "custom_model", custom_model_.c_str());
    g_key_file_set_string(file, "ren", "language", "en");
    g_key_file_set_string(file, "ren", "shortcut", shortcut_.c_str());
    g_key_file_set_string(file, "ren", "finish_shortcut", finish_shortcut_.c_str());
    g_key_file_set_boolean(file, "ren", "live_preview", live_preview_);
    g_key_file_set_boolean(file, "ren", "copy_to_clipboard", copy_to_clipboard_);
    g_key_file_set_boolean(file, "ren", "voice_commands", voice_commands_);
    g_key_file_set_string(file, "ren", "vocabulary", vocabulary_.c_str());
    GError* error = nullptr;
    if (!g_key_file_save_to_file(file, config_path_.c_str(), &error)) {
        set_state("error", error->message); g_clear_error(&error);
    }
    g_key_file_unref(file); publish();
}

void App::show() {
    if (!window_) build_ui();
    refresh(); gtk_widget_show_all(window_);
    gtk_widget_set_visible(recovery_, state_ == "error" && !last_transcript_.empty());
    gtk_window_resize(GTK_WINDOW(window_), 350, 1);
    gtk_window_present(GTK_WINDOW(window_));
}
void App::build_ui() {
    building_ui_ = true;
    window_ = gtk_application_window_new(app_);
    gtk_window_set_title(GTK_WINDOW(window_), "Ren");
    gtk_window_set_default_size(GTK_WINDOW(window_), 350, 1);
    gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);
    gtk_window_set_icon_name(GTK_WINDOW(window_), kInterface);
    gtk_style_context_add_class(gtk_widget_get_style_context(window_), "ren-preferences");
    auto* header = gtk_header_bar_new();
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Ren");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(window_), header);
    g_signal_connect(window_, "delete-event", G_CALLBACK(+[](GtkWidget* w, GdkEvent*, gpointer) -> gboolean { gtk_widget_hide(w); return TRUE; }), this);
    auto* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, R"(
        .ren-preferences .ren-card {
            background-color: @theme_base_color;
            border: 1px solid alpha(@theme_fg_color, 0.08);
            border-radius: 16px;
            padding: 16px;
            box-shadow: 0 2px 6px alpha(@theme_fg_color, 0.03);
        }
        .ren-preferences .ren-field { font-weight: 600; }
        .ren-preferences .ren-status { font-size: 0.92em; }
        .ren-preferences .ren-divider { background-color: alpha(@theme_fg_color, 0.08); }
        .ren-model-details .ren-title { font-size: 1.25em; font-weight: 600; }
        .ren-model-details .ren-subtitle { font-family: monospace; font-size: 0.92em; }
        .ren-model-details .ren-secondary { color: alpha(@theme_fg_color, 0.78); }
        .ren-model-details .ren-source-link { padding: 0; min-height: 24px; }
    )", -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_container_set_border_width(GTK_CONTAINER(box), 24);
    gtk_container_add(GTK_CONTAINER(window_), box);
    status_label_ = label("", "ren-status");
    gtk_widget_set_no_show_all(status_label_, TRUE);
    gtk_widget_set_size_request(status_label_, 308, -1);
    gtk_widget_set_halign(status_label_, GTK_ALIGN_CENTER);
    gtk_label_set_xalign(GTK_LABEL(status_label_), 0.5);
    gtk_label_set_justify(GTK_LABEL(status_label_), GTK_JUSTIFY_CENTER);
    auto card = [&] {
        auto* grid = gtk_grid_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(grid), "ren-card");
        gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 24);
        pack(box, grid);
        return grid;
    };
    auto row = [&](GtkWidget* grid, const char* title, GtkWidget* control, int position) {
        auto* field = label(title, "ren-field");
        gtk_widget_set_size_request(field, 72, 36);
        gtk_widget_set_valign(field, GTK_ALIGN_CENTER);
        gtk_widget_set_size_request(control, 168, -1);
        gtk_widget_set_hexpand(control, TRUE);
        gtk_grid_attach(GTK_GRID(grid), field, 0, position, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), control, 1, position, 1, 1);
        gtk_label_set_mnemonic_widget(GTK_LABEL(field), control);
    };
    auto* model_card = card();
    auto* model_label = label("Model", "ren-field");
    gtk_widget_set_valign(model_label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(model_card), model_label, 0, 0, 1, 1);
    model_details_ = gtk_button_new_with_label("Model details");
    gtk_button_set_relief(GTK_BUTTON(model_details_), GTK_RELIEF_NONE);
    gtk_widget_set_halign(model_details_, GTK_ALIGN_END);
    gtk_widget_set_hexpand(model_details_, TRUE);
    gtk_grid_attach(GTK_GRID(model_card), model_details_, 1, 0, 1, 1);
    g_signal_connect(model_details_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) { static_cast<App*>(d)->show_model_details(); }), this);
    model_combo_ = gtk_combo_box_text_new();
    for (const auto& model : kModels)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(model_combo_), model.id, model.title);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(model_combo_), "custom", "Custom file…");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(model_combo_), model_.c_str());
    GList* cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(model_combo_));
    for (GList* cell = cells; cell; cell = cell->next) g_object_set(cell->data, "xalign", 0.0f, nullptr);
    g_list_free(cells);
    gtk_grid_attach(GTK_GRID(model_card), model_combo_, 0, 1, 2, 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(model_label), model_combo_);
    download_button_ = gtk_button_new_with_label("Download model");
    gtk_widget_set_no_show_all(download_button_, TRUE);
    gtk_grid_attach(GTK_GRID(model_card), download_button_, 0, 2, 2, 1);
    auto* grid = card();
    shortcut_button_ = gtk_button_new();
    row(grid, "Start", shortcut_button_, 0);
    finish_button_ = gtk_button_new();
    row(grid, "Finish", finish_button_, 1);
    auto switch_row = [&](const char* title, bool active, int position) {
        auto* control = gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(control), active);
        gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
        auto* field = label(title, "ren-field");
        gtk_widget_set_size_request(field, -1, 36);
        gtk_widget_set_valign(field, GTK_ALIGN_CENTER);
        gtk_label_set_mnemonic_widget(GTK_LABEL(field), control);
        auto* line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_pack_start(GTK_BOX(line), field, TRUE, TRUE, 0);
        gtk_box_pack_end(GTK_BOX(line), control, FALSE, FALSE, 0);
        gtk_grid_attach(GTK_GRID(grid), line, 0, position, 2, 1);
        return control;
    };
    preview_switch_ = switch_row("Live text", live_preview_, 2);
    gtk_widget_set_tooltip_text(preview_switch_, "Show text while you speak.");
    copy_switch_ = switch_row("Copy to clipboard", copy_to_clipboard_, 3);
    gtk_widget_set_tooltip_text(copy_switch_, "Copy finished text when there is no writable text field.");
    commands_switch_ = switch_row("Voice commands", voice_commands_, 4);
    gtk_widget_set_tooltip_text(commands_switch_, "Recognize “open browser” as a command instead of dictation.");
    auto* divider = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_style_context_add_class(gtk_widget_get_style_context(divider), "ren-divider");
    gtk_grid_attach(GTK_GRID(grid), divider, 0, 5, 2, 1);
    auto* vocabulary_label = label("Vocabulary", "ren-field");
    gtk_grid_attach(GTK_GRID(grid), vocabulary_label, 0, 6, 2, 1);
    vocabulary_entry_ = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(vocabulary_entry_), 512);
    gtk_entry_set_width_chars(GTK_ENTRY(vocabulary_entry_), 14);
    gtk_entry_set_alignment(GTK_ENTRY(vocabulary_entry_), 0);
    gtk_entry_set_placeholder_text(GTK_ENTRY(vocabulary_entry_), "Names, terms…");
    gtk_entry_set_text(GTK_ENTRY(vocabulary_entry_), vocabulary_.c_str());
    gtk_widget_set_tooltip_text(vocabulary_entry_, "Optional words to help recognition. Stored locally.");
    gtk_grid_attach(GTK_GRID(grid), vocabulary_entry_, 0, 7, 2, 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(vocabulary_label), vocabulary_entry_);
    pack(box, status_label_);
    sound_settings_ = gtk_button_new_with_label("Open Sound Settings");
    gtk_widget_set_halign(sound_settings_, GTK_ALIGN_CENTER);
    gtk_widget_set_no_show_all(sound_settings_, TRUE);
    pack(box, sound_settings_);
    g_signal_connect(sound_settings_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
        GError* error = nullptr;
        auto* info = g_app_info_create_from_commandline("gnome-control-center sound", "Sound Settings", G_APP_INFO_CREATE_NONE, &error);
        if (info) { g_app_info_launch(info, nullptr, nullptr, &error); g_object_unref(info); }
        if (error) { static_cast<App*>(d)->set_state("error", error->message); g_clear_error(&error); }
    }), this);
    recovery_ = label(""); gtk_label_set_selectable(GTK_LABEL(recovery_), TRUE);
    gtk_widget_set_no_show_all(recovery_, TRUE); pack(box, recovery_);
    g_signal_connect(shortcut_button_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) { static_cast<App*>(d)->edit_shortcut(); }), this);
    g_signal_connect(finish_button_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) { static_cast<App*>(d)->edit_shortcut(true); }), this);
    g_signal_connect(vocabulary_entry_, "changed", G_CALLBACK(+[](GtkEditable* widget, gpointer d) {
        auto* self = static_cast<App*>(d); if (self->building_ui_) return;
        const auto vocabulary = normalize_vocabulary(gtk_entry_get_text(GTK_ENTRY(widget)));
        if (vocabulary == self->vocabulary_) return;
        self->vocabulary_ = vocabulary; self->save_config();
    }), this);
    g_signal_connect(preview_switch_, "notify::active", G_CALLBACK(+[](GObject* w, GParamSpec*, gpointer d) {
        auto* self = static_cast<App*>(d); if (self->building_ui_) return;
        self->live_preview_ = gtk_switch_get_active(GTK_SWITCH(w));
        self->save_config();
    }), this);
    g_signal_connect(copy_switch_, "notify::active", G_CALLBACK(+[](GObject* w, GParamSpec*, gpointer d) {
        auto* self = static_cast<App*>(d); if (self->building_ui_) return;
        self->copy_to_clipboard_ = gtk_switch_get_active(GTK_SWITCH(w));
        self->save_config();
    }), this);
    g_signal_connect(commands_switch_, "notify::active", G_CALLBACK(+[](GObject* w, GParamSpec*, gpointer d) {
        auto* self = static_cast<App*>(d); if (self->building_ui_) return;
        self->voice_commands_ = gtk_switch_get_active(GTK_SWITCH(w));
        self->save_config();
    }), this);
    g_signal_connect(download_button_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) { static_cast<App*>(d)->download_model(); }), this);
    g_signal_connect(model_combo_, "changed", G_CALLBACK(+[](GtkComboBox* w, gpointer d) {
        auto* self = static_cast<App*>(d); if (self->building_ui_) return;
        const char* id = gtk_combo_box_get_active_id(w); if (!id) return;
        if (std::string(id) == "custom") { self->choose_model(); return; }
        if (self->model_ == id) return;
        self->engine_.release_model();
        self->model_ = id; self->save_config(); self->refresh();
    }), this);
    building_ui_ = false;
}

void App::refresh() {
    if (!window_) return;
    const bool exists = g_file_test(model_path().c_str(), G_FILE_TEST_IS_REGULAR);
    std::string status;
    if (!download_ && state_ == "error") status = message_;
    else if (shell_owner_.empty()) status = "Enable ren in Extensions, then log out and back in.";
    else if (!input_warning_.empty()) status = input_warning_;
    gtk_label_set_text(GTK_LABEL(status_label_), status.c_str());
    gtk_widget_set_visible(status_label_, !status.empty());
    gtk_widget_set_visible(sound_settings_, !input_warning_.empty());
    gtk_button_set_label(GTK_BUTTON(download_button_), download_ ? "Downloading…" : exists ? "Model installed" : "Download model");
    gtk_widget_set_sensitive(download_button_, !download_ && (!exists || state_ == "error") && !engine_.busy());
    gtk_widget_set_visible(download_button_, model_ != "custom" && (!exists || download_ || state_ == "error"));
    if (exists && state_ == "error" && !download_) gtk_button_set_label(GTK_BUTTON(download_button_), "Verify / repair model");
    gtk_widget_set_sensitive(model_details_, !download_ && !engine_.busy());
    gtk_widget_set_sensitive(model_combo_, !download_ && !engine_.busy());
    gtk_widget_set_sensitive(shortcut_button_, !engine_.busy());
    gtk_widget_set_sensitive(finish_button_, !engine_.busy());
    gtk_widget_set_sensitive(preview_switch_, !engine_.busy());
    gtk_widget_set_sensitive(copy_switch_, !engine_.busy());
    gtk_widget_set_sensitive(commands_switch_, !engine_.busy());
    gtk_widget_set_sensitive(vocabulary_entry_, !engine_.busy());
    gtk_button_set_label(GTK_BUTTON(shortcut_button_), shortcut_label(shortcut_).c_str());
    gtk_button_set_label(GTK_BUTTON(finish_button_), shortcut_label(finish_shortcut_).c_str());
    gtk_label_set_text(GTK_LABEL(recovery_), last_transcript_.c_str());
    gtk_widget_set_visible(recovery_, state_ == "error" && !last_transcript_.empty());
}

void App::show_model_details() {
    auto* dialog = gtk_dialog_new_with_buttons("Model details", GTK_WINDOW(window_),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR), nullptr, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 380, -1);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(gtk_dialog_get_header_bar(GTK_DIALOG(dialog))), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(dialog), "ren-model-details");
    g_signal_connect_object(window_, "hide", G_CALLBACK(gtk_widget_destroy), dialog, G_CONNECT_SWAPPED);
    auto* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), TRUE);
    GdkRectangle workarea{0, 0, 0, 640};
    if (auto* monitor = gdk_display_get_monitor_at_window(gtk_widget_get_display(window_), gtk_widget_get_window(window_)))
        gdk_monitor_get_workarea(monitor, &workarea);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), std::clamp(workarea.height - 120, 160, 520));
    pack(gtk_dialog_get_content_area(GTK_DIALOG(dialog)), scroll);
    auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_container_set_border_width(GTK_CONTAINER(box), 24);
    gtk_container_add(GTK_CONTAINER(scroll), box);
    const bool custom = model_ == "custom";
    std::error_code error;
    const auto bytes = std::filesystem::file_size(model_path(), error);
    std::string name = "Custom model";
    if (!custom) {
        name = "Whisper " + model_;
        for (const auto& model : kModels)
            if (model_ == model.id) name = std::string("Whisper ") + model.title;
    }
    auto* heading = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    pack(heading, label(name.c_str(), "ren-title"));
    const auto filename = custom ? std::filesystem::path(custom_model_).filename().string() : "ggml-" + model_ + ".bin";
    auto* subtitle = label(filename.c_str(), "ren-subtitle");
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle), "ren-secondary");
    gtk_label_set_line_wrap(GTK_LABEL(subtitle), FALSE);
    gtk_label_set_max_width_chars(GTK_LABEL(subtitle), 32);
    gtk_label_set_ellipsize(GTK_LABEL(subtitle), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_tooltip_text(subtitle, filename.c_str());
    pack(heading, subtitle);
    pack(box, heading);
    auto* facts = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(facts), 10);
    gtk_grid_set_column_spacing(GTK_GRID(facts), 20);
    int position = 0;
    auto fact = [&](const char* title, GtkWidget* value) {
        auto* field = label(title, "ren-secondary");
        gtk_label_set_line_wrap(GTK_LABEL(field), FALSE);
        if (GTK_IS_LABEL(value)) gtk_label_set_line_wrap(GTK_LABEL(value), FALSE);
        gtk_widget_set_valign(field, GTK_ALIGN_BASELINE);
        gtk_widget_set_halign(value, GTK_ALIGN_START);
        gtk_widget_set_valign(value, GTK_ALIGN_BASELINE);
        gtk_grid_attach(GTK_GRID(facts), field, 0, position, 1, 1);
        gtk_grid_attach(GTK_GRID(facts), value, 1, position++, 1, 1);
    };
    auto link = [&](const char* title, const char* text, const std::string& uri) {
        auto* button = gtk_link_button_new_with_label(uri.c_str(), text);
        atk_object_set_name(gtk_widget_get_accessible(button), (std::string(title) + ": " + text).c_str());
        gtk_style_context_add_class(gtk_widget_get_style_context(button), "ren-source-link");
        fact(title, button);
    };
    std::string size = error ? "File unavailable" : take(g_format_size_full(bytes, G_FORMAT_SIZE_IEC_UNITS));
    if (error && !custom) {
        for (const auto& model : kModels)
            if (model_ == model.id) size = take(g_format_size_full(model.bytes, G_FORMAT_SIZE_IEC_UNITS)) + " download";
    }
    fact("File size", label(size.c_str()));
    fact("Runs", label("Locally on this device"));
    fact("Engine", label("whisper.cpp"));
    fact("Language", label("English"));
    pack(box, facts);
    std::string note;
    if (custom) {
        fact("Source", label("User-supplied file"));
        note = "Source not verified. Compatibility is checked when the model is loaded.";
    } else {
        link("Original weights", "OpenAI", "https://github.com/openai/whisper");
        const auto source = "https://huggingface.co/ggerganov/whisper.cpp/blob/98aa99a0a9db05ae2342309f5096248665f7cba3/ggml-" + model_ + ".bin";
        link("Quantized file", "ggerganov", source);
        note = "Downloads verified with SHA-256.";
    }
    pack(box, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    auto* footer = label(note.c_str(), "ren-secondary");
    gtk_label_set_max_width_chars(GTK_LABEL(footer), 36);
    pack(box, footer);
    if (custom) {
        auto* choose = gtk_button_new_with_label("Choose another file…");
        pack(box, choose);
        g_signal_connect(choose, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            gtk_dialog_response(GTK_DIALOG(data), GTK_RESPONSE_ACCEPT);
        }), dialog);
    }
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* w, int response, gpointer data) {
        gtk_widget_destroy(GTK_WIDGET(w));
        if (response == GTK_RESPONSE_ACCEPT) static_cast<App*>(data)->choose_model();
    }), this);
    gtk_widget_show_all(dialog);
}

void App::choose_model() {
    auto* dialog = gtk_file_chooser_dialog_new("Choose a whisper.cpp GGML model", GTK_WINDOW(window_),
        GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Choose", GTK_RESPONSE_ACCEPT, nullptr);
    auto* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "whisper.cpp models (*.bin)");
    gtk_file_filter_add_pattern(filter, "*.bin");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog), TRUE);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT && !engine_.busy() && !download_) {
        const auto path = take(gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)));
        std::ifstream file(path, std::ios::binary);
        char magic[4]{};
        file.read(magic, sizeof(magic));
        if (file && std::string(magic, 4) == "lmgg") {
            engine_.release_model();
            custom_model_ = path;
            model_ = "custom";
            save_config();
        } else {
            set_state("error", "Choose a whisper.cpp GGML .bin file. This file has an invalid header.");
        }
    }
    gtk_widget_destroy(dialog);
    building_ui_ = true;
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(model_combo_), model_.c_str());
    building_ui_ = false;
    refresh();
}

void App::edit_shortcut(bool finish) {
    auto* dialog = gtk_dialog_new_with_buttons(finish ? "Finish shortcut" : "Start shortcut", GTK_WINDOW(window_), GTK_DIALOG_MODAL,
                                              "Cancel", GTK_RESPONSE_CANCEL, nullptr);
    auto* box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(box), 24);
    auto* prompt = label(finish ? "Press a key or a combination.\nEsc cancels." : "Press a combination with Ctrl, Alt, or Super.\nEsc cancels.");
    pack(box, prompt);
    g_object_set_data(G_OBJECT(dialog), "ren-finish-shortcut", GINT_TO_POINTER(finish));
    g_signal_connect(dialog, "key-press-event", G_CALLBACK(+[](GtkWidget* w, GdkEventKey* event, gpointer d) -> gboolean {
        if (event->keyval == GDK_KEY_Escape) { gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_CANCEL); return TRUE; }
        if (event->is_modifier) return TRUE;
        if (event->state & (GDK_META_MASK | GDK_HYPER_MASK)) return TRUE;
        auto state = event->state;
        if (state & GDK_MOD4_MASK) state |= GDK_SUPER_MASK;
        auto mods = static_cast<GdkModifierType>(state &
            (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK));
        auto key = gdk_keyval_to_lower(event->keyval);
        if (key == GDK_KEY_ISO_Left_Tab) {
            key = GDK_KEY_Tab;
            mods = static_cast<GdkModifierType>(mods | GDK_SHIFT_MASK);
        }
        const bool finish = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "ren-finish-shortcut"));
        if (!valid_shortcut(key, mods, finish)) return TRUE;
        auto* self = static_cast<App*>(d);
        auto& shortcut = finish ? self->finish_shortcut_ : self->shortcut_;
        shortcut = take(gtk_accelerator_name(key, mods));
        self->save_config(); self->refresh();
        gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_OK);
        return TRUE;
    }), this);
    gtk_widget_show_all(dialog); gtk_dialog_run(GTK_DIALOG(dialog)); gtk_widget_destroy(dialog);
}

void App::download_model() {
    if (model_ == "custom") return;
    if (download_) return;
    const auto prefix = std::filesystem::path(executable_).parent_path().parent_path();
    const std::filesystem::path candidates[] = {
        prefix / "share/ren/download-model.py", prefix / "scripts/download-model.py",
    };
    std::string script;
    if (!executable_.empty()) {
        for (const auto& candidate : candidates) {
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) { script = candidate.string(); break; }
        }
    }
    if (script.empty()) {
        set_state("error", "The model downloader is missing. Reinstall ren to restore it.");
        return;
    }
    GError* error = nullptr;
    download_ = g_subprocess_new(static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE), &error,
        "python3", script.c_str(), model_.c_str(), "--directory", (data_path_ + "/models").c_str(), "--force", nullptr);
    if (!download_) { set_state("error", error->message); g_clear_error(&error); return; }
    refresh();
    struct DownloadContext { App* app; std::shared_ptr<std::atomic_bool> alive; };
    g_subprocess_communicate_utf8_async(download_, nullptr, nullptr, +[](GObject* process, GAsyncResult* result, gpointer d) {
        std::unique_ptr<DownloadContext> context(static_cast<DownloadContext*>(d));
        gchar* stderr_text = nullptr; GError* error = nullptr;
        bool ok = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(process), result, nullptr, &stderr_text, &error);
        if (*context->alive) {
            auto* self = context->app;
            const bool success = ok && g_subprocess_get_successful(G_SUBPROCESS(process));
            g_clear_object(&self->download_);
            if (success) {
                self->engine_.release_model();
                self->set_state("idle", "Ready");
            } else self->set_state("error", error ? error->message : stderr_text && *stderr_text ? stderr_text : "Model download failed. Try again.");
        }
        g_free(stderr_text); g_clear_error(&error);
    }, new DownloadContext{this, alive_});
}

}
