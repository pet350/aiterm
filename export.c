// part of the aiterm project
// export.c
// Program file for exporting context in various formats
// By: Peter Talbott
// Assisted by: Gemini and OpenAI
// aiterm The terminal emulator with an AI Pane
// September 2026

#include <gtk/gtk.h>
#include <vte/vte.h>
#include <string.h>
#include <stdlib.h>
#include <json-c/json.h>

#include "export.h"
#include "gui.h"

// ---------- helper to get active VTE ----------
static VteTerminal *get_active_terminal(AppContext *app) {
    int page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->gui.notebook));
    if (page >= 0 && page < MAX_TABS) {
        GtkWidget *vte = app->tabs[page].vte;
        if (vte && VTE_IS_TERMINAL(vte))
            return VTE_TERMINAL(vte);
    }
    if (app->gui.terminal_view && VTE_IS_TERMINAL(app->gui.terminal_view))
        return VTE_TERMINAL(app->gui.terminal_view);
    return NULL;
}

// ---------- get terminal scrollback text ----------
char *get_terminal_text(AppContext *app) {
    VteTerminal *vte = get_active_terminal(app);
    if (!vte) return g_strdup("");

    char *text = NULL;
#if VTE_CHECK_VERSION(0, 50, 0)
    text = vte_terminal_get_text_range(vte, 0, 0, -1, -1,
                                       NULL,
                                       NULL, NULL);
#else
    text = vte_terminal_get_text(vte, NULL, NULL, NULL);
#endif
    return text ? text : g_strdup("");
}

// ---------- get AI pane text ----------
char *get_ai_pane_text(AppContext *app) {
    if (!app->gui.gemini_view || !GTK_IS_TEXT_VIEW(app->gui.gemini_view))
        return g_strdup("");

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gui.gemini_view));
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buf, &start);
    gtk_text_buffer_get_end_iter(buf, &end);
    return gtk_text_buffer_get_text(buf, &start, &end, FALSE);
}

// ---------- format helpers ----------
char *escape_html(const char *text) {
    return g_markup_escape_text(text, -1);
}

char *escape_xml(const char *text) {
    // g_markup_escape_text is sufficient for XML as well
    return g_markup_escape_text(text, -1);
}

char *build_plain(const char *ai, const char *term, int include_both) {
    if (include_both)
        return g_strdup_printf("=== AI Pane ===\n%s\n\n=== Terminal ===\n%s\n", ai, term);
    return g_strdup(term ? term : "");  // placeholders; actually selected earlier
}

char *build_html(const char *ai, const char *term, int include_both) {
    char *ai_esc = escape_html(ai ? ai : "");
    char *term_esc = escape_html(term ? term : "");

    GString *html = g_string_new("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n<title>aiterm Session Export</title>\n</head>\n<body>\n");

    if (include_both) {
        g_string_append_printf(html, "<h2>AI Pane</h2>\n<pre>%s</pre>\n<hr>\n<h2>Terminal</h2>\n<pre>%s</pre>\n", ai_esc, term_esc);
    } else {
        // If only one selected, caller passes only that field as non-NULL
        const char *only = ai ? ai_esc : term_esc;
        g_string_append_printf(html, "<pre>%s</pre>\n", only);
    }

    g_string_append(html, "</body>\n</html>\n");
    g_free(ai_esc);
    g_free(term_esc);
    return g_string_free(html, FALSE);
}

char *build_json(const char *ai, const char *term, int include_both) {
    json_object *jobj = json_object_new_object();
    if (ai) json_object_object_add(jobj, "ai_text", json_object_new_string(ai));
    if (term) json_object_object_add(jobj, "terminal_text", json_object_new_string(term));
    const char *str = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PRETTY);
    char *ret = g_strdup(str);
    json_object_put(jobj);
    return ret;
}

char *build_xml(const char *ai, const char *term, int include_both) {
    char *ai_esc = escape_xml(ai ? ai : "");
    char *term_esc = escape_xml(term ? term : "");

    GString *xml = g_string_new("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<session>\n");
    if (ai) {
        g_string_append_printf(xml, "  <ai>\n    %s\n  </ai>\n", ai_esc);
    }
    if (term) {
        g_string_append_printf(xml, "  <terminal>\n    %s\n  </terminal>\n", term_esc);
    }
    g_string_append(xml, "</session>\n");

    g_free(ai_esc);
    g_free(term_esc);
    return g_string_free(xml, FALSE);
}

