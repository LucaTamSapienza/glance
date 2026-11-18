/* preview_linux.c — a read-only GTK4/WebKitGTK window around the shared HTML renderer. */
#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "preview.h"

static const char *csp_meta =
    "<head><meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
    "img-src file: data: https: http:; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'\">";

static WebKitNetworkSession *session;
static int received_open, choosing;

static void open_files(GtkApplication *app, GFile **files, int n, const char *theme);

/* Report whether this user may map itself into a new user namespace, as bwrap must. */
static int userns_works(void) {
    char map[32];
    int n = snprintf(map, sizeof map, "0 %u 1", (unsigned)getuid());
    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        if (unshare(CLONE_NEWUSER) != 0) _exit(1);
        int fd = open("/proc/self/uid_map", O_WRONLY);
        _exit(fd >= 0 && write(fd, map, n) == n ? 0 : 1);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* Ask the desktop portal for the color scheme; anything but "prefer dark" is light. */
static int system_dark(void) {
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (!bus) return 0;
    GVariant *reply = g_dbus_connection_call_sync(bus, "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Settings", "Read",
        g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme"),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
    g_object_unref(bus);
    if (!reply) return 0;
    GVariant *value;
    g_variant_get(reply, "(v)", &value);
    while (g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT)) {
        GVariant *inner = g_variant_get_variant(value);
        g_variant_unref(value);
        value = inner;
    }
    int dark = g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32) && g_variant_get_uint32(value) == 1;
    g_variant_unref(value);
    g_variant_unref(reply);
    return dark;
}

/* Release the hold taken while a dialog without a document window is visible. */
static void dialog_done(GObject *source, GAsyncResult *result, gpointer app) {
    gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, NULL);
    g_application_release(G_APPLICATION(app));
}

/* Show an error over the active window, keeping the app alive until dismissed. */
static void alert(GtkApplication *app, const char *title, const char *detail) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", title);
    gtk_alert_dialog_set_detail(dialog, detail);
    g_application_hold(G_APPLICATION(app));
    gtk_alert_dialog_choose(dialog, gtk_application_get_active_window(app), NULL, dialog_done, app);
    g_object_unref(dialog);
}

/* Keep the preview on its document; explicit web/mail links open externally. */
static gboolean decide_policy(WebKitWebView *view, WebKitPolicyDecision *decision,
                              WebKitPolicyDecisionType type, gpointer data) {
    if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) return FALSE;
    WebKitNavigationAction *action = webkit_navigation_policy_decision_get_navigation_action(
        WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    const char *uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));
    if (webkit_navigation_action_get_navigation_type(action) == WEBKIT_NAVIGATION_TYPE_LINK_CLICKED) {
        char *scheme = g_uri_parse_scheme(uri);
        if (scheme && (!g_ascii_strcasecmp(scheme, "https") || !g_ascii_strcasecmp(scheme, "http") ||
                       !g_ascii_strcasecmp(scheme, "mailto"))) {
            GtkUriLauncher *launcher = gtk_uri_launcher_new(uri);
            gtk_uri_launcher_launch(launcher, GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(view))),
                                    NULL, NULL, NULL);
            g_object_unref(launcher);
        }
        g_free(scheme);
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }
    int initial = !g_object_get_data(G_OBJECT(view), "loaded") &&
                  (g_str_has_prefix(uri, "file:") || !strcmp(uri, "about:blank"));
    if (initial) webkit_policy_decision_use(decision);
    else webkit_policy_decision_ignore(decision);
    return TRUE;
}

/* Mark the document loaded so later navigations are refused. */
static void load_changed(WebKitWebView *view, WebKitLoadEvent event, gpointer data) {
    if (event == WEBKIT_LOAD_FINISHED) g_object_set_data(G_OBJECT(view), "loaded", GINT_TO_POINTER(1));
}

