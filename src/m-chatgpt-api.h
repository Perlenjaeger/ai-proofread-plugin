#ifndef M_CHATGPT_API_H
#define M_CHATGPT_API_H

#include <json-glib/json-glib.h>

/**
 * m_chatgpt_proofread:
 * @content: The text content to proofread
 * @prompt_id: The prompt identifier
 * @prompts: Array of prompt configurations
 * @api_key: The OpenAI API key
 * @model: The model to use (e.g., "gpt-4o")
 * @error: Return location for error
 *
 * Send content to ChatGPT for proofreading.
 *
 * Returns: (transfer full) (nullable): The proofread text, or NULL on error
 */
gchar *m_chatgpt_proofread(const gchar *content,
                           const gchar *prompt_id,
                           JsonArray *prompts,
                           const gchar *api_key,
                           const gchar *model,
                           GError **error);

/**
 * m_chatgpt_fetch_models:
 * @api_key: The OpenAI API key
 * @error: Return location for error
 *
 * Fetch the list of available models from the OpenAI API.
 * Only returns models suitable for chat completions (gpt-* models).
 *
 * Returns: (transfer full) (nullable): A list of model IDs (GList of gchar*),
 *          or NULL on error. Free with g_list_free_full(list, g_free).
 */
GList *m_chatgpt_fetch_models(const gchar *api_key, GError **error);

#endif /* M_CHATGPT_API_H */ 