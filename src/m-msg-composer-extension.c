#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>

#include <composer/e-msg-composer.h>
#include <evolution/e-util/e-util.h>

#include "m-msg-composer-extension.h"
#include "m-chatgpt-api.h"

/*
 * ai-proofread-plugin - Message composer extension
 *
 * This file implements the UI integration for invoking an external
 * proofreading assistant (ChatGPT) from the message composer. The code
 * loads prompts from a JSON config and an API key from the user's
 * ~/.authinfo, then adds actions and a toolbar button to the composer
 * UI so the user can invoke proofreading on the current editor content.
 *
 * The major responsibilities here are:
 * - load prompts and API key during initialization
 * - create EUI actions and toolbar/menu items for each prompt
 * - fetch editor content asynchronously and call the proofreading API
 * - insert the returned proofread text into the editor
 *
 */

/* Forward declarations for E/GTK UI helpers not included by other headers here */
typedef struct _EUIManager EUIManager;
EUIManager *e_html_editor_get_ui_manager(EHTMLEditor *editor);
GtkActionGroup *e_html_editor_get_action_group(EHTMLEditor *editor, const gchar *name);
GtkWidget *e_ui_manager_get_widget(EUIManager *manager, const gchar *path);

struct _MMsgComposerExtensionPrivate
{
    JsonArray *prompts;     // Array of prompts loaded from config
    gchar *chatgpt_api_key; // OpenAI API key
};

