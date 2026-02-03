/*
 * m-ui-actions.c - UI Actions for AI Proofread Plugin
 *
 * Implements UI construction and action callbacks.
 */

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>

#include <composer/e-msg-composer.h>
#include <evolution/e-util/e-util.h>

#include "m-ui-actions.h"
#include "m-proofreader.h"
#include "m-config.h"

/* Forward declarations for E/GTK UI helpers */
typedef struct _EUIManager EUIManager;
EUIManager *e_html_editor_get_ui_manager(EHTMLEditor *editor);

/* Static action context - set during registration */
static MUIActionContext *g_action_context = NULL;

/*
 * m_ui_action_context_new:
 */
MUIActionContext *
m_ui_action_context_new(JsonArray *prompts,
                        const gchar *api_key,
                        const gchar *model,
                        GList *models)
{
    MUIActionContext *context = g_new0(MUIActionContext, 1);

    context->prompts = json_array_ref(prompts);
    context->api_key = g_strdup(api_key);
    context->model = g_strdup(model ? model : M_CONFIG_DEFAULT_MODEL);

    /* Copy models list */
    context->models = NULL;
    for (GList *l = models; l != NULL; l = l->next)
    {
        context->models = g_list_append(context->models, g_strdup(l->data));
    }

    return context;
}

/*
 * m_ui_action_context_set_model:
 */
void m_ui_action_context_set_model(MUIActionContext *context, const gchar *model)
{
    g_return_if_fail(context != NULL);
    g_return_if_fail(model != NULL);

    g_free(context->model);
    context->model = g_strdup(model);

    /* Save to config */
    m_config_save_model(model);
}

/*
 * m_ui_action_context_free:
 */
void
m_ui_action_context_free(MUIActionContext *context)
{
    if (!context)
        return;

    if (context->prompts)
        json_array_unref(context->prompts);

    g_free(context->api_key);
    g_free(context->model);
    g_list_free_full(context->models, g_free);
    g_free(context);
}

/*
 * m_ui_action_entries_free:
 */
void
m_ui_action_entries_free(MUIActionEntries *action_entries)
{
    if (!action_entries)
        return;

    if (action_entries->entries)
    {
        for (guint i = 0; i < action_entries->total_count; i++)
        {
            g_free((gpointer)action_entries->entries[i].name);
            g_free((gpointer)action_entries->entries[i].label);
            g_free((gpointer)action_entries->entries[i].tooltip);
        }
        g_free(action_entries->entries);
    }

    g_free(action_entries->eui_xml);
    g_free(action_entries);
}

/*
 * get_composer_from_user_data:
 * @user_data: The user data passed to the action callback (composer)
 * @out_composer: (out): The message composer
 * @out_cnt_editor: (out): The content editor
 *
 * Helper to get composer components from user data.
 * Returns FALSE if any component is unavailable.
 */
static gboolean
get_composer_from_user_data(gpointer user_data,
                            EMsgComposer **out_composer,
                            EContentEditor **out_cnt_editor)
{
    EHTMLEditor *editor;

    if (!user_data || !E_IS_MSG_COMPOSER(user_data))
        return FALSE;

    *out_composer = E_MSG_COMPOSER(user_data);
    editor = e_msg_composer_get_editor(*out_composer);
    *out_cnt_editor = e_html_editor_get_content_editor(editor);

    return TRUE;
}

/*
 * action_proofread_cb:
 *
 * EUI action callback for individual prompt actions.
 */
static void
action_proofread_cb(EUIAction *action,
                    GVariant *parameter,
                    gpointer user_data)
{
    gchar *action_name = NULL;
    EMsgComposer *composer;
    EContentEditor *cnt_editor;
    MUIActionContext *ctx = g_action_context;

    if (!ctx)
    {
        g_warning("No action context available");
        return;
    }

    g_object_get(action, "name", &action_name, NULL);
    if (!action_name)
        return;

    if (!get_composer_from_user_data(user_data, &composer, &cnt_editor))
    {
        g_free(action_name);
        return;
    }

    g_debug("Proofread action triggered: %s", action_name);

    m_proofreader_start(cnt_editor, action_name, ctx->prompts, ctx->api_key, ctx->model, composer);

    g_free(action_name);
}

/*
 * menu_item_activate_cb:
 *
 * GTK menu item activate handler for the dropdown menu.
 */
static void
menu_item_activate_cb(GtkMenuItem *item, gpointer user_data)
{
    gchar *prompt_id = user_data;
    EMsgComposer *composer;
    EHTMLEditor *editor;
    EContentEditor *cnt_editor;
    MUIActionContext *ctx = g_action_context;

    if (!prompt_id || !ctx)
        return;

    composer = g_object_get_data(G_OBJECT(item), "composer");
    if (!composer)
        return;

    editor = e_msg_composer_get_editor(composer);
    cnt_editor = e_html_editor_get_content_editor(editor);

    m_proofreader_start(cnt_editor, prompt_id, ctx->prompts, ctx->api_key, ctx->model, composer);

    g_free(prompt_id);
}