/* Surface WebKit loading failures instead of leaving the window blank. */
static gboolean load_failed(WebKitWebView *view, WebKitLoadEvent event, const char *uri,
                            GError *error, gpointer app) {
    alert(app, "Unable to display Markdown", error->message);
    return TRUE;
}

/* Render one canonical file into a new window; returns an error message or NULL. */
static char *show_document(GtkApplication *app, const char *path, const char *theme) {
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR))
        return g_strdup("Choose a Markdown file, not a folder.");
    char *markdown;
    gsize len;
    GError *error = NULL;
    if (!g_file_get_contents(path, &markdown, &len, &error)) {
        char *message = g_strdup(error->message);
        g_error_free(error);
        return message;
    }
    if (!g_utf8_validate(markdown, len, NULL)) {
        g_free(markdown);
        return g_strdup("The file is not valid UTF-8 text.");
    }
    char *name = g_path_get_basename(path);
    char *html = preview_html(markdown, len, name, *theme ? theme : NULL, system_dark(), NULL);
    g_free(markdown);
    char *head = html ? strstr(html, "<head>") : NULL;
    if (!head) { free(html); g_free(name); return g_strdup("Could not render this Markdown file."); }
    *head = '\0';
    char *page = g_strconcat(html, csp_meta, head + strlen("<head>"), NULL);
    free(html);

    WebKitSettings *settings = webkit_settings_new();
    webkit_settings_set_enable_javascript(settings, FALSE);
    GtkWidget *view = g_object_new(WEBKIT_TYPE_WEB_VIEW, "network-session", session,
                                   "settings", settings, NULL);
    g_object_unref(settings);
    gtk_widget_set_size_request(view, 360, 260);
    g_signal_connect(view, "decide-policy", G_CALLBACK(decide_policy), NULL);
    g_signal_connect(view, "load-changed", G_CALLBACK(load_changed), NULL);
    g_signal_connect(view, "load-failed", G_CALLBACK(load_failed), app);

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), name);
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 760);
    gtk_window_set_child(GTK_WINDOW(window), view);
    g_object_set_data_full(G_OBJECT(window), "path", g_strdup(path), g_free);

    char *dir = g_path_get_dirname(path);
    char *base = g_filename_to_uri(dir, NULL, NULL);
    char *slash = g_strconcat(base, "/", NULL);
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(view), page, slash);
    gtk_window_present(GTK_WINDOW(window));
    g_free(slash);
    g_free(base);
    g_free(dir);
    g_free(page);
    g_free(name);
    return NULL;
}

/* Open every file independently; an already-open canonical path is brought forward. */
static void open_files(GtkApplication *app, GFile **files, int n, const char *theme) {
    received_open = 1;
    GString *errors = g_string_new(NULL);
    for (int i = 0; i < n; i++) {
        char *raw = g_file_get_path(files[i]);
        char *path = raw ? realpath(raw, NULL) : NULL;
        char *message = NULL;
        GtkWindow *existing = NULL;
        for (GList *w = path ? gtk_application_get_windows(app) : NULL; w; w = w->next) {
            const char *open = g_object_get_data(w->data, "path");
            if (open && !strcmp(open, path)) { existing = w->data; break; }
        }
        if (existing) gtk_window_present(existing);
        else if (path) message = show_document(app, path, theme);
        else message = g_strdup("The file could not be opened.");
        if (message) {
            char *name = g_file_get_basename(files[i]);
            g_string_append_printf(errors, "%s%s: %s", errors->len ? "\n\n" : "", name, message);
            g_free(name);
            g_free(message);
        }
        free(path);
        g_free(raw);
    }
    if (errors->len) alert(app, "Unable to preview Markdown", errors->str);
    g_string_free(errors, TRUE);
}

/* Receive files from this or a later invocation; the hint carries the theme. */
static void on_open(GApplication *app, GFile **files, int n, const char *hint, gpointer data) {
    open_files(GTK_APPLICATION(app), files, n, hint);
}

