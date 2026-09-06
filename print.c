// part of the aiterm project
// print.c
// Program file for handling printing
// By: Peter Talbott
// Assisted by: Gemini and OpenAI
// aiterm The terminal emulator with an AI Pane
// September 2026

#include <gtk/gtk.h>
#include <vte/vte.h>
#include <pango/pangocairo.h>
#include <string.h>
#include <stdlib.h>

#include "print.h"
#include "gui.h"

// ---------- helper to get active VTE ----------
static VteTerminal *get_active_terminal(AppContext *app) {
    int page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->gui.notebook));
    if (page >= 0 && page < MAX_TABS) {
        GtkWidget *vte = app->tabs[page].vte;
        if (vte && VTE_IS_TERMINAL(vte))
            return VTE_TERMINAL(vte);
    }
    // fallback to main terminal view if no tab
    if (app->gui.terminal_view && VTE_IS_TERMINAL(app->gui.terminal_view))
        return VTE_TERMINAL(app->gui.terminal_view);
    return NULL;
}

// ---------- get terminal scrollback text ----------
static char *get_terminal_text(AppContext *app) {
    VteTerminal *vte = get_active_terminal(app);
    if (!vte) return g_strdup("");

    char *text = NULL;

#if VTE_CHECK_VERSION(0, 50, 0)
    // modern VTE: use range extraction (full scrollback)
    text = vte_terminal_get_text_range(vte,
                                       0, 0,           // start row/col
                                       -1, -1,         // end row/col (last)
                                       NULL,
                                       NULL, NULL);
#else
    // older API
    text = vte_terminal_get_text(vte, NULL, NULL, NULL);
#endif

    return text ? text : g_strdup("");
}

// ---------- get AI pane text ----------
static char *get_ai_pane_text(AppContext *app) {
    if (!app->gui.gemini_view || !GTK_IS_TEXT_VIEW(app->gui.gemini_view))
        return g_strdup("");

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->gui.gemini_view));
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buf, &start);
    gtk_text_buffer_get_end_iter(buf, &end);
    return gtk_text_buffer_get_text(buf, &start, &end, FALSE);
}

// ---------- print data ----------
typedef struct {
    char *text;
} PrintData;

// ---------- print data cleanup ----------
static void print_data_free_notify(gpointer data) {
    PrintData *pd = (PrintData *)data;
    if (pd) {
        if (pd->text) {
            g_free(pd->text);
            pd->text = NULL;
        }
        g_free(pd);
    }
}

// ---------- print callbacks ----------
static void begin_print(GtkPrintOperation *op, GtkPrintContext *ctx, gpointer user_data) {
    PrintData *pd = user_data;
    int lines_per_page = 60;
    int total_lines = 0;
    char *p = pd->text;
    while (p && *p) {
        if (*p == '\n') total_lines++;
        p++;
    }
    total_lines++;
    int n_pages = (total_lines + lines_per_page - 1) / lines_per_page;
    gtk_print_operation_set_n_pages(op, MAX(1, n_pages));
}

static void draw_page(GtkPrintOperation *op, GtkPrintContext *ctx, int page_nr, gpointer user_data) {
    PrintData *pd = user_data;
    if (!pd || !pd->text) return;

    cairo_t *cr = gtk_print_context_get_cairo_context(ctx);
    if (!cr) return;

    double width = gtk_print_context_get_width(ctx);
    double height = gtk_print_context_get_height(ctx);

    PangoLayout *layout = gtk_print_context_create_pango_layout(ctx);
    if (!layout) return;

    PangoFontDescription *desc = pango_font_description_from_string("Monospace 10");
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    pango_layout_set_text(layout, pd->text, -1);
    pango_layout_set_width(layout, (int)(width * PANGO_SCALE));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);

    int lines_per_page = 60;
    int start_line = page_nr * lines_per_page;
    int end_line = start_line + lines_per_page;

    GSList *lines = pango_layout_get_lines(layout);
    int num_lines = pango_layout_get_line_count(layout);
    int first_line_on_page = start_line;
    int last_line_on_page = MIN(end_line, num_lines) - 1;

    cairo_set_source_rgb(cr, 0, 0, 0);
    double y = 0;
    int current_index = 0;

    for (GSList *l = lines; l != NULL; l = l->next) {
        if (current_index >= first_line_on_page && current_index <= last_line_on_page) {
            PangoLayoutLine *line = (PangoLayoutLine *)l->data;
            PangoRectangle logical_rect;
            pango_layout_line_get_extents(line, NULL, &logical_rect);

            cairo_move_to(cr, 0, y);
            pango_cairo_show_layout_line(cr, line);

            y += (logical_rect.height / (double)PANGO_SCALE);
            if (y > height) break;
        }
        current_index++;
    }
    g_object_unref(layout);
}

