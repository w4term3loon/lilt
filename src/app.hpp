// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "engine.hpp"
#include <gtk/gtk.h>
#include <string>
#include <atomic>
#include <memory>

namespace ren {
class App {
public:
    App();
    ~App();
    int run(int argc, char** argv);
private:
    friend struct NativeApplication;
    GtkApplication* app_ = nullptr;
    GDBusConnection* bus_ = nullptr;
    GDBusNodeInfo* info_ = nullptr;
    guint registration_ = 0, owner_watch_ = 0;
    GtkWidget *window_ = nullptr, *status_label_ = nullptr, *model_combo_ = nullptr,
        *shortcut_button_ = nullptr, *finish_button_ = nullptr, *preview_switch_ = nullptr, *copy_switch_ = nullptr,
        *commands_switch_ = nullptr, *download_button_ = nullptr,
        *recovery_ = nullptr, *model_details_ = nullptr, *vocabulary_entry_ = nullptr, *sound_settings_ = nullptr;
    GSubprocess* download_ = nullptr;
    std::shared_ptr<std::atomic_bool> alive_;
    std::string model_ = "small.en-q5_1", shortcut_ = "<Control><Super>space";
    std::string custom_model_;
    std::string vocabulary_, microphone_name_, input_warning_;
    std::string finish_shortcut_ = "Return";
    std::string state_ = "idle", message_ = "Ready", shell_owner_, last_transcript_;
    double level_ = 0;
    bool building_ui_ = false, live_preview_ = true, copy_to_clipboard_ = true, voice_commands_ = true;
    bool microphone_muted_ = false, heard_input_ = false;
    unsigned quiet_levels_ = 0;
    std::string config_path_, data_path_, executable_;
    uint64_t generation_ = 0;
    Engine engine_; // Destroy first: join worker before callback state is destroyed.
    void startup();
    bool register_bus(GDBusConnection*, GError**);
    void unregister_bus();
    void show();
    void build_ui();
    void toggle();
    void cancel_session(bool discard_transcript = true);
    void set_state(const std::string&, const std::string&);
    void publish();
    void refresh();
    bool update_input_warning();
    void load_config();
    void save_config();
    void edit_shortcut(bool finish = false);
    void download_model();
    void show_model_details();
    void choose_model();
    std::string model_path() const;
    void dispatch(std::function<void()>);
    static void method_call(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                            const gchar*, GVariant*, GDBusMethodInvocation*, gpointer);
    static GVariant* get_property(GDBusConnection*, const gchar*, const gchar*,
                                 const gchar*, const gchar*, GError**, gpointer);
};
}