struct ProofreadContext
{
    EContentEditor *cnt_editor;
    gchar *prompt_id;
    MMsgComposerExtension *extension;
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED(MMsgComposerExtension, m_msg_composer_extension, E_TYPE_EXTENSION, 0,
                               G_ADD_PRIVATE_DYNAMIC(MMsgComposerExtension))

/*
 * load_prompts:
 * Read the prompts configuration file from the user's config directory
 * (``$XDG_CONFIG_HOME/ai-proofread/prompts.json`` or equivalent) and
 * return a referenced `JsonArray` containing the prompt objects.
 *
 * On error or when the root is not an array an empty `JsonArray` is
 * returned. The caller owns a reference to the returned array.
 */
static JsonArray *
load_prompts(void)
{
    const gchar *config_dir = e_get_user_config_dir();
    gchar *config_path = g_build_filename(config_dir, "ai-proofread", "prompts.json", NULL);
    JsonParser *parser;
    JsonNode *root;
    JsonArray *prompts = NULL;
    GError *error = NULL;

    g_debug("Loading prompts from: %s", config_path);

    parser = json_parser_new();
    if (json_parser_load_from_file(parser, config_path, &error))
    {
        root = json_parser_get_root(parser);
        if (JSON_NODE_HOLDS_ARRAY(root))
        {
            prompts = json_array_ref(json_node_get_array(root));
            g_debug("Prompts loaded: %d", json_array_get_length(prompts));
        }
        else
        {
            /* If root exists but is not an array, treat as empty prompts */
            g_warning("Prompts file root is not an array, using empty prompts list");
            prompts = json_array_new();
        }
    }
    else
    {
        g_warning("Error loading prompts: %s", error->message);
        g_error_free(error);
        prompts = json_array_new();
    }

    g_object_unref(parser);
    g_free(config_path);

    return prompts;
}

/*
 * load_api_key:
 * Read `~/.authinfo` and try to find a line matching the expected
 * format for the OpenAI API key: ``machine api.openai.com login apikey password <key>``.
 *
 * Returns a newly allocated string with the API key or NULL if none is
 * found. The caller must free the returned string.
 */
static gchar *
load_api_key(void)
{
    const gchar *home_dir = g_get_home_dir();
    gchar *authinfo_path = g_build_filename(home_dir, ".authinfo", NULL);
    gchar *content = NULL;
    gchar *api_key = NULL;
    GError *error = NULL;

    g_debug("Loading authinfo from: %s", authinfo_path);

    if (g_file_get_contents(authinfo_path, &content, NULL, &error))
    {
        gchar **lines = g_strsplit(content, "\n", -1);
        for (gint i = 0; lines[i] != NULL; i++)
        {
            // Skip empty lines
            if (lines[i][0] == '\0')
                continue;

            gchar **tokens = g_strsplit(lines[i], " ", -1);
            gint token_count = g_strv_length(tokens);

            // Look for line matching: machine api.openai.com login apikey password <key>
            if (token_count >= 6 &&
                g_strcmp0(tokens[0], "machine") == 0 &&
                g_strcmp0(tokens[1], "api.openai.com") == 0 &&
                g_strcmp0(tokens[2], "login") == 0 &&
                g_strcmp0(tokens[3], "apikey") == 0 &&
                g_strcmp0(tokens[4], "password") == 0)
            {
                api_key = g_strdup(tokens[5]);
                g_debug("Found API key");
                g_strfreev(tokens);
                break;
            }
            g_strfreev(tokens);
        }
        g_strfreev(lines);
        g_free(content);
    }
    else
    {
        g_warning("Error loading authinfo: %s", error->message);
        g_error_free(error);
    }

    g_free(authinfo_path);

    return api_key;
}

/*
 * msg_text_cb:
 * Asynchronous callback invoked when the editor content has been
 * retrieved. This function calls the proofreading API with the content
 * and prompt id, inserts the returned text into the editor and performs
 * proper cleanup of temporary resources.
 */
static void
msg_text_cb(GObject *source_object,
            GAsyncResult *result,
            gpointer user_data)
{
    struct ProofreadContext *context = user_data;
    EContentEditor *cnt_editor = context->cnt_editor;
    const gchar *prompt_id = context->prompt_id;
    MMsgComposerExtension *extension = context->extension;

    EContentEditorContentHash *content_hash;
    gchar *content, *new_content;
    GError *error = NULL;

    g_debug("Getting content finish for prompt: %s", prompt_id);
    content_hash = e_content_editor_get_content_finish(cnt_editor, result, &error);
    if (error)
    {
        g_warning("Error getting content: %s", error->message);
        g_error_free(error);
        g_free(context);
        return;
    }

    if (!content_hash)
    {
        g_warning("No content hash returned");
        g_free(context);
        return;
    }

    content = e_content_editor_util_steal_content_data(content_hash,
                                                       E_CONTENT_EDITOR_GET_TO_SEND_PLAIN, NULL);

    if (content)
    {
        gchar *proofread_text = m_chatgpt_proofread(
            content,
            prompt_id,
            extension->priv->prompts,
            extension->priv->chatgpt_api_key,
            &error);

        if (error)
        {
            g_warning("ChatGPT API error: %s", error->message);

            EMsgComposer *composer = E_MSG_COMPOSER(
                e_extension_get_extensible(E_EXTENSION(extension)));

            e_alert_submit(
                E_ALERT_SINK(composer),
                "ai:error-proofreading",
                error->message,
                NULL);

            /* Free resources before returning on error */
            g_error_free(error);
            g_free(content);
            e_content_editor_util_free_content_hash(content_hash);
            g_free(context->prompt_id);
            g_free(context);
            return;
        }
        else if (proofread_text)
        {
            new_content = g_strdup(proofread_text);
            g_free(proofread_text);
        }
        else
        {
            // Show dialog for no response case too
            EMsgComposer *composer = E_MSG_COMPOSER(
                e_extension_get_extensible(E_EXTENSION(extension)));

            GtkWidget *dialog = gtk_message_dialog_new(
                GTK_WINDOW(composer),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_OK,
                _("No response received from proofreading service"));

            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);

            /* Free resources on no-response path */
            g_free(content);
            e_content_editor_util_free_content_hash(content_hash);
            g_free(context->prompt_id);
            g_free(context);
            return;
        }

        e_content_editor_insert_content(
            cnt_editor,
            new_content,
            E_CONTENT_EDITOR_INSERT_TEXT_PLAIN | E_CONTENT_EDITOR_INSERT_FROM_PLAIN_TEXT);

        g_free(new_content); // Free new_content after insertion
        g_free(content);
    }

    /* Always free the content hash and context allocated by the caller */
    e_content_editor_util_free_content_hash(content_hash);
    g_free(context->prompt_id);
    g_free(context);
}

/* removed GtkAction-based prompt callback — using EUI-based callbacks instead */

/*
 * action_msg_composer_eui_cb:
 * EUI action callback invoked when a prompt action defined via EUI
 * is triggered. The action's name is used as the prompt identifier — the
 * editor content is requested and processed asynchronously by
 * `msg_text_cb`.
 */
