/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_log.h>
#include <fluent-bit/flb_mem.h>
#include <fluent-bit/flb_str.h>
#include <fluent-bit/stream_processor/flb_sp_parser.h>

#include "sql_parser.h"
#include "sql_lex.h"

void flb_sp_cmd_destroy(struct flb_sp_cmd *cmd)
{
    struct mk_list *head;
    struct mk_list *tmp;
    struct flb_sp_cmd_key *key;
    struct flb_sp_cmd_prop *prop;

    /* remove keys */
    mk_list_foreach_safe(head, tmp, &cmd->keys) {
        key = mk_list_entry(head, struct flb_sp_cmd_key, _head);
        mk_list_del(&key->_head);
        flb_sp_cmd_key_del(key);
    }

    /* stream */
    if (cmd->stream_name) {
        mk_list_foreach_safe(head, tmp, &cmd->stream_props) {
            prop = mk_list_entry(head, struct flb_sp_cmd_prop, _head);
            mk_list_del(&prop->_head);
            flb_sp_cmd_stream_prop_del(prop);
        }
        flb_sds_destroy(cmd->stream_name);
    }
    flb_sds_destroy(cmd->source_name);
    flb_free(cmd);
}

void flb_sp_cmd_key_del(struct flb_sp_cmd_key *key)
{
    if (key->name) {
        flb_sds_destroy(key->name);
    }
    if (key->alias) {
        flb_sds_destroy(key->alias);
    }
    flb_free(key);
}

int flb_sp_cmd_key_add(struct flb_sp_cmd *cmd, int aggr_func,
                       char *key_name, char *key_alias)
{
    struct flb_sp_cmd_key *key;

    key = flb_calloc(1, sizeof(struct flb_sp_cmd_key));
    if (!key) {
        flb_errno();
        return -1;
    }

    /* key name and aliases works when the selection is not a wildcard */
    if (key_name) {
        key->name = flb_sds_create(key_name);
        if (!key->name) {
            flb_sp_cmd_key_del(key);
            return -1;
        }
    }
    else {
        /*
         * This is a wildcard selection, make sure if any aggregation function
         * exists only apply for COUNT().
         */
        if (aggr_func > 0  && aggr_func != FLB_SP_COUNT) {
            flb_sp_cmd_key_del(key);
            return -1;
        }
    }

    if (key_alias) {
        key->alias = flb_sds_create(key_alias);
        if (!key->alias) {
            flb_sp_cmd_key_del(key);
            return -1;
        }
    }

    /* Aggregation function */
    if (aggr_func > 0) {
        key->aggr_func = aggr_func;
    }

    mk_list_add(&key->_head, &cmd->keys);
    return 0;
}

int flb_sp_cmd_source(struct flb_sp_cmd *cmd, int type, char *source)
{
    cmd->source_type = type;
    cmd->source_name = flb_sds_create(source);
    if (!cmd->source_name) {
        flb_errno();
        return -1;
    }

    return 0;
}

void flb_sp_cmd_dump(struct flb_sp_cmd *cmd)
{
    struct mk_list *head;
    struct mk_list *tmp;
    struct flb_sp_cmd_key *key;

    /* Lookup keys */
    printf("== KEYS ==\n");
    mk_list_foreach_safe(head, tmp, &cmd->keys) {
        key = mk_list_entry(head, struct flb_sp_cmd_key, _head);
        printf("- '%s'\n", key->name);
    }
    printf("== SOURCE ==\n");
    if (cmd->source_type == FLB_SP_STREAM) {
        printf("stream => ");
    }
    else if (cmd->source_type == FLB_SP_TAG) {
        printf("tag match => ");
    }

    printf("'%s'\n", cmd->source_name);
}

struct flb_sp_cmd *flb_sp_cmd_create(char *sql)
{
    int ret;
    yyscan_t scanner;
    YY_BUFFER_STATE buf;
    struct flb_sp_cmd *cmd;

    /* create context */
    cmd = flb_calloc(1, sizeof(struct flb_sp_cmd));
    if (!cmd) {
        flb_errno();
        return NULL;
    }
    mk_list_init(&cmd->stream_props);
    mk_list_init(&cmd->keys);

    /* Flex/Bison work */
    yylex_init(&scanner);
    buf = yy_scan_string(sql, scanner);

    ret = yyparse(cmd, scanner);
    if (ret != 0) {
        flb_sp_cmd_destroy(cmd);
        return NULL;
    }

    yy_delete_buffer(buf, scanner);
    yylex_destroy(scanner);

    return cmd;
}

int flb_sp_cmd_stream_new(struct flb_sp_cmd *cmd, char *stream_name)
{
    cmd->stream_name = flb_sds_create(stream_name);
    if (!cmd->stream_name) {
        return -1;
    }

    cmd->type = FLB_SP_CREATE_STREAM;
    return 0;
}

int flb_sp_cmd_stream_prop_add(struct flb_sp_cmd *cmd, char *key, char *val)
{
    struct flb_sp_cmd_prop *prop;

    prop = flb_malloc(sizeof(struct flb_sp_cmd_prop));
    if (!prop) {
        flb_errno();
        return -1;
    }

    prop->key = flb_sds_create(key);
    if (!prop->key) {
        flb_free(prop);
        return -1;
    }

    prop->val = flb_sds_create(val);
    if (!prop->val) {
        flb_free(prop->key);
        flb_free(prop);
        return -1;
    }

    mk_list_add(&prop->_head, &cmd->stream_props);
    return 0;
}

void flb_sp_cmd_stream_prop_del(struct flb_sp_cmd_prop *prop)
{
    if (prop->key) {
        flb_sds_destroy(prop->key);
    }
    if (prop->val) {
        flb_sds_destroy(prop->val);
    }
    flb_free(prop);
}