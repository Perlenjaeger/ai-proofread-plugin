/*
 * m-proofreader.c - Proofreading logic for AI Proofread Plugin
 *
 * Implements the proofreading workflow including async content
 * retrieval, API calls, and result insertion.
 */

#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <composer/e-msg-composer.h>

#include "m-proofreader.h"
#include "m-chatgpt-api.h"

#define PROOFREAD_WAIT_DELAY_MS 800

typedef struct
{
    MProofreadContext *context;
    gchar *content;
} ProofreadTaskData;

static void proofreader_wait_indicator_clear(MProofreadContext *context);
static void show_error_alert(EMsgComposer *composer, const gchar *error_message);
static void show_no_response_dialog(EMsgComposer *composer);
static void insert_proofread_content(EContentEditor *cnt_editor, const gchar *content);
static gboolean proofreader_wait_indicator_show(gpointer user_data);
static void proofreader_wait_indicator_schedule(MProofreadContext *context);
static ProofreadTaskData *proofread_task_data_new(MProofreadContext *context, const gchar *content);
static void proofread_task_data_free(ProofreadTaskData *data);
static void proofread_task_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable);
static void proofread_task_completed(GObject *source_object, GAsyncResult *result, gpointer user_data);

static gboolean
proofreader_wait_indicator_show(gpointer user_data)
{
    MProofreadContext *context = user_data;
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *box;
    GtkWidget *spinner;
    GtkWidget *label;
    gchar *message;

    if (!context->composer || !GTK_IS_WINDOW(context->composer))
        return G_SOURCE_REMOVE;

    dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), _("AI Proofreading"));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(context->composer));
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_add(GTK_CONTAINER(content_area), box);

    spinner = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_box_pack_start(GTK_BOX(box), spinner, FALSE, FALSE, 0);

    message = g_strdup_printf(_("Proofreading with %s may take a little longer. Please wait..."),
                              context->model ? context->model : "AI");
    label = gtk_label_new(message);
    g_free(message);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);

    gtk_widget_show_all(dialog);

    context->wait_dialog = dialog;
    context->wait_timeout_id = 0;

    return G_SOURCE_REMOVE;
}

static void
proofreader_wait_indicator_schedule(MProofreadContext *context)
{
    if (context->wait_timeout_id != 0 || context->wait_dialog)
        return;

    context->wait_timeout_id = g_timeout_add(PROOFREAD_WAIT_DELAY_MS,
                                             proofreader_wait_indicator_show,
                                             context);
}

static void
proofreader_wait_indicator_clear(MProofreadContext *context)
{
    if (context->wait_timeout_id != 0)
    {
        g_source_remove(context->wait_timeout_id);
        context->wait_timeout_id = 0;
    }

    if (context->wait_dialog)
    {
        gtk_widget_destroy(context->wait_dialog);
        context->wait_dialog = NULL;
    }
}

static ProofreadTaskData *
proofread_task_data_new(MProofreadContext *context, const gchar *content)
{
    ProofreadTaskData *data = g_new0(ProofreadTaskData, 1);
    data->context = context;
    data->content = g_strdup(content);
    return data;
}

static void
proofread_task_data_free(ProofreadTaskData *data)
{
    if (!data)
        return;
    g_free(data->content);
    g_free(data);
}

static void
proofread_task_thread(GTask *task,
                      gpointer source_object,
                      gpointer task_data,
                      GCancellable *cancellable)
{
    ProofreadTaskData *data = task_data;
    MProofreadContext *context = data->context;
    GError *error = NULL;
    gchar *proofread_text;

    proofread_text = m_chatgpt_proofread(
        data->content,
        context->prompt_id,
        context->prompts,
        context->api_key,
        context->model,
        &error);

    if (error)
    {
        g_task_return_error(task, error);
        return;
    }

    g_task_return_pointer(task, proofread_text, g_free);
}

static void
proofread_task_completed(GObject *source_object,
                         GAsyncResult *result,
                         gpointer user_data)
{
    GTask *task = G_TASK(result);
    ProofreadTaskData *data = g_task_get_task_data(task);
    MProofreadContext *context = data->context;
    GError *error = NULL;
    gchar *proofread_text;

    proofreader_wait_indicator_clear(context);

    proofread_text = g_task_propagate_pointer(task, &error);

    if (error)
    {
        g_warning("ChatGPT API error: %s", error->message);
        show_error_alert(context->composer, error->message);
        g_error_free(error);
    }
    else if (!proofread_text)
    {
        show_no_response_dialog(context->composer);
    }
    else
    {
        insert_proofread_content(context->cnt_editor, proofread_text);
        g_free(proofread_text);
    }

    m_proofreader_context_free(context);
}