/* Open the files picked in the Open dialog with the automatic theme. */
static void chosen(GObject *source, GAsyncResult *result, gpointer app) {
    GListModel *list = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(source), result, NULL);
    choosing = 0;
    if (list) {
        int n = (int)g_list_model_get_n_items(list);
        GFile **files = g_new(GFile *, n ? n : 1);
        for (int i = 0; i < n; i++) files[i] = g_list_model_get_item(list, i);
        open_files(app, files, n, "");
        for (int i = 0; i < n; i++) g_object_unref(files[i]);
        g_free(files);
        g_object_unref(list);
    }
    g_application_release(G_APPLICATION(app));
}

/* Offer a multi-file Markdown picker, once at a time. */
static void open_dialog(GSimpleAction *action, GVariant *param, gpointer app) {
    if (choosing) return;
    choosing = 1;
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Markdown");
    gtk_file_filter_add_suffix(filter, "md");
    gtk_file_filter_add_suffix(filter, "markdown");
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    g_application_hold(G_APPLICATION(app));
    gtk_file_dialog_open_multiple(dialog, gtk_application_get_active_window(app), NULL, chosen, app);
    g_object_unref(dialog);
    g_object_unref(filters);
    g_object_unref(filter);
}

/* Close the focused document window. */
static void close_window(GSimpleAction *action, GVariant *param, gpointer app) {
    GtkWindow *window = gtk_application_get_active_window(app);
    if (window) gtk_window_close(window);
}

/* Quit the preview, closing every window. */
static void quit(GSimpleAction *action, GVariant *param, gpointer app) {
    g_application_quit(app);
}

/* Register the Open, Close and Quit actions and their shortcuts. */
static void on_startup(GApplication *app, gpointer data) {
    static const GActionEntry actions[] = {
        { .name = "open", .activate = open_dialog },
        { .name = "close", .activate = close_window },
        { .name = "quit", .activate = quit },
    };
    g_action_map_add_action_entries(G_ACTION_MAP(app), actions, G_N_ELEMENTS(actions), app);
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.open", (const char *[]){ "<Control>o", NULL });
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.close", (const char *[]){ "<Control>w", NULL });
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.quit", (const char *[]){ "<Control>q", NULL });
    session = webkit_network_session_new_ephemeral();
}

/* A launch without files offers the picker; otherwise focus an existing window. */
static void on_activate(GApplication *app, gpointer data) {
    GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(app));
    if (window) gtk_window_present(window);
    else if (!received_open) open_dialog(NULL, NULL, app);
}

/* Accept FILE... [--theme NAME]; the first instance serves every later request over D-Bus. */
int main(int argc, char **argv) {
    const char *theme = "";
    GFile **files = g_new0(GFile *, argc);
    int n = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--theme") && i + 1 < argc) theme = argv[++i];
        else if (argv[i][0] == '-') {
            fprintf(stderr, "usage: glance-preview [FILE...] [--theme NAME]\n");
            return 2;
        } else files[n++] = g_file_new_for_commandline_arg(argv[i]);
    }
    /* WebKit's bwrap sandbox needs user namespaces; Ubuntu's AppArmor blocks them
     * unless a profile allows this binary (see README). JavaScript stays off. */
    if (!userns_works()) {
        g_setenv("WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS", "1", FALSE);
        fprintf(stderr, "glance-preview: WebKit sandbox disabled (user namespaces blocked); "
                        "install the AppArmor profile described in the README to keep it on.\n");
    }
    GtkApplication *app = gtk_application_new("dev.glance.Preview", G_APPLICATION_HANDLES_OPEN);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "open", G_CALLBACK(on_open), NULL);
    GError *error = NULL;
    if (!g_application_register(G_APPLICATION(app), NULL, &error)) {
        fprintf(stderr, "glance-preview: %s\n", error->message);
        return 1;
    }
    if (n) g_application_open(G_APPLICATION(app), files, n, theme);
    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    for (int i = 0; i < n; i++) g_object_unref(files[i]);
    g_free(files);
    g_object_unref(app);
    return status;
}