/*
 * action_dropdown_cb:
 *
 * EUI action callback for the dropdown toolbar button.
 */
static void
action_dropdown_cb(EUIAction *action,
                   GVariant *parameter,
                   gpointer user_data)
{
    EMsgComposer *composer;
    EContentEditor *cnt_editor;
    GtkWidget *menu;
    MUIActionContext *ctx = g_action_context;
    guint i, n_prompts;

    if (!ctx)
    {
        g_warning("No action context available");
        return;
    }

    if (!get_composer_from_user_data(user_data, &composer, &cnt_editor))
        return;

    menu = gtk_menu_new();
    n_prompts = json_array_get_length(ctx->prompts);

    for (i = 0; i < n_prompts; i++)
    {
        JsonObject *prompt = json_array_get_object_element(ctx->prompts, i);
        const gchar *name = json_object_get_string_member(prompt, "name");
        gchar *action_name = g_strdup_printf("ai-proofread-%s", name);

        GtkWidget *mi = gtk_menu_item_new_with_label(name);
        g_object_set_data(G_OBJECT(mi), "composer", composer);
        g_signal_connect(mi, "activate", G_CALLBACK(menu_item_activate_cb), g_strdup(action_name));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
        gtk_widget_show(mi);

        g_free(action_name);
    }

    gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);
}

/*
 * action_select_model_cb:
 *
 * EUI action callback for model selection menu items.
 */
static void
action_select_model_cb(EUIAction *action,
                       GVariant *parameter,
                       gpointer user_data)
{
    gchar *action_name = NULL;
    MUIActionContext *ctx = g_action_context;
    const gchar *model_id;

    if (!ctx)
    {
        g_warning("No action context available");
        return;
    }

    g_object_get(action, "name", &action_name, NULL);
    if (!action_name)
        return;

    /* Extract model ID from action name (ai-model-<model_id>) */
    if (g_str_has_prefix(action_name, "ai-model-"))
    {
        model_id = action_name + strlen("ai-model-");
        g_debug("Model selected: %s", model_id);
        m_ui_action_context_set_model(ctx, model_id);
    }

    g_free(action_name);
}

/*
 * build_eui_xml:
 * @prompts: Array of prompts
 * @models: List of available models
 * @current_model: Currently selected model
 *
 * Build the EUI XML string for menu and toolbar items.
 */
static gchar *
build_eui_xml(JsonArray *prompts, GList *models, const gchar *current_model)
{
    GString *xml;
    guint i, n_prompts;

    xml = g_string_new(
        "<eui>"
        "<menu id='main-menu'>"
        "<placeholder id='custom-menus'>"
        "<submenu action='ai-menu'>"
        "<placeholder id='ai-menu-holder'>");

    n_prompts = json_array_get_length(prompts);
    for (i = 0; i < n_prompts; i++)
    {
        JsonObject *prompt = json_array_get_object_element(prompts, i);
        const gchar *name = json_object_get_string_member(prompt, "name");
        gchar *action_name = g_strdup_printf("ai-proofread-%s", name);

        g_string_append_printf(xml, "<item action='%s'/>", action_name);
        g_free(action_name);
    }

    /* Add separator and Model submenu */
    g_string_append(xml, "<separator/>");
    g_string_append(xml, "<submenu action='ai-model-menu'>");

    for (GList *l = models; l != NULL; l = l->next)
    {
        const gchar *model_id = l->data;
        gchar *action_name = g_strdup_printf("ai-model-%s", model_id);
        g_string_append_printf(xml, "<item action='%s'/>", action_name);
        g_free(action_name);
    }

    g_string_append(xml, "</submenu>");

    g_string_append(xml,
                    "</placeholder>"
                    "</submenu>"
                    "</placeholder>"
                    "</menu>");

    /* Toolbar buttons */
    g_string_append(xml, "<toolbar id='main-toolbar-with-headerbar'><item action='ai-proofread-dropdown'/></toolbar>");
    g_string_append(xml, "<toolbar id='main-toolbar-without-headerbar'><item action='ai-proofread-dropdown'/></toolbar>");
    g_string_append(xml, "</eui>");

    return g_string_free(xml, FALSE);
}

/*
 * create_prompt_entry:
 * @prompt: The prompt JSON object
 *
 * Create an EUI action entry for a single prompt.
 */
static EUIActionEntry
create_prompt_entry(JsonObject *prompt)
{
    const gchar *name = json_object_get_string_member(prompt, "name");
    const gchar *prompt_text = json_object_get_string_member(prompt, "prompt");

    return (EUIActionEntry){
        g_strdup_printf("ai-proofread-%s", name),
        "tools-check-spelling",
        g_strdup(name),
        NULL,
        g_strdup(prompt_text),
        action_proofread_cb,
        NULL,
        NULL,
        NULL};
}

/*
 * create_menu_entry:
 *
 * Create the parent AI menu entry.
 */
static EUIActionEntry
create_menu_entry(void)
{
    return (EUIActionEntry){
        g_strdup("ai-menu"),
        NULL,
        g_strdup(N_("AI")),
        NULL,
        g_strdup(N_("AI tools")),
        NULL,
        NULL,
        NULL,
        NULL};
}

