/*
 * NetChecker - GTK3 GUI edition
 *
 * This is a GUI conversion of the original console-based NetChecker app.
 * All original functions and their core logic are preserved:
 *   Add_Device, edit_deviceList, delete_entry/Customdelete, searchbyip, view,
 *   FunctionToCheckDevices (background ping monitor thread), send_alert,
 *   save_file/open_file (device CSV persistence), saveUsers/readUsers,
 *   register_user, edit_users, delete_user, view_users, search_user,
 *   SignIn/Loginpage.
 *
 * The console I/O layer (printf/scanf/read_line menus) has been replaced
 * with GTK dialogs, GtkTreeView lists, and buttons. Win32 MessageBox has
 * been replaced with GtkMessageDialog (Beep() is kept on Windows, gdk_beep()
 * is used elsewhere) so the same binary can be built on Linux too.
 *
 * The device check in FunctionToCheckDevices() uses a real ICMP echo
 * (IcmpSendEcho via <icmpapi.h>) on Windows instead of shelling out to
 * `ping`. Everything around it -- the mutex locking, the alert-on-change
 * logic, the status string, the retry count -- is unchanged; only how
 * "is this IP alive" gets answered changed. On non-Windows builds it falls
 * back to the original system("ping ...") approach with POSIX ping flags.
 *
 * Three small, deliberate correctness fixes were made while porting (each
 * flagged in place with "PORT FIX"):
 *   1. readUsers() now starts at index 0 and sets `users` to the number of
 *      entries actually read, so previously-registered users show up in the
 *      GUI after a restart (the original started at index 1 and never set
 *      `users`, so the Users tab would always be empty on relaunch).
 *   2. saveUsers() now writes only `users` valid entries instead of a
 *      hardcoded MAX_USERS, so empty slots aren't written to disk.
 *   3. SignIn() now checks against `users` (registered count) instead of
 *      MAX_USERS, and returns a boolean instead of recursing into the login
 *      dialog (recursion doesn't fit a GUI event loop).
 *
 * Build (Windows, MSYS2 MinGW64 shell):
 *   pacman -S mingw-w64-x86_64-gtk3
 *   gcc netchecker_gui.c -o netchecker_gui.exe `pkg-config --cflags --libs gtk+-3.0` -lpthread -liphlpapi -lws2_32 -mwindows
 *
 * Build (Linux):
 *   sudo apt install libgtk-3-dev
 *   gcc netchecker_gui.c -o netchecker_gui `pkg-config --cflags --libs gtk+-3.0` -lpthread
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

/* ----------------------------- Data model ----------------------------- */

#define MAX_DEVICES 100
#define MAX_USERS 5
#define MAX_PASSWORD_LENGTH 64

struct Devices {
    char name[50];
    char ip[15];
    char location[50];
    char status[20]; /* "Active" / "Not_Active" / "Unknown" */
};

struct User_Info {
    char name[50];
    char email[50];
    char phone[15];
};

static struct Devices Device_List[MAX_DEVICES];
static struct User_Info User_List[MAX_USERS];

static int i = 0;          /* number of registered devices */
static int users = 0;      /* number of registered users */
static bool stop_flag = false;
static bool monitor_started = false;
static bool registration_state = false;
static char password[MAX_PASSWORD_LENGTH] = "netchecker123";

static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t monitor_thread;

/* ------------------------------ GTK state ------------------------------ */

enum { COL_NAME = 0, COL_IP, COL_LOCATION, COL_STATUS, N_DEVICE_COLS };
enum { UCOL_NAME = 0, UCOL_EMAIL, UCOL_PHONE, N_USER_COLS };

static GtkWidget *main_window = NULL;
static GtkListStore *device_store = NULL;
static GtkListStore *user_store = NULL;
static GtkWidget *device_tree = NULL;
static GtkWidget *user_tree = NULL;
static GtkWidget *statusbar = NULL;
static guint statusbar_ctx = 0;
static GtkWidget *monitor_button = NULL;

/* ------------------------------ Prototypes ------------------------------ */

static void refresh_device_view(void);
static void refresh_user_view(void);
static void on_refresh_devices_clicked(GtkButton *btn, gpointer user_data);
static void on_refresh_users_clicked(GtkButton *btn, gpointer user_data);
static void show_message(const char *title, const char *message, GtkMessageType type);
static gboolean confirm_dialog(const char *message);
static gboolean multi_field_dialog(GtkWindow *parent, const char *title,
                                    const char *labels[], char *buffers[], const int lens[],
                                    const gboolean *mask, int n);
static gboolean single_entry_dialog(GtkWindow *parent, const char *title,
                                     const char *label, char *buffer, int maxlen);

int searchbyip(const char *parameter);
int Customdelete(int id);
void save_file(void);
void open_file(void);
void saveUsers(void);
void readUsers(void);
void send_alert(const char *name, const char *ip, const char *location, const char *status);
void *FunctionToCheckDevices(void *arg);
gboolean SignIn(const char *username, const char *upassword);

