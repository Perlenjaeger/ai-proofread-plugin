/*
 * m-proofreader.c - Proofreading logic for AI Proofread Plugin
 *
 * Implements the proofreading workflow including async content
 * retrieval, API calls, and result insertion.
 */

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <composer/e-msg-composer.h>

#include "m-proofreader.h"
#include "m-chatgpt-api.h"

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
                          EMsgComposer *composer)
{
    MProofreadContext *context = g_new0(MProofreadContext, 1);

    context->cnt_editor = cnt_editor;
    context->prompt_id = g_strdup(prompt_id);
    context->prompts = json_array_ref(prompts);
    context->api_key = g_strdup(api_key);
    context->composer = composer;

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

    g_free(context->prompt_id);
    g_free(context->api_key);

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
 * process_proofread_result:
 * @context: The proofreading context
 * @original_content: The original editor content
 *
 * Call the proofreading API and handle the result.
 * Returns TRUE if content was successfully inserted, FALSE otherwise.
 */
static gboolean
process_proofread_result(MProofreadContext *context, const gchar *original_content)
{
    GError *error = NULL;
    gchar *proofread_text = NULL;

    proofread_text = m_chatgpt_proofread(
        original_content,
        context->prompt_id,
        context->prompts,
        context->api_key,
        &error);

    if (error)
    {
        g_warning("ChatGPT API error: %s", error->message);
        show_error_alert(context->composer, error->message);
        g_error_free(error);
        return FALSE;
    }

    if (!proofread_text)
    {
        show_no_response_dialog(context->composer);
        return FALSE;
    }

    insert_proofread_content(context->cnt_editor, proofread_text);
    g_free(proofread_text);

    return TRUE;
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

    if (content)
    {
        process_proofread_result(context, content);
        g_free(content);
    }

    e_content_editor_util_free_content_hash(content_hash);
    m_proofreader_context_free(context);
}

/*
 * m_proofreader_start:
 *
 * Start the proofreading process by requesting editor content.
 */
void
m_proofreader_start(EContentEditor *cnt_editor,
                    const gchar *prompt_id,
                    JsonArray *prompts,
                    const gchar *api_key,
                    EMsgComposer *composer)
{
    MProofreadContext *context;

    g_return_if_fail(cnt_editor != NULL);
    g_return_if_fail(prompt_id != NULL);
    g_return_if_fail(prompts != NULL);
    g_return_if_fail(api_key != NULL);

    context = m_proofreader_context_new(cnt_editor, prompt_id, prompts, api_key, composer);

    g_debug("Starting proofreading for prompt: %s", prompt_id);

    e_content_editor_get_content(
        cnt_editor,
        E_CONTENT_EDITOR_GET_TO_SEND_PLAIN,
        NULL,
        NULL,
        m_proofreader_content_ready_cb,
        context);
}
