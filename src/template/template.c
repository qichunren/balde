/*
 * balde: A microframework for C based on GLib and bad intentions.
 * Copyright (C) 2013-2015 Rafael G. Martins <rafael@rafaelmartins.eng.br>
 *
 * This program can be distributed under the terms of the LGPL-2 License.
 * See the file COPYING.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include "template.h"
#include "parser.h"


void
balde_template_build_state(const gchar *filename, balde_template_state_t **state)
{
    if (*state == NULL) {
        *state = g_new(balde_template_state_t, 1);
        (*state)->includes = NULL;
        (*state)->decls = NULL;
        (*state)->free_decls = NULL;
        (*state)->args = NULL;
        (*state)->format = g_string_new("");
        (*state)->decl_count = 1;
    }

    GRegex *re_percent = g_regex_new("%", 0, 0, NULL);
    gchar *dirname = g_path_get_dirname(filename);
    gchar *template_source;
    if (!g_file_get_contents(filename, &template_source, NULL, NULL))
        g_critical("Failed to read source file: %s\n", filename);
    GSList *blocks = balde_template_parse(template_source);
    g_free(template_source);

    gchar *tmp_str;
    GString *tmp_string;

    for (GSList *tmp = blocks; tmp != NULL; tmp = g_slist_next(tmp)) {
        balde_template_block_t *node = tmp->data;
        switch (node->type) {
            case BALDE_TEMPLATE_IMPORT_BLOCK:
                (*state)->includes = g_slist_append((*state)->includes,
                    g_strdup(((balde_template_import_block_t*) node->block)->import));
                break;
            case BALDE_TEMPLATE_INCLUDE_BLOCK:
                tmp_str = g_build_filename(dirname,
                    ((balde_template_include_block_t*) node->block)->include,
                    NULL);
                balde_template_build_state(tmp_str, state);
                g_free(tmp_str);
                break;
            case BALDE_TEMPLATE_CONTENT_BLOCK:
                tmp_str = g_regex_replace_literal(re_percent,
                    ((balde_template_content_block_t*) node->block)->content,
                    -1, 0, "%%", 0, NULL);
                g_string_append((*state)->format, tmp_str);
                g_free(tmp_str);
                break;
            case BALDE_TEMPLATE_PRINT_VAR_BLOCK:
                g_string_append((*state)->format, "%s");
                (*state)->args = g_slist_append((*state)->args,
                    g_strdup_printf(
                        "balde_response_get_tmpl_var(response, \"%s\")",
                        ((balde_template_print_var_block_t*) node->block)->variable));
                break;
            case BALDE_TEMPLATE_PRINT_FN_CALL_BLOCK:
                g_string_append((*state)->format, "%s");
                (*state)->args = g_slist_append((*state)->args,
                    g_strdup_printf("a%d", (*state)->decl_count));
                (*state)->free_decls = g_slist_prepend((*state)->free_decls,
                    g_strdup_printf("a%d", (*state)->decl_count));
                tmp_string = g_string_new("");
                g_string_append_printf(tmp_string,
                    "gchar *a%d = balde_tmpl_%s(app, request", (*state)->decl_count++,
                    ((balde_template_print_fn_call_block_t*) node->block)->name);
                if (((balde_template_print_fn_call_block_t*) node->block)->args != NULL)
                    g_string_append(tmp_string, ", ");
                else
                    g_string_append(tmp_string, ")");
                for (GSList *tmp2 = ((balde_template_print_fn_call_block_t*) node->block)->args;
                        tmp2 != NULL; tmp2 = g_slist_next(tmp2)) {
                    switch (((balde_template_fn_arg_t*) tmp2->data)->type) {
                        case BALDE_TEMPLATE_FN_ARG_STRING:
                        case BALDE_TEMPLATE_FN_ARG_INT:
                        case BALDE_TEMPLATE_FN_ARG_FLOAT:
                        case BALDE_TEMPLATE_FN_ARG_BOOL:
                        case BALDE_TEMPLATE_FN_ARG_NULL:
                            g_string_append(tmp_string,
                                ((balde_template_fn_arg_t*) tmp2->data)->content);
                            break;
                        case BALDE_TEMPLATE_FN_ARG_VAR:
                            g_string_append_printf(tmp_string,
                                "balde_response_get_tmpl_var(response, \"%s\")",
                                ((balde_template_fn_arg_t*) tmp2->data)->content);
                            break;
                    }
                    if (g_slist_next(tmp2) == NULL)
                        g_string_append(tmp_string, ")");
                    else
                        g_string_append(tmp_string, ", ");
                }
                (*state)->decls = g_slist_append((*state)->decls,
                    g_string_free(tmp_string, FALSE));
                break;
        }
    }

    balde_template_free_blocks(blocks);
    g_free(dirname);
    g_regex_unref(re_percent);
}


void
balde_template_free_state(balde_template_state_t *state)
{
    if (state == NULL)
        return;
    g_slist_free_full(state->includes, g_free);
    g_slist_free_full(state->decls, g_free);
    g_slist_free_full(state->free_decls, g_free);
    g_slist_free_full(state->args, g_free);
    g_string_free(state->format, TRUE);
    g_free(state);
}


gchar*
balde_template_generate_source(const gchar *template_name, const gchar *file_name)
{
    balde_template_state_t *state = NULL;
    balde_template_build_state(file_name, &state);

    GString *rv = g_string_new(
        "// WARNING: this file was generated automatically by balde-template-gen\n"
        "\n"
        "#include <balde.h>\n"
        "#include <glib.h>\n");

    for (GSList *tmp = state->includes; tmp != NULL; tmp = g_slist_next(tmp))
        g_string_append_printf(rv, "#include <%s>\n", (gchar*) tmp->data);

    gchar *escaped = g_strescape(state->format->str, "");

    g_string_append_printf(rv,
        "\n"
        "static const gchar *balde_template_%s_format = \"%s\";\n"
        "extern void balde_template_%s(balde_app_t *app, balde_request_t *request, "
        "balde_response_t *response);\n"
        "\n"
        "void\n"
        "balde_template_%s(balde_app_t *app, balde_request_t *request, "
        "balde_response_t *response)\n"
        "{\n",
        template_name, escaped, template_name, template_name);

    for (GSList *tmp = state->decls; tmp != NULL; tmp = g_slist_next(tmp))
        g_string_append_printf(rv, "    %s;\n", (gchar*) tmp->data);

    if (state->args == NULL) {
        g_string_append_printf(rv,
            "    gchar *rv = g_strdup(balde_template_%s_format);\n",
            template_name);
    }
    else {
        g_string_append_printf(rv,
            "    gchar *rv = g_strdup_printf(balde_template_%s_format, ",
            template_name);
    }

    for (GSList *tmp = state->args; tmp != NULL; tmp = g_slist_next(tmp)) {
        g_string_append(rv, (gchar*) tmp->data);
        if (tmp->next != NULL)
            g_string_append(rv, ", ");
        else
            g_string_append(rv, ");\n");
    }

    g_string_append(rv,
        "    balde_response_append_body(response, rv);\n"
        "    g_free(rv);\n");

    for (GSList *tmp = state->free_decls; tmp != NULL; tmp = g_slist_next(tmp))
        g_string_append_printf(rv, "    g_free(%s);\n", (gchar*) tmp->data);

    g_string_append(rv, "}\n");

    g_free(escaped);
    balde_template_free_state(state);

    return g_string_free(rv, FALSE);
}


gchar*
balde_template_generate_header(const gchar *template_name)
{
    return g_strdup_printf(
        "// WARNING: this file was generated automatically by balde-template-gen\n"
        "\n"
        "#ifndef __%s_balde_template\n"
        "#define __%s_balde_template\n"
        "\n"
        "#include <balde.h>\n"
        "\n"
        "extern void balde_template_%s(balde_app_t *app, balde_request_t *request, "
        "balde_response_t *response);\n"
        "\n"
        "#endif\n", template_name, template_name, template_name);
}


gchar*
balde_template_get_name(const gchar *template_basename)
{
    gchar *template_name = g_path_get_basename(template_basename);
    for (guint i = strlen(template_name); i != 0; i--) {
        if (template_name[i] == '.') {
            template_name[i] = '\0';
            break;
        }
    }
    for (guint i = 0; template_name[i] != '\0'; i++) {
        if (!g_ascii_isalpha(template_name[i])) {
            template_name[i] = '_';
        }
    }
    return template_name;
}