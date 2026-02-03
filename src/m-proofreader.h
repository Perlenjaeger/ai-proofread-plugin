/*
 * m-proofreader.h - Proofreading logic for AI Proofread Plugin
 *
 * This module handles the proofreading workflow:
 * - Context management for async operations
 * - Content retrieval callbacks
 * - Error handling and user feedback
 */

#ifndef M_PROOFREADER_H
#define M_PROOFREADER_H

#include <glib.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <composer/e-msg-composer.h>

G_BEGIN_DECLS

/**
 * MProofreadContext:
 * @cnt_editor: The content editor to proofread
 * @prompt_id: The prompt identifier to use
 * @prompts: Array of available prompts
 * @api_key: The API key for the proofreading service
 * @composer: The message composer (for error alerts)
 *
 * Context structure passed through async proofreading operations.
 */
typedef struct _MProofreadContext MProofreadContext;

struct _MProofreadContext
{
    EContentEditor *cnt_editor;
    gchar *prompt_id;
    JsonArray *prompts;
    gchar *api_key;
    EMsgComposer *composer;
};

/**
 * m_proofreader_context_new:
 * @cnt_editor: The content editor
 * @prompt_id: The prompt identifier (will be copied)
 * @prompts: The prompts array (will be referenced)
 * @api_key: The API key (will be copied)
 * @composer: The message composer
 *
 * Create a new proofreading context.
 *
 * Returns: (transfer full): A newly allocated context
 */
MProofreadContext *m_proofreader_context_new(EContentEditor *cnt_editor,
                                              const gchar *prompt_id,
                                              JsonArray *prompts,
                                              const gchar *api_key,
                                              EMsgComposer *composer);

/**
 * m_proofreader_context_free:
 * @context: The context to free
 *
 * Free a proofreading context and all its resources.
 */
void m_proofreader_context_free(MProofreadContext *context);

/**
 * m_proofreader_content_ready_cb:
 * @source_object: The source object for the async call
 * @result: The async result
 * @user_data: The MProofreadContext
 *
 * Async callback invoked when editor content has been retrieved.
 * Calls the proofreading API and inserts the result into the editor.
 */
void m_proofreader_content_ready_cb(GObject *source_object,
                                     GAsyncResult *result,
                                     gpointer user_data);

/**
 * m_proofreader_start:
 * @cnt_editor: The content editor
 * @prompt_id: The prompt identifier
 * @prompts: The prompts array
 * @api_key: The API key
 * @composer: The message composer
 *
 * Start the proofreading process by requesting editor content.
 * The content will be processed asynchronously.
 */
void m_proofreader_start(EContentEditor *cnt_editor,
                         const gchar *prompt_id,
                         JsonArray *prompts,
                         const gchar *api_key,
                         EMsgComposer *composer);

G_END_DECLS

#endif /* M_PROOFREADER_H */
