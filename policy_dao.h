#ifndef POLICY_DAO_H
#define POLICY_DAO_H

#include <glib.h>
#include "gui.h" // Assuming AppContext is defined here

typedef struct {
    char *name;
    char *type;
    int risk;
} PolicyRecord;

typedef struct {
    AppContext *app;
    char *cmd;
    void (*callback)(PolicyRecord *p, gpointer data);
    gpointer user_data;
    PolicyRecord *result;
} GetPolicyArgs;

PolicyRecord* get_policy_for_command(AppContext *app, const char *cmd);
gboolean set_command_policy(AppContext *app, PolicyRecord *p);
gboolean delete_command_policy(AppContext *app, const char *cmd);
GList* get_all_policies(AppContext *app);
void free_policy_record(PolicyRecord *p);

#endif
