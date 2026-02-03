/*
 * m-msg-composer-extension.c - Message composer extension for AI Proofread Plugin
 *
 * This is the main extension module that integrates AI proofreading into
 * Evolution's message composer. It orchestrates the configuration loading,
 * UI creation, and extension lifecycle.
 *
 * The implementation is split into logical modules:
 * - m-config: Configuration loading (prompts, API key)
 * - m-proofreader: Proofreading workflow and callbacks
 * - m-ui-actions: UI action entries and menu/toolbar construction
 * - m-chatgpt-api: ChatGPT API communication
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <composer/e-msg-composer.h>
#include <evolution/e-util/e-util.h>

#include "m-msg-composer-extension.h"
#include "m-config.h"
#include "m-ui-actions.h"
#include "m-chatgpt-api.h"

struct _MMsgComposerExtensionPrivate
{
    JsonArray *prompts;           /* Array of prompts loaded from config */
    gchar *chatgpt_api_key;       /* OpenAI API key */
    gchar *model;                 /* Selected AI model */
    GList *models;                /* List of available models */
    MUIActionContext *ui_context; /* UI action context */
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED(MMsgComposerExtension, m_msg_composer_extension, E_TYPE_EXTENSION, 0,
                               G_ADD_PRIVATE_DYNAMIC(MMsgComposerExtension))

/*
 * validate_configuration:
 * @extension: The message composer extension
 *
 * Check if the extension has valid configuration.
 * Returns TRUE if prompts and API key are available.
 */
static gboolean
validate_configuration(MMsgComposerExtension *extension)
{
    if (!extension->priv->prompts ||
        json_array_get_length(extension->priv->prompts) == 0)
    {
        g_warning("No prompts configured, skipping UI creation");
        return FALSE;
    }

    if (!extension->priv->chatgpt_api_key)
    {
        g_warning("No API key configured, skipping UI creation");
        return FALSE;
    }

    return TRUE;
}

/*
 * m_msg_composer_extension_add_ui:
 * @extension: The message composer extension
 * @composer: The message composer to add UI elements to
 *
 * Add UI elements (menu actions and toolbar button) for the configured
 * prompts to the message composer.
 */
static void
m_msg_composer_extension_add_ui(MMsgComposerExtension *extension,
                                EMsgComposer *composer)
{
    MUIActionEntries *action_entries;

    g_return_if_fail(M_IS_MSG_COMPOSER_EXTENSION(extension));
    g_return_if_fail(E_IS_MSG_COMPOSER(composer));

    if (!validate_configuration(extension))
        return;

    /* Create UI action context */
    extension->priv->ui_context = m_ui_action_context_new(
        extension->priv->prompts,
        extension->priv->chatgpt_api_key,
        extension->priv->model,
        extension->priv->models);

    /* Build action entries and EUI XML */
    action_entries = m_ui_build_action_entries(
        extension->priv->prompts,
        extension->priv->ui_context);

    if (!action_entries)
    {
        g_warning("No action entries built, skipping UI registration");
        return;
    }

    /* Register actions with the UI manager */
    m_ui_register_actions(composer, action_entries, extension->priv->ui_context);

    /* Free the action entries (the UI manager has copied what it needs) */
    m_ui_action_entries_free(action_entries);
}

static void
m_msg_composer_extension_constructed(GObject *object)
{
    EExtension *extension;
    EExtensible *extensible;

    /* Chain up to parent's method */
    G_OBJECT_CLASS(m_msg_composer_extension_parent_class)->constructed(object);

    extension = E_EXTENSION(object);
    extensible = e_extension_get_extensible(extension);

    m_msg_composer_extension_add_ui(
        M_MSG_COMPOSER_EXTENSION(object),
        E_MSG_COMPOSER(extensible));
}

static void
m_msg_composer_extension_dispose(GObject *object)
{
    MMsgComposerExtension *extension = M_MSG_COMPOSER_EXTENSION(object);

    if (extension->priv->prompts)
    {
        json_array_unref(extension->priv->prompts);
        extension->priv->prompts = NULL;
    }

    g_clear_pointer(&extension->priv->chatgpt_api_key, g_free);
    g_clear_pointer(&extension->priv->model, g_free);

    if (extension->priv->models)
    {
        g_list_free_full(extension->priv->models, g_free);
        extension->priv->models = NULL;
    }

    if (extension->priv->ui_context)
    {
        m_ui_action_context_free(extension->priv->ui_context);
        extension->priv->ui_context = NULL;
    }

    /* Chain up to parent's method */
    G_OBJECT_CLASS(m_msg_composer_extension_parent_class)->dispose(object);
}

static void
m_msg_composer_extension_class_init(MMsgComposerExtensionClass *class)
{
    GObjectClass *object_class;
    EExtensionClass *extension_class;

    object_class = G_OBJECT_CLASS(class);
    object_class->constructed = m_msg_composer_extension_constructed;
    object_class->dispose = m_msg_composer_extension_dispose;

    /* Set the type to extend */
    extension_class = E_EXTENSION_CLASS(class);
    extension_class->extensible_type = E_TYPE_MSG_COMPOSER;
}

static void
m_msg_composer_extension_class_finalize(MMsgComposerExtensionClass *class)
{
    /* Nothing to finalize */
}

static void
m_msg_composer_extension_init(MMsgComposerExtension *extension)
{
    extension->priv = m_msg_composer_extension_get_instance_private(extension);

    /* Load configuration using the config module */
    extension->priv->prompts = m_config_load_prompts();
    extension->priv->chatgpt_api_key = m_config_load_api_key();
    extension->priv->model = m_config_load_model();
    extension->priv->models = NULL;
    extension->priv->ui_context = NULL;

    /* Fetch available models from API */
    if (extension->priv->chatgpt_api_key)
    {
        GError *error = NULL;
        extension->priv->models = m_chatgpt_fetch_models(
            extension->priv->chatgpt_api_key,
            &error);

        if (error)
        {
            g_warning("Failed to fetch models: %s", error->message);
            g_error_free(error);
        }
    }
}

void
m_msg_composer_extension_type_register(GTypeModule *type_module)
{
    m_msg_composer_extension_register_type(type_module);
}