static void end_print(GtkPrintOperation *op, GtkPrintContext *ctx, gpointer user_data) {
    // Keep empty
}

// ---------- public function ----------
void print_session(AppContext *app) {
    if (!app) return;

    // Step 1: ask what to print
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Print Session",
        GTK_WINDOW(app->gui.window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Print", GTK_RESPONSE_ACCEPT,
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

    GtkWidget *label = gtk_label_new("Select content to print:");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    GtkWidget *radio_ai = gtk_radio_button_new_with_label(NULL, "AI Pane Only");
    GtkWidget *radio_term = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_ai), "Terminal Only");
    GtkWidget *radio_both = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_ai), "Both AI and Terminal");

    gtk_box_pack_start(GTK_BOX(vbox), radio_ai, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), radio_term, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), radio_both, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dialog);
        return;
    }

    // Determine selection
    int mode;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_ai))) mode = 0;
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_term))) mode = 1;
    else mode = 2;
    gtk_widget_destroy(dialog);

    // Step 2: collect content
    char *ai_text = get_ai_pane_text(app);
    char *term_text = get_terminal_text(app);
    char *combined = NULL;

    switch (mode) {
        case 0: // AI only
            combined = g_strdup(ai_text);
            break;
        case 1: // Terminal only
            combined = g_strdup(term_text);
            break;
        case 2: // Both
            combined = g_strdup_printf("=== AI Pane ===\n%s\n\n=== Terminal ===\n%s\n", ai_text, term_text);
            break;
    }

    g_free(ai_text);
    g_free(term_text);

    if (!combined || combined[0] == '\0') {
        GtkWidget *warn = gtk_message_dialog_new(GTK_WINDOW(app->gui.window),
                                                 GTK_DIALOG_MODAL,
                                                 GTK_MESSAGE_INFO,
                                                 GTK_BUTTONS_OK,
                                                 "Nothing to print.");
        gtk_dialog_run(GTK_DIALOG(warn));
        gtk_widget_destroy(warn);
        g_free(combined);
        return;
    }

    // Step 3: prepare print data and run print operation
    PrintData *pd = g_new0(PrintData, 1);
    pd->text = combined;

    GtkPrintOperation *op = gtk_print_operation_new();
    gtk_print_operation_set_job_name(op, "aiterm Session");
    gtk_print_operation_set_show_progress(op, TRUE);

    // Bind lifetime of pd to op
    g_object_set_data_full(G_OBJECT(op), "print-data", pd, print_data_free_notify);

    g_signal_connect(op, "begin-print", G_CALLBACK(begin_print), pd);
    g_signal_connect(op, "draw-page", G_CALLBACK(draw_page), pd);
    g_signal_connect(op, "end-print", G_CALLBACK(end_print), pd);

    GError *error = NULL;
    GtkPrintOperationResult res = gtk_print_operation_run(op,
                                                          GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                                                          GTK_WINDOW(app->gui.window),
                                                          &error);

    if (res == GTK_PRINT_OPERATION_RESULT_ERROR) {
        GtkWidget *err = gtk_message_dialog_new(GTK_WINDOW(app->gui.window),
                                                GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_CLOSE,
                                                "Printing failed: %s", 
                                                error ? error->message : "Unknown error");
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        if (error) g_error_free(error);
        g_object_unref(op);
    } else if (res == GTK_PRINT_OPERATION_RESULT_APPLY) {
        // Synchronous print finished immediately, safe to unref
        g_object_unref(op);
    }
    // Note: If res == GTK_PRINT_OPERATION_RESULT_IN_PROGRESS (Preview Mode) or
    // GTK_PRINT_OPERATION_RESULT_CANCEL, GTK manages op during the preview window 
    // lifecycle or cleanup, so do not unref(op) here.
}
	
