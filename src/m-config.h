/*
 * m-config.h - Configuration management for AI Proofread Plugin
 *
 * This module handles loading and managing configuration:
 * - Loading prompts from JSON config file
 * - Loading API key from ~/.authinfo
 * - Managing selected AI model
 */

#ifndef M_CONFIG_H
#define M_CONFIG_H

#include <json-glib/json-glib.h>

G_BEGIN_DECLS

/**
 * M_CONFIG_DEFAULT_MODEL:
 *
 * Default model to use when none is configured.
 */
#define M_CONFIG_DEFAULT_MODEL "gpt-4o"

/**
 * m_config_load_prompts:
 *
 * Read the prompts configuration file from the user's config directory
 * ($XDG_CONFIG_HOME/ai-proofread/prompts.json) and return a referenced
 * JsonArray containing the prompt objects.
 *
 * On error or when the root is not an array, an empty JsonArray is
 * returned. The caller owns a reference to the returned array.
 *
 * Returns: (transfer full): A JsonArray with prompt configurations
 */
JsonArray *m_config_load_prompts(void);

/**
 * m_config_load_api_key:
 *
 * Read ~/.authinfo and try to find a line matching the expected format
 * for the OpenAI API key:
 * "machine api.openai.com login apikey password <key>"
 *
 * Returns: (transfer full) (nullable): A newly allocated string with
 *          the API key, or NULL if none is found. The caller must free
 *          the returned string.
 */
gchar *m_config_load_api_key(void);

/**
 * m_config_load_model:
 *
 * Load the currently selected AI model from the configuration file.
 *
 * Returns: (transfer full): A newly allocated string with the model ID.
 *          If no model is configured, returns the default model (gpt-4o).
 *          The caller must free the returned string.
 */
gchar *m_config_load_model(void);

/**
 * m_config_save_model:
 * @model: The model ID to save
 *
 * Save the selected AI model to the configuration file.
 *
 * Returns: TRUE on success, FALSE on error
 */
gboolean m_config_save_model(const gchar *model);

G_END_DECLS

#endif /* M_CONFIG_H */