static void
action_msg_composer_eui_cb(EUIAction *action,
                           GVariant *parameter,
                           gpointer user_data)
{
    gchar *action_name = NULL;
    MMsgComposerExtension *msg_composer_ext = M_MSG_COMPOSER_EXTENSION(user_data);
    EMsgComposer *composer;
    EHTMLEditor *editor;
    EContentEditor *cnt_editor;

    g_return_if_fail(M_IS_MSG_COMPOSER_EXTENSION(msg_composer_ext));

    g_object_get(action, "name", &action_name, NULL);
    if (!action_name)
        return;
    composer = E_MSG_COMPOSER(e_extension_get_extensible(E_EXTENSION(msg_composer_ext)));
    editor = e_msg_composer_get_editor(composer);
    cnt_editor = e_html_editor_get_content_editor(editor);

    struct ProofreadContext *context = g_new(struct ProofreadContext, 1);
    context->cnt_editor = cnt_editor;
    context->prompt_id = g_strdup(action_name);
    context->extension = msg_composer_ext;

    g_debug("Getting content for action: %s", action_name);
    e_content_editor_get_content(
        cnt_editor,
        E_CONTENT_EDITOR_GET_TO_SEND_PLAIN,
        NULL,
        NULL,
        msg_text_cb,
        context);

    g_free(action_name);
}

/*
 * menu_item_activate_cb:
 * GTK menu item activate handler used by the runtime popup menu.
 * `user_data` is expected to be a duplicated string containing the
 * prompt id; this function copies it into a `ProofreadContext` and
 * triggers content retrieval. The duplicate is freed here.
 */
static void
menu_item_activate_cb(GtkMenuItem *item, gpointer user_data)
{
    gchar *prompt_id = user_data;
    MMsgComposerExtension *msg_composer_ext;
    EMsgComposer *composer;
    EHTMLEditor *editor;
    EContentEditor *cnt_editor;

    if (!prompt_id)
        return;

    msg_composer_ext = M_MSG_COMPOSER_EXTENSION(g_object_get_data(G_OBJECT(item), "extension"));
    if (!msg_composer_ext)
        return;

    composer = E_MSG_COMPOSER(e_extension_get_extensible(E_EXTENSION(msg_composer_ext)));
    editor = e_msg_composer_get_editor(composer);
    cnt_editor = e_html_editor_get_content_editor(editor);

    struct ProofreadContext *context = g_new(struct ProofreadContext, 1);
    context->cnt_editor = cnt_editor;
    context->prompt_id = g_strdup(prompt_id);
    context->extension = msg_composer_ext;

    e_content_editor_get_content(
        cnt_editor,
        E_CONTENT_EDITOR_GET_TO_SEND_PLAIN,
        NULL,
        NULL,
        msg_text_cb,
        context);

    /* The prompt_id passed here was duplicated when connecting the signal;
     * free the duplicate now that we've copied the string into the context. */
    g_free(prompt_id);
}

/*
 * action_msg_composer_dropdown_cb:
 * Show a transient GTK popup menu listing all configured prompts. Each
 * menu item stores a pointer to the extension in its object data and a
 * duplicated prompt id as `user_data` so the activate handler can run the
 * proofreading flow.
 */
static void
action_msg_composer_dropdown_cb(EUIAction *action,
                                GVariant *parameter,
                                gpointer user_data)
{
    MMsgComposerExtension *msg_composer_ext = M_MSG_COMPOSER_EXTENSION(user_data);
    GtkWidget *menu;
    guint i, n_prompts;

    g_return_if_fail(M_IS_MSG_COMPOSER_EXTENSION(msg_composer_ext));

    menu = gtk_menu_new();
    n_prompts = json_array_get_length(msg_composer_ext->priv->prompts);
    for (i = 0; i < n_prompts; i++)
    {
        JsonObject *prompt = json_array_get_object_element(msg_composer_ext->priv->prompts, i);
        const gchar *name = json_object_get_string_member(prompt, "name");
        gchar *action_name = g_strdup_printf("ai-proofread-%s", name);

        GtkWidget *mi = gtk_menu_item_new_with_label(json_object_get_string_member(prompt, "name"));
        /* store extension on the menu item for retrieval in activate handler */
        g_object_set_data(G_OBJECT(mi), "extension", msg_composer_ext);
        /* Duplicate the action name for the menu item's user_data so it remains
         * valid after the entries[] array (which also holds the original
         * action_name pointer) is freed. The activate handler is responsible
         * for freeing this duplicate. */
        g_signal_connect(mi, "activate", G_CALLBACK(menu_item_activate_cb), g_strdup(action_name));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
        gtk_widget_show(mi);
    }

    gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);
}