static void
start_proofread_task(MProofreadContext *context, const gchar *original_content)
{
    ProofreadTaskData *data;
    GTask *task;

    data = proofread_task_data_new(context, original_content);
    task = g_task_new(NULL, NULL, proofread_task_completed, NULL);
    g_task_set_task_data(task, data, (GDestroyNotify)proofread_task_data_free);

    proofreader_wait_indicator_schedule(context);

    g_task_run_in_thread(task, proofread_task_thread);
    g_object_unref(task);
}

/*
 * m_proofreader_context_new:
 *
 * Create a new proofreading context with all required data.
 */
MProofreadContext *
m_proofreader_context_new(EContentEditor *cnt_editor,
                          const gchar *prompt_id,
                          JsonArray *prompts,
                          const gchar *api_key,
                          const gchar *model,
                          EMsgComposer *composer)
{
    MProofreadContext *context = g_new0(MProofreadContext, 1);

    context->cnt_editor = cnt_editor;
    context->prompt_id = g_strdup(prompt_id);
    context->prompts = json_array_ref(prompts);
    context->api_key = g_strdup(api_key);
    context->model = g_strdup(model);
    context->composer = composer;
    context->wait_dialog = NULL;
    context->wait_timeout_id = 0;

    return context;
}

/*
 * m_proofreader_context_free:
 *
 * Free a proofreading context and all its resources.
 */
void
m_proofreader_context_free(MProofreadContext *context)
{
    if (!context)
        return;

    proofreader_wait_indicator_clear(context);

    g_free(context->prompt_id);
    g_free(context->api_key);
    g_free(context->model);

    if (context->prompts)
        json_array_unref(context->prompts);

    g_free(context);
}

/*
 * show_error_alert:
 * @composer: The message composer
 * @error_message: The error message to display
 *
 * Show an error alert in the composer.
 */
static void
show_error_alert(EMsgComposer *composer, const gchar *error_message)
{
    e_alert_submit(E_ALERT_SINK(composer),
                   "ai:error-proofreading",
                   error_message,
                   NULL);
}

/*
 * show_no_response_dialog:
 * @composer: The message composer
 *
 * Show a dialog when no response is received from the proofreading service.
 */
static void
show_no_response_dialog(EMsgComposer *composer)
{
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(composer),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        _("No response received from proofreading service"));

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/*
 * insert_proofread_content:
 * @cnt_editor: The content editor
 * @content: The proofread content to insert
 *
 * Insert the proofread content into the editor.
 */
static void
insert_proofread_content(EContentEditor *cnt_editor, const gchar *content)
{
    e_content_editor_insert_content(
        cnt_editor,
        content,
        E_CONTENT_EDITOR_INSERT_TEXT_PLAIN | E_CONTENT_EDITOR_INSERT_FROM_PLAIN_TEXT);
}

/*
 * m_proofreader_content_ready_cb:
 *
 * Async callback invoked when the editor content has been retrieved.
 */
void
m_proofreader_content_ready_cb(GObject *source_object,
                                GAsyncResult *result,
                                gpointer user_data)
{
    MProofreadContext *context = user_data;
    EContentEditor *cnt_editor = context->cnt_editor;
    EContentEditorContentHash *content_hash = NULL;
    gchar *content = NULL;
    GError *error = NULL;

    g_debug("Getting content finish for prompt: %s", context->prompt_id);

    content_hash = e_content_editor_get_content_finish(cnt_editor, result, &error);
    if (error)
    {
        g_warning("Error getting content: %s", error->message);
        g_error_free(error);
        m_proofreader_context_free(context);
        return;
    }

    if (!content_hash)
    {
        g_warning("No content hash returned");
        m_proofreader_context_free(context);
        return;
    }

    content = e_content_editor_util_steal_content_data(
        content_hash, E_CONTENT_EDITOR_GET_TO_SEND_PLAIN, NULL);

    gboolean handed_off = FALSE;

    if (content)
    {
        start_proofread_task(context, content);
        handed_off = TRUE;
    }

    g_free(content);
    e_content_editor_util_free_content_hash(content_hash);

    if (!handed_off)
        m_proofreader_context_free(context);
}

/*
 * m_proofreader_start:
 *
 * Start the proofreading process by requesting editor content.
 */
void m_proofreader_start(EContentEditor *cnt_editor,
                         const gchar *prompt_id,
                         JsonArray *prompts,
                         const gchar *api_key,
                         const gchar *model,
                         EMsgComposer *composer)
{
    MProofreadContext *context;

    g_return_if_fail(cnt_editor != NULL);
    g_return_if_fail(prompt_id != NULL);
    g_return_if_fail(prompts != NULL);
    g_return_if_fail(api_key != NULL);

    context = m_proofreader_context_new(cnt_editor, prompt_id, prompts, api_key, model, composer);

    g_debug("Starting proofreading for prompt: %s with model: %s", prompt_id, model);

    e_content_editor_get_content(
        cnt_editor,
        E_CONTENT_EDITOR_GET_TO_SEND_PLAIN,
        NULL,
        NULL,
        m_proofreader_content_ready_cb,
        context);
}
