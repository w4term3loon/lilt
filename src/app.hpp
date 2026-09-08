// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "engine.hpp"
#include <gtk/gtk.h>
#include <string>
#include <atomic>
#include <memory>

namespace lilt {
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
        *shortcut_button_ = nullptr, *finish_button_ = nullptr, *preview_switch_ = nullptr, *download_button_ = nullptr,
        *recovery_ = nullptr;
    GSubprocess* download_ = nullptr;
    std::shared_ptr<std::atomic_bool> alive_;
    std::string model_ = "small.en-q5_1", shortcut_ = "<Control><Super>space";
    std::string finish_shortcut_ = "Return";
    std::string state_ = "idle", message_ = "Ready", shell_owner_, last_transcript_;
    double level_ = 0;
    std::array<double, 3> bands_{};
    bool building_ui_ = false, live_preview_ = true;
    std::string config_path_, data_path_, executable_;
    uint64_t generation_ = 0;
    Engine engine_; // Destroy first: join worker before callback state is destroyed.
    void startup();
    bool register_bus(GDBusConnection*, GError**);
    void unregister_bus();
    void show();
    void build_ui();
    void toggle();
    void cancel_session();
    void set_state(const std::string&, const std::string&);
    void publish();
    void refresh();
    void load_config();
    void save_config();
    void edit_shortcut(bool finish = false);
    void download_model();
    std::string model_path() const;
    void dispatch(std::function<void()>);
    static void method_call(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                            const gchar*, GVariant*, GDBusMethodInvocation*, gpointer);
    static GVariant* get_property(GDBusConnection*, const gchar*, const gchar*,
                                 const gchar*, const gchar*, GError**, gpointer);
};
}