/* ------------------------------ Dialog helpers ------------------------------ */

static void show_message(const char *title, const char *message, GtkMessageType type) {
    GtkWidget *dialog = gtk_message_dialog_new(
        main_window ? GTK_WINDOW(main_window) : NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, type, GTK_BUTTONS_OK, "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_window_set_position(GTK_WINDOW(dialog),
        main_window ? GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER_ALWAYS);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static gboolean confirm_dialog(const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(
        main_window ? GTK_WINDOW(main_window) : NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
        "%s", message);
    gtk_window_set_position(GTK_WINDOW(dialog),
        main_window ? GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER_ALWAYS);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response == GTK_RESPONSE_YES;
}

/* Generic labeled-entry-grid dialog. buffers[] are pre-filled (may be empty
 * strings) and on OK are overwritten (bounded by lens[]) with what the user
 * typed. mask[k]==TRUE hides the k-th entry's text (for passwords); mask may
 * be NULL to mean "no masking". Returns TRUE if OK was pressed. */
static gboolean multi_field_dialog(GtkWindow *parent, const char *title,
                                    const char *labels[], char *buffers[], const int lens[],
                                    const gboolean *mask, int n) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(title, parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_OK", GTK_RESPONSE_OK,
        NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_window_set_position(GTK_WINDOW(dialog),
        parent ? GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER_ALWAYS);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    GtkWidget **entries = g_new0(GtkWidget *, n);
    for (int k = 0; k < n; k++) {
        GtkWidget *lbl = gtk_label_new(labels[k]);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(entry), lens[k] - 1);
        gtk_entry_set_text(GTK_ENTRY(entry), buffers[k]);
        gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
        gtk_entry_set_width_chars(GTK_ENTRY(entry), 28);
        if (mask && mask[k]) {
            gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
        }
        gtk_grid_attach(GTK_GRID(grid), lbl, 0, k, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), entry, 1, k, 1, 1);
        entries[k] = entry;
    }

    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dialog);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gboolean ok = (response == GTK_RESPONSE_OK);
    if (ok) {
        for (int k = 0; k < n; k++) {
            const char *text = gtk_entry_get_text(GTK_ENTRY(entries[k]));
            strncpy(buffers[k], text, lens[k] - 1);
            buffers[k][lens[k] - 1] = '\0';
        }
    }
    g_free(entries);
    gtk_widget_destroy(dialog);
    return ok;
}

static gboolean single_entry_dialog(GtkWindow *parent, const char *title,
                                     const char *label, char *buffer, int maxlen) {
    const char *labels[1] = { label };
    char *buffers[1] = { buffer };
    int lens[1] = { maxlen };
    return multi_field_dialog(parent, title, labels, buffers, lens, NULL, 1);
}

/* ------------------------------ Device CSV persistence ------------------------------ */

void save_file(void) {
    pthread_mutex_lock(&list_lock);
    FILE *f = fopen("Devices_List.csv", "w");
    if (f == NULL) {
        pthread_mutex_unlock(&list_lock);
        g_printerr("Error opening Devices_List.csv for writing!\n");
        return;
    }
    fprintf(f, "Device-Name,IP-Address,Location,Status\n");
    for (int a = 0; a < i; a++) {
        fprintf(f, "%s,%s,%s,%s\n",
                Device_List[a].name, Device_List[a].ip,
                Device_List[a].location, Device_List[a].status);
    }
    fclose(f);
    pthread_mutex_unlock(&list_lock);
}

void open_file(void) {
    FILE *f = fopen("Devices_List.csv", "r");
    if (f == NULL) {
        g_printerr("No existing device list found. Starting with an empty list.\n");
        return;
    }
    char line[300];
    fgets(line, sizeof(line), f); /* skip header */

    while (fgets(line, sizeof(line), f) && i < MAX_DEVICES) {
        int fields = sscanf(line, "%49[^,],%14[^,],%49[^,],%19[^\r\n]",
                             Device_List[i].name, Device_List[i].ip,
                             Device_List[i].location, Device_List[i].status);
        if (fields == 4) {
            i++;
        }
        /* malformed rows are silently skipped rather than corrupting the array */
    }
    fclose(f);
}

/* ------------------------------ Users CSV persistence ------------------------------ */

void saveUsers(void) {
    FILE *fp = fopen("users.csv", "w");
    if (fp == NULL) {
        g_printerr("Error opening users.csv for writing!\n");
        return;
    }
    fprintf(fp, "User-Name,Email,Phone-Number\n");
    /* PORT FIX: write only the valid entries, not a hardcoded MAX_USERS */
    for (int a = 0; a < users; a++) {
        fprintf(fp, "%s,%s,%s\n",
                User_List[a].name, User_List[a].email, User_List[a].phone);
    }
    registration_state = true;
    fclose(fp);
}