static void
run_button_clicked_cb(GtkButton *button,
                      MMsgComposerExtension *msg_composer_ext)
{
    GtkComboBoxText *combo = g_object_get_data(G_OBJECT(button), "combo");
    const gchar *prompt_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));

    if (prompt_id)
    {
        g_debug("Run button clicked with active selection: %s", prompt_id);

        EMsgComposer *composer;
        EHTMLEditor *editor;
        EContentEditor *cnt_editor;

        g_return_if_fail(M_IS_MSG_COMPOSER_EXTENSION(msg_composer_ext));

        composer = E_MSG_COMPOSER(e_extension_get_extensible(E_EXTENSION(msg_composer_ext)));
        editor = e_msg_composer_get_editor(composer);
        cnt_editor = e_html_editor_get_content_editor(editor);

        // Create context to pass to callback
        struct ProofreadContext *context = g_new(struct ProofreadContext, 1);
        context->cnt_editor = cnt_editor;
        context->prompt_id = g_strdup(prompt_id);
        context->extension = msg_composer_ext;

        e_content_editor_get_content(
            cnt_editor,
            E_CONTENT_EDITOR_GET_TO_SEND_PLAIN,
            NULL,
            NULL,
            msg_text_cb,
            context);
    }
}

/*
 * Build EUI action entries and the corresponding EUI XML string for the
 * configured prompts. The caller receives an allocated `EUIActionEntry *`
 * containing `n_prompts + 2` entries (one per prompt, plus the parent AI
 * menu and the dropdown action). The caller is responsible for freeing the
 * strings in each entry, the entries array and the returned `GString`.
 *
 * Returns: newly allocated `EUIActionEntry *` on success, or NULL if there
 *          are no prompts. On success `*out_count` is set to `n_prompts` and
 *          `*out_eui_def` points to a new `GString` with the EUI XML.
 */
static EUIActionEntry *
build_action_entries_and_eui(JsonArray *prompts, guint *out_count, GString **out_eui_def)
{
    guint i, n_prompts;
    GString *eui_def;
    EUIActionEntry *entries;

    if (!prompts)
        return NULL;

    n_prompts = json_array_get_length(prompts);
    if (n_prompts == 0)
        return NULL;

    eui_def = g_string_new(
        "<eui>"
        "<menu id='main-menu'>"
        "<placeholder id='custom-menus'>"
        "<submenu action='ai-menu'>"
        "<placeholder id='ai-menu-holder'>");

    entries = g_new0(EUIActionEntry, n_prompts + 2);

    for (i = 0; i < n_prompts; i++)
    {
        JsonObject *prompt = json_array_get_object_element(prompts, i);
        const gchar *name = json_object_get_string_member(prompt, "name");
        const gchar *prompt_text = json_object_get_string_member(prompt, "prompt");

        gchar *action_name = g_strdup_printf("ai-proofread-%s", name);

        entries[i] = (EUIActionEntry){
            action_name,
            "tools-check-spelling",
            g_strdup(name),
            NULL,
            g_strdup(prompt_text),
            action_msg_composer_eui_cb,
            NULL,
            NULL,
            NULL};

        g_string_append_printf(eui_def,
                               "          <item action='%s'/>",
                               action_name);
    }

    g_string_append(eui_def,
                    "                                        </placeholder>"
                    "                                    </submenu>"
                    "                                </placeholder>"
                    "                            </menu>\n");

    /* Add a single toolbar button that triggers the dropdown */
    g_string_append(eui_def, "<toolbar id='main-toolbar-with-headerbar'>\n  <item action='ai-proofread-dropdown'/>\n</toolbar>\n");
    g_string_append(eui_def, "<toolbar id='main-toolbar-without-headerbar'>\n  <item action='ai-proofread-dropdown'/>\n</toolbar>\n");
    g_string_append(eui_def, "</eui>");

    /* Parent top-level AI menu action (no callback) */
    entries[n_prompts] = (EUIActionEntry){
        g_strdup("ai-menu"),
        NULL,
        g_strdup(N_("AI")),
        NULL,
        g_strdup(N_("AI tools")),
        NULL,
        NULL,
        NULL,
        NULL};

    /* Dropdown action entry (single toolbar button that shows a popup menu of prompts) */
    entries[n_prompts + 1] = (EUIActionEntry){
        g_strdup("ai-proofread-dropdown"),
        "tools-check-spelling",
        g_strdup(N_("AI _Proofread")),
        NULL,
        g_strdup(N_("AI Proofread")),
        action_msg_composer_dropdown_cb,
        NULL,
        NULL,
        NULL};

    /* Validate entries: ensure no NULL action names to avoid assertion in e_ui_action_new */
    for (i = 0; i < n_prompts + 2; i++)
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

    *out_count = n_prompts;
    *out_eui_def = eui_def;
    return entries;
}

