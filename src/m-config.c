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