void readUsers(void) {
    FILE *fO = fopen("users.csv", "r");
    if (fO == NULL) {
        return;
    }
    registration_state = true;

    char line[300];
    fgets(line, sizeof(line), fO); /* skip header */

    /* PORT FIX: start at index 0 and track how many rows were actually read,
     * so users that were registered in a previous session reappear in the
     * GUI on launch. */
    users = 0;
    while (fgets(line, sizeof(line), fO) && users < MAX_USERS) {
        int fields = sscanf(line, "%49[^,],%49[^,],%14[^\r\n]",
                             User_List[users].name, User_List[users].email,
                             User_List[users].phone);
        if (fields == 3) {
            users++;
        }
    }
    fclose(fO);
}

/* ------------------------------ Device logic ------------------------------ */

int searchbyip(const char *parameter) {
    pthread_mutex_lock(&list_lock);
    for (int b = 0; b < i; b++) {
        if (strcmp(Device_List[b].ip, parameter) == 0) {
            pthread_mutex_unlock(&list_lock);
            return b;
        }
    }
    pthread_mutex_unlock(&list_lock);
    return -1;
}

int Customdelete(int id) {
    pthread_mutex_lock(&list_lock);
    for (int x = id; x < i - 1; x++) {
        Device_List[x] = Device_List[x + 1];
    }
    i--;
    pthread_mutex_unlock(&list_lock);
    save_file();
    return 0;
}

static void refresh_device_view(void) {
    gtk_list_store_clear(device_store);
    pthread_mutex_lock(&list_lock);
    for (int a = 0; a < i; a++) {
        GtkTreeIter iter;
        gtk_list_store_append(device_store, &iter);
        gtk_list_store_set(device_store, &iter,
            COL_NAME, Device_List[a].name,
            COL_IP, Device_List[a].ip,
            COL_LOCATION, Device_List[a].location,
            COL_STATUS, Device_List[a].status,
            -1);
    }
    pthread_mutex_unlock(&list_lock);
}

static void refresh_user_view(void) {
    gtk_list_store_clear(user_store);
    for (int u = 0; u < users; u++) {
        GtkTreeIter iter;
        gtk_list_store_append(user_store, &iter);
        gtk_list_store_set(user_store, &iter,
            UCOL_NAME, User_List[u].name,
            UCOL_EMAIL, User_List[u].email,
            UCOL_PHONE, User_List[u].phone,
            -1);
    }
}

/* Thin wrappers so the "clicked" signal (which passes a GtkButton* and a
 * gpointer) doesn't get connected directly to a void(void) function --
 * mismatched callback signatures are undefined behavior in C even though it
 * happens to work on most ABIs. */
static void on_refresh_devices_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    refresh_device_view();
}

static void on_refresh_users_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    refresh_user_view();
}

/* Get the IP string in the currently selected row of a tree view, or NULL. */
static gchar *get_selected_column_text(GtkWidget *tree, int col) {
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    GtkTreeModel *model;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) {
        return NULL;
    }
    gchar *text;
    gtk_tree_model_get(model, &iter, col, &text, -1);
    return text;
}

static void on_add_device(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    pthread_mutex_lock(&list_lock);
    if (i >= MAX_DEVICES) {
        pthread_mutex_unlock(&list_lock);
        show_message("List Full", "List is full!!", GTK_MESSAGE_WARNING);
        return;
    }
    pthread_mutex_unlock(&list_lock);

    char name[50] = "", ip[15] = "", location[50] = "";
    const char *labels[3] = { "Device Name:", "IP Address:", "Location:" };
    char *buffers[3] = { name, ip, location };
    int lens[3] = { 50, 15, 50 };

    if (!multi_field_dialog(GTK_WINDOW(main_window), "Add New Device", labels, buffers, lens, NULL, 3))
        return;

    if (name[0] == '\0' || ip[0] == '\0') {
        show_message("Invalid Input", "Device name and IP address cannot be empty.", GTK_MESSAGE_ERROR);
        return;
    }

    pthread_mutex_lock(&list_lock);
    if (i >= MAX_DEVICES) {
        pthread_mutex_unlock(&list_lock);
        show_message("List Full", "List is full!!", GTK_MESSAGE_WARNING);
        return;
    }
    strncpy(Device_List[i].name, name, sizeof(Device_List[i].name) - 1);
    Device_List[i].name[sizeof(Device_List[i].name) - 1] = '\0';
    strncpy(Device_List[i].ip, ip, sizeof(Device_List[i].ip) - 1);
    Device_List[i].ip[sizeof(Device_List[i].ip) - 1] = '\0';
    strncpy(Device_List[i].location, location, sizeof(Device_List[i].location) - 1);
    Device_List[i].location[sizeof(Device_List[i].location) - 1] = '\0';
    strcpy(Device_List[i].status, "Unknown");
    i++;
    pthread_mutex_unlock(&list_lock);

    save_file();
    refresh_device_view();
    show_message("Success", "Device successfully added!", GTK_MESSAGE_INFO);
}

