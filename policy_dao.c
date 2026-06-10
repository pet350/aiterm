#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "policy_dao.h"
#include "gui.h"
#include "utils.h"

/* ========================================================================== */
/* EXISTING SYNCHRONOUS FUNCTIONS (Internal Workers)                          */
/* Introduced in  0.8.4-alpha						      */
/* ========================================================================== */

PolicyRecord* get_policy_for_command(AppContext *app, const char *cmd) {
    if (!app->global_db_conn) return NULL;

    mysql_thread_init();

    // LOCK: Ensure exclusive access to the database connection
    pthread_mutex_lock(&app->db_mutex);

    char query[512];
    snprintf(query, sizeof(query),
             "SELECT policy_type, risk_level FROM command_policy WHERE command_name = '%s'", cmd);

    pthread_mutex_lock(&app->db_mutex);
    if (mysql_query(app->global_db_conn, query)) {
        pthread_mutex_unlock(&app->db_mutex);
        return NULL;
    }

    MYSQL_RES *res = mysql_store_result(app->global_db_conn);
    pthread_mutex_unlock(&app->db_mutex);

    if (!res) return NULL;

    MYSQL_ROW row = mysql_fetch_row(res);
    PolicyRecord *p = NULL;
    if (row) {
        p = g_malloc0(sizeof(PolicyRecord));
        p->name = g_strdup(cmd);
        p->type = g_strdup(row[0]);
        p->risk = atoi(row[1]);
    }

    mysql_free_result(res);
    pthread_mutex_unlock(&app->db_mutex);
    mysql_thread_end();
    return p;
}

gboolean set_command_policy(AppContext *app, PolicyRecord *p) {
    if (!app->global_db_conn || !p) return FALSE;

    mysql_thread_init();

    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO command_policy (command_name, policy_type, risk_level) "
             "VALUES ('%s', '%s', %d) "
             "ON DUPLICATE KEY UPDATE policy_type='%s', risk_level=%d", 
             p->name, p->type, p->risk, p->type, p->risk);

    pthread_mutex_lock(&app->db_mutex);
    int status = mysql_query(app->global_db_conn, query);
    pthread_mutex_unlock(&app->db_mutex);

    mysql_thread_end();
    return (status == 0);
}

gboolean delete_command_policy(AppContext *app, const char *cmd) {
    if (!app->global_db_conn || !cmd) return FALSE;

    mysql_thread_init();
    char query[512];
    snprintf(query, sizeof(query),
             "DELETE FROM command_policy WHERE command_name = '%s'", cmd);

    pthread_mutex_lock(&app->db_mutex);
    int status = mysql_query(app->global_db_conn, query);
    pthread_mutex_unlock(&app->db_mutex);

    mysql_thread_end();
    return (status == 0);
}

GList* get_all_policies(AppContext *app) {
    if (!app->global_db_conn) return NULL;
    mysql_thread_init();
    char query[] = "SELECT command_name, policy_type, risk_level FROM command_policy ORDER BY command_name ASC";

    pthread_mutex_lock(&app->db_mutex);
    if (mysql_query(app->global_db_conn, query)) {
        pthread_mutex_unlock(&app->db_mutex);
        return NULL;
    }

    MYSQL_RES *res = mysql_store_result(app->global_db_conn);
    pthread_mutex_unlock(&app->db_mutex);

    if (!res) return NULL;

    GList *list = NULL;
    MYSQL_ROW row;

    while ((row = mysql_fetch_row(res))) {
        PolicyRecord *p = g_malloc0(sizeof(PolicyRecord));
        p->name = g_strdup(row[0]);
        p->type = g_strdup(row[1]);
        p->risk = atoi(row[2]);
        list = g_list_append(list, p);
    }

    mysql_free_result(res);
    mysql_thread_end();
    return list;
}

void free_policy_record(PolicyRecord *p) {
    if (!p) return;
    g_free(p->name);
    g_free(p->type);
    g_free(p);
}


/* ========================================================================== */
/* ASYNCHRONOUS THREADED WRAPPERS                                             */
/* New for 0.8.4-delta an effort to move all mysql to their own threads       */
/* ========================================================================== */

static gboolean get_policy_idle_cb(gpointer data) {
    GetPolicyArgs *args = (GetPolicyArgs*)data;
    if (args->callback) args->callback(args->result, args->user_data);
    g_free(args->cmd);
    g_free(args);
    return FALSE; // Removes source from loop
}