/*
 * create_dropdown_entry:
 *
 * Create the dropdown toolbar button entry.
 */
static EUIActionEntry
create_dropdown_entry(void)
{
    return (EUIActionEntry){
        g_strdup("ai-proofread-dropdown"),
        "tools-check-spelling",
        g_strdup(N_("AI _Proofread")),
        NULL,
        g_strdup(N_("AI Proofread")),
        action_dropdown_cb,
        NULL,
        NULL,
        NULL};
}

/*
 * create_model_menu_entry:
 *
 * Create the Model submenu entry.
 */
static EUIActionEntry
create_model_menu_entry(const gchar *current_model)
{
    gchar *label = g_strdup_printf(N_("Model (%s)"), current_model ? current_model : M_CONFIG_DEFAULT_MODEL);
    return (EUIActionEntry){
        g_strdup("ai-model-menu"),
        NULL,
        label,
        NULL,
        g_strdup(N_("Select AI model")),
        NULL,
        NULL,
        NULL,
        NULL};
}

/*
 * create_model_entry:
 * @model_id: The model identifier
 * @is_current: Whether this is the currently selected model
 *
 * Create an EUI action entry for a model selection item.
 */
static EUIActionEntry
create_model_entry(const gchar *model_id, gboolean is_current)
{
    gchar *label;

    if (is_current)
        label = g_strdup_printf("✓ %s", model_id);
    else
        label = g_strdup(model_id);

    return (EUIActionEntry){
        g_strdup_printf("ai-model-%s", model_id),
        NULL,
        label,
        NULL,
        g_strdup_printf(N_("Use %s model"), model_id),
        action_select_model_cb,
        NULL,
        NULL,
        NULL};
}

/*
 * validate_entries:
 * @entries: The entries array
 * @count: Number of entries
 *
 * Ensure no NULL action names to avoid assertion failures.
 */
static void
validate_entries(EUIActionEntry *entries, guint count)
{
    for (guint i = 0; i < count; i++)
    {
        if (!entries[i].name)
        {
            entries[i].name = g_strdup_printf("ai-proofread-missing-%u", i);
            g_warning("Found null action name for entry %u, using fallback '%s'", i, entries[i].name);
        }
        if (!entries[i].label)
            entries[i].label = g_strdup("(no label)");
        if (!entries[i].tooltip)
            entries[i].tooltip = g_strdup("");
    }
}

/*
 * m_ui_build_action_entries:
 */
MUIActionEntries *
m_ui_build_action_entries(JsonArray *prompts, MUIActionContext *action_context)
{
    MUIActionEntries *result;
    guint n_prompts;
    guint n_models;
    guint idx;

    if (!prompts)
        return NULL;

    n_prompts = json_array_get_length(prompts);
    if (n_prompts == 0)
        return NULL;

    n_models = g_list_length(action_context->models);

    result = g_new0(MUIActionEntries, 1);
    result->count = n_prompts;
    /* prompts + ai-menu + dropdown + model-menu + model entries */
    result->total_count = n_prompts + 3 + n_models;
    result->entries = g_new0(EUIActionEntry, result->total_count);

    idx = 0;

    /* Create prompt entries */
    for (guint i = 0; i < n_prompts; i++)
    {
        JsonObject *prompt = json_array_get_object_element(prompts, i);
        result->entries[idx++] = create_prompt_entry(prompt);
    }

    /* Add menu and dropdown entries */
    result->entries[idx++] = create_menu_entry();
    result->entries[idx++] = create_dropdown_entry();

    /* Add model menu entry */
    result->entries[idx++] = create_model_menu_entry(action_context->model);

    /* Add model selection entries */
    for (GList *l = action_context->models; l != NULL; l = l->next)
    {
        const gchar *model_id = l->data;
        gboolean is_current = g_strcmp0(model_id, action_context->model) == 0;
        result->entries[idx++] = create_model_entry(model_id, is_current);
    }

    /* Validate all entries */
    validate_entries(result->entries, result->total_count);

    /* Build EUI XML */
    result->eui_xml = build_eui_xml(prompts, action_context->models, action_context->model);

    return result;
}

/*
 * m_ui_register_actions:
 */
void
m_ui_register_actions(EMsgComposer *composer,
                      MUIActionEntries *action_entries,
                      MUIActionContext *action_context)
{
    EHTMLEditor *html_editor;
    EUIManager *ui_manager;

    g_return_if_fail(E_IS_MSG_COMPOSER(composer));
    g_return_if_fail(action_entries != NULL);
    g_return_if_fail(action_context != NULL);

    /* Store action context for callbacks */
    g_action_context = action_context;

    html_editor = e_msg_composer_get_editor(composer);
    ui_manager = e_html_editor_get_ui_manager(html_editor);

    e_ui_manager_add_actions_with_eui_data(
        ui_manager, "core", GETTEXT_PACKAGE,
        action_entries->entries, action_entries->total_count,
        composer, action_entries->eui_xml);
}