static void on_edit_device(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    gchar *cur_ip = get_selected_column_text(device_tree, COL_IP);
    if (!cur_ip) {
        show_message("No Selection", "Please select a device to edit.", GTK_MESSAGE_WARNING);
        return;
    }
    int idx = searchbyip(cur_ip);
    g_free(cur_ip);
    if (idx < 0) {
        show_message("Not Found", "Device not found!", GTK_MESSAGE_ERROR);
        return;
    }

    char name[50], ip[15], location[50];
    pthread_mutex_lock(&list_lock);
    strcpy(name, Device_List[idx].name);
    strcpy(ip, Device_List[idx].ip);
    strcpy(location, Device_List[idx].location);
    pthread_mutex_unlock(&list_lock);

    const char *labels[3] = { "Device Name:", "IP Address:", "Location:" };
    char *buffers[3] = { name, ip, location };
    int lens[3] = { 50, 15, 50 };
    if (!multi_field_dialog(GTK_WINDOW(main_window), "Edit Device", labels, buffers, lens, NULL, 3))
        return;

    pthread_mutex_lock(&list_lock);
    if (idx >= i) {
        pthread_mutex_unlock(&list_lock);
        show_message("Error", "Device list changed, please try again.", GTK_MESSAGE_ERROR);
        return;
    }
    strncpy(Device_List[idx].name, name, sizeof(Device_List[idx].name) - 1);
    Device_List[idx].name[sizeof(Device_List[idx].name) - 1] = '\0';
    strncpy(Device_List[idx].ip, ip, sizeof(Device_List[idx].ip) - 1);
    Device_List[idx].ip[sizeof(Device_List[idx].ip) - 1] = '\0';
    strncpy(Device_List[idx].location, location, sizeof(Device_List[idx].location) - 1);
    Device_List[idx].location[sizeof(Device_List[idx].location) - 1] = '\0';
    pthread_mutex_unlock(&list_lock);

    save_file();
    refresh_device_view();
    show_message("Success", "Device information successfully edited.", GTK_MESSAGE_INFO);
}

static void on_delete_device(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    gchar *ip = get_selected_column_text(device_tree, COL_IP);
    if (!ip) {
        show_message("No Selection", "Please select a device to delete.", GTK_MESSAGE_WARNING);
        return;
    }
    int idx = searchbyip(ip);
    if (idx < 0) {
        show_message("Not Found", "Device not found!", GTK_MESSAGE_ERROR);
        g_free(ip);
        return;
    }

    gchar *msg = g_strdup_printf("Are you sure you want to delete the device with IP %s?", ip);
    gboolean yes = confirm_dialog(msg);
    g_free(msg);
    g_free(ip);

    if (yes) {
        Customdelete(idx);
        refresh_device_view();
        show_message("Deleted", "Device successfully deleted!!!", GTK_MESSAGE_INFO);
    }
}

static void on_search_device(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    char ip[15] = "";
    if (!single_entry_dialog(GTK_WINDOW(main_window), "Search Device", "IP Address:", ip, 15))
        return;

    int idx = searchbyip(ip);
    if (idx < 0) {
        show_message("Not Found", "Device not found, verify that the IP address is correct and try again.", GTK_MESSAGE_WARNING);
        return;
    }

    /* select + scroll to the matching row */
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(device_store), &iter);
    int pos = 0;
    while (valid) {
        if (pos == idx) {
            GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(device_tree));
            gtk_tree_selection_select_iter(sel, &iter);
            GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(device_store), &iter);
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(device_tree), path, NULL, FALSE, 0, 0);
            gtk_tree_path_free(path);
            break;
        }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(device_store), &iter);
        pos++;
    }

    pthread_mutex_lock(&list_lock);
    gchar *msg = g_strdup_printf("Device Found!!!\n\nName: %s\nIP: %s\nLocation: %s\nStatus: %s",
        Device_List[idx].name, Device_List[idx].ip, Device_List[idx].location, Device_List[idx].status);
    pthread_mutex_unlock(&list_lock);
    show_message("Device Found", msg, GTK_MESSAGE_INFO);
    g_free(msg);
}

/* ------------------------------ Monitor thread ------------------------------ */

typedef struct {
    char name[50];
    char ip[15];
    char location[50];
    char status[20];
} AlertData;

void send_alert(const char *name, const char *ip, const char *location, const char *status) {
    char message[300];
    snprintf(message, sizeof(message), "Device named %s with IP address %s at %s is %s",
              name, ip, location, status);

    GtkWidget *dialog = gtk_message_dialog_new(
        main_window ? GTK_WINDOW(main_window) : NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), "Alert!!!");
    gtk_window_set_position(GTK_WINDOW(dialog),
        main_window ? GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER_ALWAYS);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

#ifdef _WIN32
    Beep(1000, 500);
#else
    gdk_beep();
#endif
}

/* Runs on the GTK main thread via g_idle_add, since GTK widgets are not
 * thread-safe to touch directly from the pthread monitor thread. */
