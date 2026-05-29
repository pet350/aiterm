#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "policy_dao.h"
#include "gui.h"
#include "utils.h"

PolicyRecord* get_policy_for_command(AppContext *app, const char *cmd) {
    if (!app->global_db_conn) return NULL;

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
        // Allocate and populate the structure correctly
        p = g_malloc0(sizeof(PolicyRecord));
        p->name = g_strdup(cmd);
        p->type = g_strdup(row[0]);
        p->risk = atoi(row[1]);
    }

    mysql_free_result(res);
    return p;
}

// Updated to use the structure instead of individual params
gboolean set_command_policy(AppContext *app, PolicyRecord *p) {
    if (!app->global_db_conn || !p) return FALSE;

    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO command_policy (command_name, policy_type, risk_level) "
             "VALUES ('%s', '%s', %d) "
             "ON DUPLICATE KEY UPDATE policy_type='%s', risk_level=%d", 
             p->name, p->type, p->risk, p->type, p->risk);

    pthread_mutex_lock(&app->db_mutex);
    int status = mysql_query(app->global_db_conn, query);
    pthread_mutex_unlock(&app->db_mutex);

    return (status == 0);
}

void free_policy_record(PolicyRecord *p) {
    if (!p) return;
    g_free(p->name);
    g_free(p->type);
    g_free(p);
}

// Add these functions to your existing policy_dao.c file

gboolean delete_command_policy(AppContext *app, const char *cmd) {
    if (!app->global_db_conn || !cmd) return FALSE;

    char query[512];
    snprintf(query, sizeof(query), 
             "DELETE FROM command_policy WHERE command_name = '%s'", cmd);

    pthread_mutex_lock(&app->db_mutex);
    int status = mysql_query(app->global_db_conn, query);
    pthread_mutex_unlock(&app->db_mutex);

    return (status == 0);
}

GList* get_all_policies(AppContext *app) {
    if (!app->global_db_conn) return NULL;

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
    return list;
}