// ---------- public function ----------
void export_session(AppContext *app) {
    if (!app) return;

    // Step 1: ask for content and format
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Export Session",
        GTK_WINDOW(app->gui.window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Next", GTK_RESPONSE_ACCEPT,
        NULL
    );

    // Apply custom theme style class to dialog
    GtkStyleContext *dialog_context = gtk_widget_get_style_context(dialog);
    gtk_style_context_add_class(dialog_context, "session-dialog");

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkStyleContext *content_context = gtk_widget_get_style_context(content);
    gtk_style_context_add_class(content_context, "session-dialog");

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkStyleContext *vbox_context = gtk_widget_get_style_context(vbox);
    gtk_style_context_add_class(vbox_context, "session-dialog");

    gtk_container_add(GTK_CONTAINER(content), vbox);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    // Content selection
    GtkWidget *label_content = gtk_label_new("Export content:");
    gtk_box_pack_start(GTK_BOX(vbox), label_content, FALSE, FALSE, 0);

    GtkWidget *radio_ai = gtk_radio_button_new_with_label(NULL, "AI Pane Only");
    GtkWidget *radio_term = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_ai), "Terminal Only");
    GtkWidget *radio_both = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_ai), "Both AI and Terminal");
    gtk_box_pack_start(GTK_BOX(vbox), radio_ai, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), radio_term, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), radio_both, FALSE, FALSE, 0);

    // Format selection
    GtkWidget *label_fmt = gtk_label_new("Export format:");
    gtk_box_pack_start(GTK_BOX(vbox), label_fmt, FALSE, FALSE, 0);

    GtkWidget *combo_fmt = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_fmt), "Plain Text");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_fmt), "HTML");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_fmt), "JSON");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_fmt), "XML");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_fmt), 0);
    gtk_box_pack_start(GTK_BOX(vbox), combo_fmt, FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dialog);
        return;
    }

    int content_mode;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_ai))) content_mode = 0;
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_term))) content_mode = 1;
    else content_mode = 2;

    int fmt = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_fmt));
    gtk_widget_destroy(dialog);

    // Step 2: collect content based on selection
    char *ai_text = NULL, *term_text = NULL;
    if (content_mode == 0 || content_mode == 2) {
        ai_text = get_ai_pane_text(app);
    }
    if (content_mode == 1 || content_mode == 2) {
        term_text = get_terminal_text(app);
    }

    // If nothing selected (shouldn't happen) or empty, warn
    if ((content_mode == 0 && (!ai_text || ai_text[0] == '\0')) ||
        (content_mode == 1 && (!term_text || term_text[0] == '\0')) ||
        (content_mode == 2 && ((!ai_text || ai_text[0] == '\0') && (!term_text || term_text[0] == '\0')))) {
        GtkWidget *warn = gtk_message_dialog_new(GTK_WINDOW(app->gui.window),
                                                 GTK_DIALOG_MODAL,
                                                 GTK_MESSAGE_INFO,
                                                 GTK_BUTTONS_OK,
                                                 "No content to export.");
        gtk_dialog_run(GTK_DIALOG(warn));
        gtk_widget_destroy(warn);
        g_free(ai_text);
        g_free(term_text);
        return;
    }

    // Step 3: file chooser
    GtkFileChooserNative *chooser = gtk_file_chooser_native_new("Export Session",
                                                      GTK_WINDOW(app->gui.window),
                                                      GTK_FILE_CHOOSER_ACTION_SAVE,
                                                      "_Save", "_Cancel");
    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(chooser), TRUE);

    // Suggest filename based on format
    const char *ext = (fmt == 0) ? "txt" : (fmt == 1) ? "html" : (fmt == 2) ? "json" : "xml";
    char *suggested = g_strdup_printf("aiterm_session.%s", ext);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), suggested);
    g_free(suggested);

    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(chooser)) != GTK_RESPONSE_ACCEPT) {
        g_object_unref(chooser);
        g_free(ai_text);
        g_free(term_text);
        return;
    }

    char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    g_object_unref(chooser);

    // Step 4: generate output
    char *output = NULL;
    switch (fmt) {
        case 0: // plain text
            if (content_mode == 0) output = g_strdup(ai_text);
            else if (content_mode == 1) output = g_strdup(term_text);
            else output = g_strdup_printf("=== AI Pane ===\n%s\n\n=== Terminal ===\n%s\n", ai_text, term_text);
            break;
        case 1: // HTML
            output = build_html(ai_text, term_text, content_mode == 2);
            break;
        case 2: // JSON
            output = build_json(ai_text, term_text, content_mode == 2);
            break;
        case 3: // XML
            output = build_xml(ai_text, term_text, content_mode == 2);
            break;
    }

    if (output && output[0] != '\0') {
        GError *err = NULL;
        if (!g_file_set_contents(filename, output, -1, &err)) {
            GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(app->gui.window),
                                                    GTK_DIALOG_MODAL,
                                                    GTK_MESSAGE_ERROR,
                                                    GTK_BUTTONS_CLOSE,
                                                    "Failed to write file: %s",
                                                    err ? err->message : "unknown error");
            gtk_dialog_run(GTK_DIALOG(msg));
            gtk_widget_destroy(msg);
            g_clear_error(&err);
        }
    }

    g_free(output);
    g_free(filename);
    g_free(ai_text);
    g_free(term_text);
}