static gboolean alert_idle_cb(gpointer data) {
    AlertData *ad = data;
    send_alert(ad->name, ad->ip, ad->location, ad->status);
    g_free(ad);
    return G_SOURCE_REMOVE;
}

static gboolean refresh_idle_cb(gpointer data) {
    (void)data;
    refresh_device_view();
    return G_SOURCE_REMOVE;
}

/* ------------------------------ ICMP ping ------------------------------ */

#ifdef _WIN32
/* Real ICMP echo via the Windows IP Helper API, replacing the old
 * system("ping ...") shell-out. Same retry count (3) and per-attempt
 * timeout (1000ms) as the original `ping -n 3 -w 1000`. Returns TRUE if any
 * attempt got a successful echo reply. */
static gboolean ping_host(const char *ip_str) {
    HANDLE hIcmp = IcmpCreateFile();
    if (hIcmp == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    IPAddr ipaddr = 0;
    if (InetPtonA(AF_INET, ip_str, &ipaddr) != 1) {
        IcmpCloseHandle(hIcmp);
        return FALSE;
    }

    char sendData[32] = "NetChecker-Ping";
    DWORD replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(sendData) + 8;
    LPVOID replyBuffer = malloc(replySize);
    if (!replyBuffer) {
        IcmpCloseHandle(hIcmp);
        return FALSE;
    }

    gboolean alive = FALSE;
    for (int attempt = 0; attempt < 3 && !alive; attempt++) {
        DWORD ret = IcmpSendEcho(hIcmp, ipaddr, sendData, (WORD)sizeof(sendData),
                                  NULL, replyBuffer, replySize, 1000);
        if (ret != 0) {
            PICMP_ECHO_REPLY reply = (PICMP_ECHO_REPLY)replyBuffer;
            if (reply->Status == IP_SUCCESS) {
                alive = TRUE;
            }
        }
    }

    free(replyBuffer);
    IcmpCloseHandle(hIcmp);
    return alive;
}
#else
/* Non-Windows fallback: same shell-out approach as the original, just with
 * POSIX ping flags instead of Windows ones. */
static gboolean ping_host(const char *ip_str) {
    char command[100];
    sprintf(command, "ping -c 3 -W 1 %s >/dev/null 2>&1", ip_str);
    return system(command) == 0;
}
#endif

void *FunctionToCheckDevices(void *arg) {
    (void)arg;
    while (!stop_flag) {
        pthread_mutex_lock(&list_lock);
        int count = i;
        pthread_mutex_unlock(&list_lock);

        if (count == 0) {
            g_usleep(2000000);
            continue;
        }

        for (int a = 0; a < count && !stop_flag; a++) {
            char ip_copy[15];
            char name_copy[50];
            char location_copy[50];
            char previous_status[20];

            pthread_mutex_lock(&list_lock);
            if (a >= i) { /* list shrank since we grabbed `count` */
                pthread_mutex_unlock(&list_lock);
                break;
            }
            strcpy(ip_copy, Device_List[a].ip);
            strcpy(name_copy, Device_List[a].name);
            strcpy(location_copy, Device_List[a].location);
            strcpy(previous_status, Device_List[a].status);
            pthread_mutex_unlock(&list_lock);

            gboolean is_alive = ping_host(ip_copy);
            int feedback = is_alive ? 0 : 1; /* keep 0-means-alive convention from the original */

            char new_status[20];
            strcpy(new_status, feedback == 0 ? "Active" : "Not_Active");

            pthread_mutex_lock(&list_lock);
            if (a < i && strcmp(Device_List[a].ip, ip_copy) == 0) {
                strcpy(Device_List[a].status, new_status);
            }
            pthread_mutex_unlock(&list_lock);

            /* alert on downtime (only when status is changing into Not_Active) */
            if (feedback != 0 && strcmp(previous_status, "Not_Active") != 0) {
                AlertData *ad = g_new0(AlertData, 1);
                strcpy(ad->name, name_copy);
                strcpy(ad->ip, ip_copy);
                strcpy(ad->location, location_copy);
                strcpy(ad->status, new_status);
                g_idle_add(alert_idle_cb, ad);
            }
            /* alert on recovery (status changing from Not_Active back to Active) */
            if (feedback == 0 && strcmp(previous_status, "Not_Active") == 0) {
                AlertData *ad = g_new0(AlertData, 1);
                strcpy(ad->name, name_copy);
                strcpy(ad->ip, ip_copy);
                strcpy(ad->location, location_copy);
                strcpy(ad->status, new_status);
                g_idle_add(alert_idle_cb, ad);
            }
        }
        g_idle_add(refresh_idle_cb, NULL);
        save_file();
        g_usleep(5000000);
    }
    return NULL;
}

static void on_toggle_monitor(GtkButton *btn, gpointer user_data) {
    (void)user_data;
    if (!monitor_started) {
        if (pthread_create(&monitor_thread, NULL, FunctionToCheckDevices, NULL) != 0) {
            show_message("Error", "Failed to create monitor thread", GTK_MESSAGE_ERROR);
            return;
        }
        monitor_started = true;
        gtk_button_set_label(btn, "Stop Monitor");
        gtk_statusbar_push(GTK_STATUSBAR(statusbar), statusbar_ctx, "Monitor started.");
    } else {
        stop_flag = true;
        pthread_join(monitor_thread, NULL); /* let it exit its loop cleanly */
        stop_flag = false;
        monitor_started = false;
        gtk_button_set_label(btn, "Start Monitor");
        gtk_statusbar_push(GTK_STATUSBAR(statusbar), statusbar_ctx, "Monitor stopped.");
    }
}

/* ------------------------------ Users management ------------------------------ */

static void on_register_user(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    if (users >= MAX_USERS) {
        show_message("Limit Reached", "User limit reached. Cannot register more users.", GTK_MESSAGE_WARNING);
        return;
    }
    char name[50] = "", email[50] = "", phone[15] = "";
    const char *labels[3] = { "Name:", "Email:", "Phone:" };
    char *buffers[3] = { name, email, phone };
    int lens[3] = { 50, 50, 15 };

    if (!multi_field_dialog(GTK_WINDOW(main_window), "Register New User", labels, buffers, lens, NULL, 3))
        return;
    if (name[0] == '\0' || email[0] == '\0') {
        show_message("Invalid Input", "Name and email cannot be empty.", GTK_MESSAGE_ERROR);
        return;
    }

    strncpy(User_List[users].name, name, sizeof(User_List[users].name) - 1);
    User_List[users].name[sizeof(User_List[users].name) - 1] = '\0';
    strncpy(User_List[users].email, email, sizeof(User_List[users].email) - 1);
    User_List[users].email[sizeof(User_List[users].email) - 1] = '\0';
    strncpy(User_List[users].phone, phone, sizeof(User_List[users].phone) - 1);
    User_List[users].phone[sizeof(User_List[users].phone) - 1] = '\0';

    users++;
    registration_state = true;
    saveUsers();
    refresh_user_view();

    gchar *msg = g_strdup_printf("Registration successful! Welcome, %s!", User_List[users - 1].name);
    show_message("Success", msg, GTK_MESSAGE_INFO);
    g_free(msg);
}

static void on_edit_user(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    gchar *cur_email = get_selected_column_text(user_tree, UCOL_EMAIL);
    if (!cur_email) {
        show_message("No Selection", "Please select a user to edit.", GTK_MESSAGE_WARNING);
        return;
    }
    int idx = -1;
    for (int u = 0; u < users; u++) {
        if (strcmp(User_List[u].email, cur_email) == 0) { idx = u; break; }
    }
    g_free(cur_email);
    if (idx < 0) {
        show_message("Not Found", "User not found.", GTK_MESSAGE_ERROR);
        return;
    }

    char name[50], email[50], phone[15];
    strcpy(name, User_List[idx].name);
    strcpy(email, User_List[idx].email);
    strcpy(phone, User_List[idx].phone);

    const char *labels[3] = { "Name:", "Email:", "Phone:" };
    char *buffers[3] = { name, email, phone };
    int lens[3] = { 50, 50, 15 };
    if (!multi_field_dialog(GTK_WINDOW(main_window), "Edit User", labels, buffers, lens, NULL, 3))
        return;

    strncpy(User_List[idx].name, name, sizeof(User_List[idx].name) - 1);
    User_List[idx].name[sizeof(User_List[idx].name) - 1] = '\0';
    strncpy(User_List[idx].email, email, sizeof(User_List[idx].email) - 1);
    User_List[idx].email[sizeof(User_List[idx].email) - 1] = '\0';
    strncpy(User_List[idx].phone, phone, sizeof(User_List[idx].phone) - 1);
    User_List[idx].phone[sizeof(User_List[idx].phone) - 1] = '\0';

    saveUsers();
    refresh_user_view();
    show_message("Success", "User information updated.", GTK_MESSAGE_INFO);
}

static void on_delete_user(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    gchar *cur_email = get_selected_column_text(user_tree, UCOL_EMAIL);
    if (!cur_email) {
        show_message("No Selection", "Please select a user to delete.", GTK_MESSAGE_WARNING);
        return;
    }
    int idx = -1;
    for (int u = 0; u < users; u++) {
        if (strcmp(User_List[u].email, cur_email) == 0) { idx = u; break; }
    }
    if (idx < 0) {
        g_free(cur_email);
        show_message("Not Found", "User not found.", GTK_MESSAGE_ERROR);
        return;
    }

    gchar *msg = g_strdup_printf("Delete user %s?", User_List[idx].name);
    gboolean yes = confirm_dialog(msg);
    g_free(msg);
    g_free(cur_email);
    if (!yes) return;

    for (int j = idx; j < users - 1; j++) {
        User_List[j] = User_List[j + 1];
    }
    users--;
    saveUsers();
    refresh_user_view();
    show_message("Deleted", "User information deleted successfully.", GTK_MESSAGE_INFO);
}

static void on_search_user(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    char param[50] = "";
    if (!single_entry_dialog(GTK_WINDOW(main_window), "Search User", "Name or Email:", param, 50))
        return;

    for (int u = 0; u < users; u++) {
        if (strstr(User_List[u].name, param) != NULL || strstr(User_List[u].email, param) != NULL) {
            gchar *msg = g_strdup_printf("User Found:\n\nName: %s\nEmail: %s\nPhone: %s",
                User_List[u].name, User_List[u].email, User_List[u].phone);
            show_message("User Found", msg, GTK_MESSAGE_INFO);
            g_free(msg);
            return;
        }
    }
    show_message("Not Found", "User not found.", GTK_MESSAGE_WARNING);
}

/* ------------------------------ Login / registration ------------------------------ */

gboolean SignIn(const char *username, const char *upassword) {
    /* PORT FIX: check against `users` (the actual registered count) rather
     * than MAX_USERS, and return a result instead of recursing into the
     * login dialog. */
    for (int b = 0; b < users; b++) {
        if (strcmp(username, User_List[b].name) == 0 && strcmp(password, upassword) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static void do_startup_flow(void) {
    readUsers();

    if (!registration_state || users == 0) {
        show_message("Welcome", "Kindly register your information to continue using the app.", GTK_MESSAGE_INFO);
        for (;;) {
            char name[50] = "", email[50] = "", phone[15] = "";
            const char *labels[3] = { "Name:", "Email:", "Phone:" };
            char *buffers[3] = { name, email, phone };
            int lens[3] = { 50, 50, 15 };

            gboolean ok = multi_field_dialog(GTK_WINDOW(main_window), "Register", labels, buffers, lens, NULL, 3);
            if (!ok) {
                /* First-run registration is mandatory to use the app */
                show_message("Registration Required", "You must register to use NetChecker.", GTK_MESSAGE_WARNING);
                continue;
            }
            if (name[0] == '\0' || email[0] == '\0') {
                show_message("Invalid Input", "Name and email are required.", GTK_MESSAGE_ERROR);
                continue;
            }
            strncpy(User_List[0].name, name, sizeof(User_List[0].name) - 1);
            User_List[0].name[sizeof(User_List[0].name) - 1] = '\0';
            strncpy(User_List[0].email, email, sizeof(User_List[0].email) - 1);
            User_List[0].email[sizeof(User_List[0].email) - 1] = '\0';
            strncpy(User_List[0].phone, phone, sizeof(User_List[0].phone) - 1);
            User_List[0].phone[sizeof(User_List[0].phone) - 1] = '\0';
            users = 1;
            registration_state = true;
            saveUsers();
            break;
        }

        gchar *welcome = g_strdup_printf(
            "Welcome to the NetChecker App, %s!\n\n"
            "This application allows you to monitor the status of your network "
            "devices. You can add devices, edit their information, delete them, "
            "and check their status in real-time. The app will also alert you if "
            "any device becomes inactive.", User_List[0].name);
        show_message("Welcome", welcome, GTK_MESSAGE_INFO);
        g_free(welcome);
    } else {
        open_file();
        for (;;) {
            char uname[50] = "", upass[MAX_PASSWORD_LENGTH] = "";
            const char *labels[2] = { "Username:", "Password:" };
            char *buffers[2] = { uname, upass };
            int lens[2] = { 50, MAX_PASSWORD_LENGTH };
            gboolean mask[2] = { FALSE, TRUE };

            if (!multi_field_dialog(GTK_WINDOW(main_window), "Login", labels, buffers, lens, mask, 2)) {
                exit(0); /* user cancelled login */
            }
            if (SignIn(uname, upass)) {
                gchar *msg = g_strdup_printf("Sign In successful.\nWelcome back, %s. What's new?", uname);
                show_message("Signed In", msg, GTK_MESSAGE_INFO);
                g_free(msg);
                break;
            }
            show_message("Login Failed", "Invalid user or password. Try again.", GTK_MESSAGE_ERROR);
        }
    }
}

/* ------------------------------ Main window construction ------------------------------ */

static void add_text_column(GtkWidget *tree, const char *title, int col_id) {
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(title, renderer, "text", col_id, NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_min_width(column, 100);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
}

static void on_main_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget; (void)user_data;
    if (monitor_started) {
        stop_flag = true;
        pthread_join(monitor_thread, NULL);
    }
}

/* Dark, NOC-console style CSS for the app. Scoped to the ".netchecker-dark"
 * class (applied only to the main window) so dialogs -- Login, Add Device,
 * message/confirm popups -- keep the system's normal light theme with
 * ordinary black-on-white text instead of inheriting a half-applied dark
 * style that made their text nearly invisible. */
static void apply_dark_theme(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        ".netchecker-dark { background-color: #1a1d23; }"
        ".netchecker-dark treeview { background-color: #1f2329; color: #d8dee9; font-family: 'JetBrains Mono', monospace; }"
        ".netchecker-dark treeview:selected { background-color: #2e6f9e; }"
        ".netchecker-dark treeview header button { background-color: #262b33; color: #8fb0c9; font-weight: bold; }"
        ".netchecker-dark button { background-color: #2a2f38; color: #e5e9f0; border: 1px solid #3a3f4a; }"
        ".netchecker-dark button:hover { background-color: #343a45; }"
        ".netchecker-dark statusbar { background-color: #14161a; color: #7c8798; }"
        ".netchecker-dark label { color: #d8dee9; }"
        ".netchecker-dark notebook tab { background-color: #20242b; color: #8fb0c9; padding: 6px 12px; }"
        ".netchecker-dark notebook tab:checked { background-color: #1f2329; color: #ffffff; }";
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static GtkWidget *build_devices_tab(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_add = gtk_button_new_with_label("Add Device");
    GtkWidget *btn_edit = gtk_button_new_with_label("Edit Device");
    GtkWidget *btn_delete = gtk_button_new_with_label("Delete Device");
    GtkWidget *btn_search = gtk_button_new_with_label("Search by IP");
    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh");
    monitor_button = gtk_button_new_with_label("Start Monitor");

    gtk_box_pack_start(GTK_BOX(toolbar), btn_add, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_edit, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_delete, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_search, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(toolbar), monitor_button, FALSE, FALSE, 0);

    device_store = gtk_list_store_new(N_DEVICE_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    device_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(device_store));
    add_text_column(device_tree, "Device Name", COL_NAME);
    add_text_column(device_tree, "IP Address", COL_IP);
    add_text_column(device_tree, "Location", COL_LOCATION);
    add_text_column(device_tree, "Status", COL_STATUS);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), device_tree);

    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    g_signal_connect(btn_add, "clicked", G_CALLBACK(on_add_device), NULL);
    g_signal_connect(btn_edit, "clicked", G_CALLBACK(on_edit_device), NULL);
    g_signal_connect(btn_delete, "clicked", G_CALLBACK(on_delete_device), NULL);
    g_signal_connect(btn_search, "clicked", G_CALLBACK(on_search_device), NULL);
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_refresh_devices_clicked), NULL);
    g_signal_connect(monitor_button, "clicked", G_CALLBACK(on_toggle_monitor), NULL);

    return vbox;
}

static GtkWidget *build_users_tab(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_reg = gtk_button_new_with_label("Register User");
    GtkWidget *btn_edit = gtk_button_new_with_label("Edit User");
    GtkWidget *btn_delete = gtk_button_new_with_label("Delete User");
    GtkWidget *btn_search = gtk_button_new_with_label("Search User");
    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh");

    gtk_box_pack_start(GTK_BOX(toolbar), btn_reg, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_edit, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_delete, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_search, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_refresh, FALSE, FALSE, 0);

    user_store = gtk_list_store_new(N_USER_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    user_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(user_store));
    add_text_column(user_tree, "Name", UCOL_NAME);
    add_text_column(user_tree, "Email", UCOL_EMAIL);
    add_text_column(user_tree, "Phone", UCOL_PHONE);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), user_tree);

    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    g_signal_connect(btn_reg, "clicked", G_CALLBACK(on_register_user), NULL);
    g_signal_connect(btn_edit, "clicked", G_CALLBACK(on_edit_user), NULL);
    g_signal_connect(btn_delete, "clicked", G_CALLBACK(on_delete_user), NULL);
    g_signal_connect(btn_search, "clicked", G_CALLBACK(on_search_user), NULL);
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_refresh_users_clicked), NULL);

    return vbox;
}

static void build_main_window(GtkApplication *app) {
    main_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(main_window), "NetChecker");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 820, 520);
    gtk_style_context_add_class(gtk_widget_get_style_context(main_window), "netchecker-dark");
    g_signal_connect(main_window, "destroy", G_CALLBACK(on_main_window_destroy), NULL);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *notebook = gtk_notebook_new();

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_devices_tab(), gtk_label_new("Devices"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_users_tab(), gtk_label_new("Users"));

    statusbar = gtk_statusbar_new();
    statusbar_ctx = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "main");
    gtk_statusbar_push(GTK_STATUSBAR(statusbar), statusbar_ctx, "Ready.");

    gtk_box_pack_start(GTK_BOX(outer), notebook, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(outer), statusbar, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(main_window), outer);
}

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    apply_dark_theme();
    build_main_window(app);
    gtk_widget_show_all(main_window);
    do_startup_flow();
    refresh_device_view();
    refresh_user_view();
}

int main(int argc, char **argv) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData); /* required by InetPtonA/ICMP calls in ping_host() */
#endif

    GtkApplication *app = gtk_application_new("com.netchecker.gui", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

#ifdef _WIN32
    WSACleanup();
#endif
    return status;
}
