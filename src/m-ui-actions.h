/*
 * m-ui-actions.h - UI Actions for AI Proofread Plugin
 *
 * This module handles UI construction:
 * - Building EUI action entries from prompts
 * - Creating EUI XML definitions
 * - Action callbacks for menu and toolbar items
 */

#ifndef M_UI_ACTIONS_H
#define M_UI_ACTIONS_H

#include <glib.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <composer/e-msg-composer.h>
#include <evolution/e-util/e-util.h>

G_BEGIN_DECLS

/**
 * MUIActionContext:
 * @prompts: Array of available prompts
 * @api_key: The API key for the proofreading service
 * @model: The currently selected AI model
 * @models: List of available models (GList of gchar*)
 *
 * Context for UI action callbacks.
 */
typedef struct _MUIActionContext MUIActionContext;

struct _MUIActionContext
{
    JsonArray *prompts;
    gchar *api_key;
    gchar *model;
    GList *models;
};

/**
 * m_ui_action_context_new:
 * @prompts: The prompts array (will be referenced)
 * @api_key: The API key (will be copied)
 * @model: The selected model (will be copied)
 * @models: List of available models (will be copied)
 *
 * Create a new UI action context.
 *
 * Returns: (transfer full): A newly allocated context
 */
MUIActionContext *m_ui_action_context_new(JsonArray *prompts,
                                          const gchar *api_key,
                                          const gchar *model,
                                          GList *models);

/**
 * m_ui_action_context_set_model:
 * @context: The action context
 * @model: The new model to set
 *
 * Update the selected model in the context and save it to config.
 */
void m_ui_action_context_set_model(MUIActionContext *context, const gchar *model);

/**
 * m_ui_action_context_free:
 * @context: The context to free
 *
 * Free a UI action context and all its resources.
 */
void m_ui_action_context_free(MUIActionContext *context);

/**
 * MUIActionEntries:
 * @entries: Array of EUI action entries
 * @count: Number of entries (prompts only, not counting ai-menu and dropdown)
 * @total_count: Total entries including ai-menu and dropdown
 * @eui_xml: The EUI XML string
 *
 * Structure holding the built action entries and XML.
 */
typedef struct _MUIActionEntries MUIActionEntries;

struct _MUIActionEntries
{
    EUIActionEntry *entries;
    guint count;       /* Number of prompt entries */
    guint total_count; /* Total entries including menu and dropdown */
    gchar *eui_xml;
};

/**
 * m_ui_action_entries_free:
 * @action_entries: The entries to free
 *
 * Free the action entries structure and all its resources.
 */
void m_ui_action_entries_free(MUIActionEntries *action_entries);

/**
 * m_ui_build_action_entries:
 * @prompts: Array of prompt configurations
 * @action_context: Context for callbacks (will be stored, caller retains ownership)
 *
 * Build EUI action entries and XML from the prompt configurations.
 *
 * Returns: (transfer full) (nullable): The built entries, or NULL if no prompts
 */
MUIActionEntries *m_ui_build_action_entries(JsonArray *prompts, MUIActionContext *action_context);

/**
 * m_ui_register_actions:
 * @composer: The message composer
 * @action_entries: The built action entries
 * @action_context: The UI action context for callbacks
 *
 * Register the actions with the composer's UI manager.
 */
void m_ui_register_actions(EMsgComposer *composer,
                           MUIActionEntries *action_entries,
                           MUIActionContext *action_context);

G_END_DECLS

#endif /* M_UI_ACTIONS_H */