static void* get_policy_worker(void *data) {
    GetPolicyArgs *args = (GetPolicyArgs*)data;
    mysql_thread_init();
    args->result = get_policy_for_command(args->app, args->cmd);
    mysql_thread_end();
    g_idle_add(get_policy_idle_cb, args);
    return NULL;
}

void get_policy_for_command_async(AppContext *app, const char *cmd, void (*callback)(PolicyRecord*, gpointer), gpointer user_data) {
    GetPolicyArgs *args = g_malloc0(sizeof(GetPolicyArgs));
    args->app = app;
    args->cmd = g_strdup(cmd);
    args->callback = callback;
    args->user_data = user_data;

    pthread_t thread;
    pthread_create(&thread, NULL, get_policy_worker, args);
    pthread_detach(thread);
}


// --- 2. Async Set Policy ---
typedef struct {
    AppContext *app;
    PolicyRecord *p;
    void (*callback)(gboolean success, gpointer data);
    gpointer user_data;
    gboolean result;
} SetPolicyArgs;

static gboolean set_policy_idle_cb(gpointer data) {
    SetPolicyArgs *args = (SetPolicyArgs*)data;
    if (args->callback) args->callback(args->result, args->user_data);
    g_free(args);
    return FALSE;
}

static void* set_policy_worker(void *data) {
    SetPolicyArgs *args = (SetPolicyArgs*)data;
    mysql_thread_init();
    args->result = set_command_policy(args->app, args->p);
    mysql_thread_end();
    g_idle_add(set_policy_idle_cb, args);
    return NULL;
}

void set_command_policy_async(AppContext *app, PolicyRecord *p, void (*callback)(gboolean, gpointer), gpointer user_data) {
    SetPolicyArgs *args = g_malloc0(sizeof(SetPolicyArgs));
    args->app = app;
    args->p = p;
    args->callback = callback;
    args->user_data = user_data;

    pthread_t thread;
    pthread_create(&thread, NULL, set_policy_worker, args);
    pthread_detach(thread);
}


// --- 3. Async Delete Policy ---
typedef struct {
    AppContext *app;
    char *cmd;
    void (*callback)(gboolean success, gpointer data);
    gpointer user_data;
    gboolean result;
} DeletePolicyArgs;

static gboolean delete_policy_idle_cb(gpointer data) {
    DeletePolicyArgs *args = (DeletePolicyArgs*)data;
    if (args->callback) args->callback(args->result, args->user_data);
    g_free(args->cmd);
    g_free(args);
    return FALSE;
}

static void* delete_policy_worker(void *data) {
    DeletePolicyArgs *args = (DeletePolicyArgs*)data;
    mysql_thread_init();
    args->result = delete_command_policy(args->app, args->cmd);
    mysql_thread_end();
    g_idle_add(delete_policy_idle_cb, args);
    return NULL;
}

void delete_command_policy_async(AppContext *app, const char *cmd, void (*callback)(gboolean, gpointer), gpointer user_data) {
    DeletePolicyArgs *args = g_malloc0(sizeof(DeletePolicyArgs));
    args->app = app;
    args->cmd = g_strdup(cmd);
    args->callback = callback;
    args->user_data = user_data;

    pthread_t thread;
    pthread_create(&thread, NULL, delete_policy_worker, args);
    pthread_detach(thread);
}


// --- 4. Async Get All Policies ---
typedef struct {
    AppContext *app;
    void (*callback)(GList *list, gpointer data);
    gpointer user_data;
    GList *result;
} GetAllPoliciesArgs;

static gboolean get_all_policies_idle_cb(gpointer data) {
    GetAllPoliciesArgs *args = (GetAllPoliciesArgs*)data;
    if (args->callback) args->callback(args->result, args->user_data);
    g_free(args);
    return FALSE;
}

static void* get_all_policies_worker(void *data) {
    GetAllPoliciesArgs *args = (GetAllPoliciesArgs*)data;
    mysql_thread_init();
    args->result = get_all_policies(args->app);
    mysql_thread_end();
    g_idle_add(get_all_policies_idle_cb, args);
    return NULL;
}

void get_all_policies_async(AppContext *app, void (*callback)(GList*, gpointer), gpointer user_data) {
    GetAllPoliciesArgs *args = g_malloc0(sizeof(GetAllPoliciesArgs));
    args->app = app;
    args->callback = callback;
    args->user_data = user_data;

    pthread_t thread;
    pthread_create(&thread, NULL, get_all_policies_worker, args);
    pthread_detach(thread);
}