static void
m_msg_composer_extension_add_ui(MMsgComposerExtension *msg_composer_ext,
                                EMsgComposer *composer)
{
    /*
     * Add UI elements (menu actions and toolbar button) for the configured
     * prompts. This function delegates the creation of action entries and
     * the EUI XML string to `build_action_entries_and_eui()` so the
     * implementation stays concise and easier to follow.
     */
    g_return_if_fail(M_IS_MSG_COMPOSER_EXTENSION(msg_composer_ext));
    g_return_if_fail(E_IS_MSG_COMPOSER(composer));

    /* Validate configuration before doing work */
    if (!msg_composer_ext->priv->prompts ||
        json_array_get_length(msg_composer_ext->priv->prompts) == 0)
    {
        g_warning("No prompts configured, skipping UI creation");
        return;
    }

    if (!msg_composer_ext->priv->chatgpt_api_key)
    {
        g_warning("No API key configured, skipping UI creation");
        return;
    }

    EHTMLEditor *html_editor = e_msg_composer_get_editor(composer);
    EUIManager *ui_manager = e_html_editor_get_ui_manager(html_editor);

    guint n_prompts = 0;
    GString *eui_def = NULL;
    EUIActionEntry *entries = build_action_entries_and_eui(msg_composer_ext->priv->prompts, &n_prompts, &eui_def);
    if (!entries)
    {
        g_warning("No action entries built, skipping UI registration");
        return;
    }

    /* Register actions and UI with the EUI manager */
    e_ui_manager_add_actions_with_eui_data(ui_manager, "core", GETTEXT_PACKAGE,
                                           entries, n_prompts + 2, msg_composer_ext, eui_def->str);

    /* Free allocated names and strings in entries (ownership contract) */
    for (guint i = 0; i < n_prompts + 2; i++)
    {
        g_free((gpointer)entries[i].name);
        g_free((gpointer)entries[i].label);
        g_free((gpointer)entries[i].tooltip);
    }
    g_free(entries);
    g_string_free(eui_def, TRUE);
}

static void
m_msg_composer_extension_constructed(GObject *object)
{
    EExtension *extension;
    EExtensible *extensible;

    /* Chain up to parent's method. */
    G_OBJECT_CLASS(m_msg_composer_extension_parent_class)->constructed(object);

    extension = E_EXTENSION(object);
    extensible = e_extension_get_extensible(extension);

    m_msg_composer_extension_add_ui(M_MSG_COMPOSER_EXTENSION(object), E_MSG_COMPOSER(extensible));
}

static void
m_msg_composer_extension_class_init(MMsgComposerExtensionClass *class)
{
    GObjectClass *object_class;
    EExtensionClass *extension_class;

    object_class = G_OBJECT_CLASS(class);
    object_class->constructed = m_msg_composer_extension_constructed;

    /* Set the type to extend, it's supposed to implement the EExtensible interface */
    extension_class = E_EXTENSION_CLASS(class);
    extension_class->extensible_type = E_TYPE_MSG_COMPOSER;
}

static void
m_msg_composer_extension_class_finalize(MMsgComposerExtensionClass *class)
{
}

static void
m_msg_composer_extension_init(MMsgComposerExtension *msg_composer_ext)
{
    msg_composer_ext->priv = m_msg_composer_extension_get_instance_private(msg_composer_ext);
    msg_composer_ext->priv->prompts = load_prompts();
    msg_composer_ext->priv->chatgpt_api_key = load_api_key();
}

static void
m_msg_composer_extension_dispose(GObject *object)
{
    MMsgComposerExtension *msg_composer_ext = M_MSG_COMPOSER_EXTENSION(object);

    if (msg_composer_ext->priv->prompts)
    {
        json_array_unref(msg_composer_ext->priv->prompts);
        msg_composer_ext->priv->prompts = NULL;
    }

    g_free(msg_composer_ext->priv->chatgpt_api_key);
    msg_composer_ext->priv->chatgpt_api_key = NULL;

    /* Chain up to parent's method */
    G_OBJECT_CLASS(m_msg_composer_extension_parent_class)->dispose(object);
}

void m_msg_composer_extension_type_register(GTypeModule *type_module)
{
    m_msg_composer_extension_register_type(type_module);
}
