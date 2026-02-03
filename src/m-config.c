/*
 * m-config.c - Configuration management for AI Proofread Plugin
 *
 * Implements loading of prompts from JSON and API key from authinfo.
 */

#include <glib.h>
#include <json-glib/json-glib.h>
#include <evolution/e-util/e-util.h>

#include "m-config.h"

/*
 * m_config_load_prompts:
 *
 * Read the prompts configuration file from the user's config directory
 * ($XDG_CONFIG_HOME/ai-proofread/prompts.json) and return a referenced
 * JsonArray containing the prompt objects.
 */
JsonArray *
m_config_load_prompts(void)
{
    const gchar *config_dir = e_get_user_config_dir();
    gchar *config_path = g_build_filename(config_dir, "ai-proofread", "prompts.json", NULL);
    JsonParser *parser = NULL;
    JsonNode *root = NULL;
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
 * parse_authinfo_line:
 * @line: A line from the authinfo file
 *
 * Parse a single line looking for the OpenAI API key pattern:
 * "machine api.openai.com login apikey password <key>"
 *
 * Returns: (transfer full) (nullable): The API key if found, or NULL
 */
static gchar *
parse_authinfo_line(const gchar *line)
{
    gchar **tokens = NULL;
    gint token_count;
    gchar *api_key = NULL;

    if (!line || line[0] == '\0')
        return NULL;

    tokens = g_strsplit(line, " ", -1);
    token_count = g_strv_length(tokens);

    /* Look for: machine api.openai.com login apikey password <key> */
    if (token_count >= 6 &&
        g_strcmp0(tokens[0], "machine") == 0 &&
        g_strcmp0(tokens[1], "api.openai.com") == 0 &&
        g_strcmp0(tokens[2], "login") == 0 &&
        g_strcmp0(tokens[3], "apikey") == 0 &&
        g_strcmp0(tokens[4], "password") == 0)
    {
        api_key = g_strdup(tokens[5]);
        g_debug("Found API key");
    }

    g_strfreev(tokens);
    return api_key;
}

/*
 * m_config_load_api_key:
 *
 * Read ~/.authinfo and try to find the OpenAI API key.
 */
gchar *
m_config_load_api_key(void)
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
            api_key = parse_authinfo_line(lines[i]);
            if (api_key)
                break;
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
 * get_config_file_path:
 *
 * Get the path to the config.json file.
 * Returns: (transfer full): The path. The caller must free the returned string.
 */
static gchar *
get_config_file_path(void)
{
    const gchar *config_dir = e_get_user_config_dir();
    return g_build_filename(config_dir, "ai-proofread", "config.json", NULL);
}

/*
 * m_config_load_model:
 */
gchar *
m_config_load_model(void)
{
    gchar *config_path = get_config_file_path();
    JsonParser *parser = NULL;
    gchar *model = NULL;
    GError *error = NULL;

    g_debug("Loading model from: %s", config_path);

    parser = json_parser_new();
    if (json_parser_load_from_file(parser, config_path, &error))
    {
        JsonNode *root = json_parser_get_root(parser);
        if (JSON_NODE_HOLDS_OBJECT(root))
        {
            JsonObject *obj = json_node_get_object(root);
            if (json_object_has_member(obj, "model"))
            {
                model = g_strdup(json_object_get_string_member(obj, "model"));
                g_debug("Loaded model: %s", model);
            }
        }
    }
    else
    {
        g_debug("No config file found or error loading: %s", error->message);
        g_error_free(error);
    }

    g_object_unref(parser);
    g_free(config_path);

    if (!model)
        model = g_strdup(M_CONFIG_DEFAULT_MODEL);

    return model;
}

/*
 * m_config_save_model:
 */
gboolean
m_config_save_model(const gchar *model)
{
    gchar *config_path = get_config_file_path();
    gchar *config_dir = g_path_get_dirname(config_path);
    JsonBuilder *builder = NULL;
    JsonGenerator *generator = NULL;
    JsonNode *root = NULL;
    gboolean success = FALSE;
    GError *error = NULL;

    g_return_val_if_fail(model != NULL, FALSE);

    g_debug("Saving model to: %s", config_path);

    /* Ensure directory exists */
    if (g_mkdir_with_parents(config_dir, 0700) != 0)
    {
        g_warning("Failed to create config directory: %s", config_dir);
        goto cleanup;
    }

    /* Load existing config or create new */
    JsonParser *parser = json_parser_new();
    JsonObject *config_obj = NULL;

    if (json_parser_load_from_file(parser, config_path, NULL))
    {
        JsonNode *existing_root = json_parser_get_root(parser);
        if (JSON_NODE_HOLDS_OBJECT(existing_root))
        {
            config_obj = json_object_ref(json_node_get_object(existing_root));
        }
    }

    if (!config_obj)
        config_obj = json_object_new();

    /* Update model */
    if (json_object_has_member(config_obj, "model"))
        json_object_remove_member(config_obj, "model");
    json_object_set_string_member(config_obj, "model", model);

    /* Generate JSON */
    builder = json_builder_new();
    json_builder_begin_object(builder);

    GList *members = json_object_get_members(config_obj);
    for (GList *l = members; l != NULL; l = l->next)
    {
        const gchar *member_name = l->data;
        JsonNode *member_node = json_object_get_member(config_obj, member_name);
        json_builder_set_member_name(builder, member_name);
        json_builder_add_value(builder, json_node_copy(member_node));
    }
    g_list_free(members);

    json_builder_end_object(builder);

    generator = json_generator_new();
    json_generator_set_pretty(generator, TRUE);
    root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);

    success = json_generator_to_file(generator, config_path, &error);
    if (!success)
    {
        g_warning("Failed to save config: %s", error->message);
        g_error_free(error);
    }
    else
    {
        g_debug("Model saved: %s", model);
    }

    json_object_unref(config_obj);
    g_object_unref(parser);

cleanup:
    if (root)
        json_node_free(root);
    if (generator)
        g_object_unref(generator);
    if (builder)
        g_object_unref(builder);
    g_free(config_dir);
    g_free(config_path);

    return success;
}
