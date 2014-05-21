/**
 * ulogd_output_REDIS.c
 *
 * ulogd output target to feed data to a Redis database.
 *
 * (C) 2014 Jason Hensley <jhensley@subfx.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <ctype.h>
#include <ulogd/ulogd.h>
#include <ulogd/common.h>
#include <hiredis/hiredis.h>

#define ULOGD_REDIS_MAX_DATA_SIZE 128
#define ULOGD_REDIS_MAX_HASH_ENTRIES 128
#define ULOGD_REDIS_MAX_MACRO_FIELDS 8

#define ULOGD_REDIS_DEFAULT_HOST "127.0.0.1"
#define ULOGD_REDIS_DEFAULT_PORT 6379
#define ULOGD_REDIS_DEFAULT_DB 1

#define ULOGD_REDIS_MACRO_OPEN  "{{"
#define ULOGD_REDIS_MACRO_CLOSE "}}"

#define ULOGD_REDIS_DEFAULT_KEYFORMAT "{{ip.saddr}}|{{ip.daddr}}|{{oob.time.sec}}.{{oob.time.usec}}"

#define NIPQUAD(addr)          \
  ((unsigned char *)&addr)[0], \
  ((unsigned char *)&addr)[1], \
  ((unsigned char *)&addr)[2], \
  ((unsigned char *)&addr)[3]

static struct config_keyset redis_cfg_kset = {
	.num_ces = 9,
	.ces = {
    {
      .key = "host",
      .type = CONFIG_TYPE_STRING,
      .options = CONFIG_OPT_NONE,
      .u = { .string = ULOGD_REDIS_DEFAULT_HOST },
    },
    {
      .key = "port",
      .type = CONFIG_TYPE_INT,
      .options = CONFIG_OPT_NONE,
      .u = { .value = ULOGD_REDIS_DEFAULT_PORT },
    },
    {
      .key = "sockpath",
      .type = CONFIG_TYPE_STRING,
      .options = CONFIG_OPT_NONE,
    },
    {
      .key = "db",
      .type = CONFIG_TYPE_INT,
      .options = CONFIG_OPT_NONE,
      .u = { .value = ULOGD_REDIS_DEFAULT_DB },
    },
    {
      .key = "passwd",
      .type = CONFIG_TYPE_STRING,
      .options = CONFIG_OPT_NONE,
    },
    {
      .key = "expire",
      .type = CONFIG_TYPE_INT,
      .options = CONFIG_OPT_NONE,
      .u = { .value = 0 },
    },
    {
      .key = "keyformat",
      .type = CONFIG_TYPE_STRING,
      .options = CONFIG_OPT_NONE,
      .u = { .string = ULOGD_REDIS_DEFAULT_KEYFORMAT },
    },
    {
      .key = "pipeline",
      .type = CONFIG_TYPE_INT,
      .options = CONFIG_OPT_NONE,
      .u = { .value = 0 },
    },
    {
      .key = "exclude",
      .type = CONFIG_TYPE_STRING,
      .options = CONFIG_OPT_NONE,
    },
  },
};

#define redis_host_ce(x)     (x->config_kset->ces[0])
#define redis_port_ce(x)     (x->config_kset->ces[1])
#define redis_sock_ce(x)     (x->config_kset->ces[2])
#define redis_db_ce(x)	     (x->config_kset->ces[3])
#define redis_passwd_ce(x)   (x->config_kset->ces[4])
#define redis_expire_ce(x)  (x->config_kset->ces[5])
#define redis_keyfmt_ce(x)   (x->config_kset->ces[6])
#define redis_pipeline_ce(x) (x->config_kset->ces[7])
#define redis_exclude_ce(x)  (x->config_kset->ces[8])

struct macro_field {
  char field[ULOGD_MAX_KEYLEN];
  char macro[ULOGD_MAX_KEYLEN+4];
};

struct redis_cmd {
  char *argv[ULOGD_REDIS_MAX_HASH_ENTRIES];
  size_t argvlen[ULOGD_REDIS_MAX_HASH_ENTRIES];
  int argc;
};

struct redis_ctx {
  redisContext *c;
  u_int16_t pipelined;
  int bufavail;
  char keybuf[ULOGD_REDIS_MAX_DATA_SIZE];
  char *excludes_buf;
  char **excludes;
  struct redis_cmd cmd;
  struct macro_field macros[ULOGD_REDIS_MAX_MACRO_FIELDS];
};

static int connect_redis(struct ulogd_pluginstance *upi)
{
  char *host = NULL;
  char *sockpath = NULL;
  char *passwd = NULL;
  int db;
  int port;
  struct timeval tv;
  redisReply *reply;
  struct redis_ctx *ctx = (struct redis_ctx *)upi->private;

  host = redis_host_ce(upi).u.string;
  passwd = (strlen(redis_passwd_ce(upi).u.string)) ? redis_passwd_ce(upi).u.string : NULL;
  sockpath = (strlen(redis_sock_ce(upi).u.string)) ? redis_sock_ce(upi).u.string   : NULL;
  port = redis_port_ce(upi).u.value;
  db = redis_db_ce(upi).u.value;

  tv.tv_sec  = 1;
  tv.tv_usec = 5000;

  if (sockpath) {
    ulogd_log(ULOGD_DEBUG, "Connecting to Redis via UNIX socket at: %s\n", sockpath);
    ctx->c = redisConnectUnixWithTimeout(sockpath, tv);
  } else {
    ulogd_log(ULOGD_DEBUG, "Connecting to Redis via TCP at: %s:%d\n", host, port);
    ctx->c = redisConnectWithTimeout(host, port, tv);
    //redisEnableKeepAlive(ctx->c);
  }

  if (ctx->c->err) {
    ulogd_log(ULOGD_ERROR, "Failed to connect to redis: %s\n", ctx->c->errstr);
    return ULOGD_IRET_ERR;
  }

  if (passwd) {
    reply = redisCommand(ctx->c, "AUTH %s", passwd);
    if (reply->type == REDIS_REPLY_ERROR) {
      ulogd_log(ULOGD_ERROR, "Password authentication failed: %s\n", reply->str);
      goto conn_err_free_reply;
    }
    freeReplyObject(reply);
  }

  if (db) {
    reply = redisCommand(ctx->c, "SELECT %d", db);
    if (reply->type == REDIS_REPLY_ERROR) {
      ulogd_log(ULOGD_ERROR, "Failed to change to db %d: %s\n", db, reply->str);
      goto conn_err_free_reply;
    }
    freeReplyObject(reply);
  }

  return ULOGD_IRET_OK;
conn_err_free_reply:
  freeReplyObject(reply);
  return ULOGD_IRET_ERR;
}

static int disconnect_redis(redisContext *c)
{
  redisFree(c);
  return ULOGD_IRET_OK;
}

static inline char *get_field_name(struct ulogd_key *key)
{
  return (key->cim_name) ? key->cim_name : key->name;
}

static inline void str_replace(char *str, char *find, char *repl)
{
  char *pos;
  int findlen = strlen(find);
  int repllen = strlen(repl);

  while ((pos = strstr(str, find))) {
    memmove(pos + repllen, pos + findlen, strlen(pos) - findlen + 1);
    memcpy(pos, repl, repllen);
  }
}

static inline char *trim(char *str)
{
  char *end;

  /* Trim leading space */
  while (isspace(*str)) str++;

  if (*str == 0)
    return str;

  /* Trim trailing space */
  end = str + strlen(str) - 1;
  while (end > str && isspace(*end)) end--;
  *(end+1) = 0;

  return str;
}

static char **str_split(char *src, const char delim) {
  size_t num_sub_str = 0;
  char *tmp = src;
  bool found_delim = true;

  while (*tmp) {
    if (*tmp == delim) {
      *tmp = 0;
      found_delim = true;
    } 
    else if (found_delim) {
      num_sub_str++;
      found_delim = false;
    }
    tmp++;
  }

  ulogd_log(ULOGD_DEBUG, "Number excludes = %d\n", num_sub_str);
  if (num_sub_str <= 0) {
    /* No substrings found */
    return 0;
  }

  char **sub_strings = (char **)malloc((sizeof(char *) * num_sub_str) + 1);
  const char *term = tmp;
  bool found_null = true;
  size_t idx = 0;

  tmp = src;

  while (tmp < term) {
    if (!*tmp)
      found_null = true;
    else if (found_null) {
      sub_strings[idx] = trim(tmp);
      found_null = false;
      idx++;
    }
    tmp++;
  }
  sub_strings[num_sub_str] = NULL;

  return sub_strings;
}

static int setup_excluded_fields(struct ulogd_pluginstance *upi)
{
  struct redis_ctx *ctx = (struct redis_ctx *)upi->private;
  ctx->excludes_buf = strdup(redis_exclude_ce(upi).u.string);
  unsigned int i;

  ctx->excludes = str_split(ctx->excludes_buf, ',');

  ulogd_log(ULOGD_DEBUG, "Excluding fields: \n");
  for (i = 0; ctx->excludes[i]; ++i) {
    ulogd_log(ULOGD_DEBUG, "    %s\n", ctx->excludes[i]);
  }

  return ULOGD_IRET_OK;
}

static inline int field_is_excluded(struct redis_ctx *ctx, const char *field) {
  int i;

  for (i = 0; ctx->excludes[i]; ++i) {
    if (!strcmp(field, ctx->excludes[i]))
      return 1;
  }

  return 0;
}

static int parse_keyfmt_macros(struct ulogd_pluginstance *upi) 
{
  char *macro_start, *macro_end;
  size_t flen, mlen;
  char *keyfmt = redis_keyfmt_ce(upi).u.string;
  struct redis_ctx *ctx = (struct redis_ctx *)upi->private;
  int x = 0;

  while ((macro_start = strstr(keyfmt, ULOGD_REDIS_MACRO_OPEN))) {

    if (x >= ULOGD_REDIS_MAX_MACRO_FIELDS) {
      ulogd_log(ULOGD_ERROR, "Too many macro substitutions in keyformat.  Max is %d\n", ULOGD_REDIS_MAX_MACRO_FIELDS);
      return ULOGD_IRET_ERR;
    }

    macro_end = strstr(macro_start, ULOGD_REDIS_MACRO_CLOSE);
    if (!macro_end) {
      ulogd_log(ULOGD_ERROR, "No macro closing braces found for keyformat: %s\n", keyfmt);
      return ULOGD_IRET_ERR;
    }

    flen = macro_end - (macro_start+2);
    mlen = (macro_end+2) - macro_start;

    if (flen >= ULOGD_MAX_KEYLEN) {
      ulogd_log(ULOGD_ERROR, "Key in keyformat macro is greater than ULOGD_MAX_KEYLEN\n");
      return ULOGD_IRET_ERR;
    }

    strncpy(ctx->macros[x].field, macro_start+2, flen);
    strncpy(ctx->macros[x].macro, macro_start, mlen);

    ulogd_log(ULOGD_DEBUG, "Parsed keyformat field: %s length: %d\n", ctx->macros[x].field, flen);

    keyfmt = macro_end+2;
    if (keyfmt == '\0')
      break;
    x++;
  }

  return ULOGD_IRET_OK;
}

static int redis_append_cmd(struct redis_ctx *ctx, char *key, size_t klen, char *value, size_t vlen)
{
  if ((ctx->cmd.argc + 2) > ULOGD_REDIS_MAX_HASH_ENTRIES) {
    ulogd_log(ULOGD_ERROR, "Reached maximum hash entries!\n");
    return ULOGD_IRET_ERR;
  }

  if (!key || !value)
    return ULOGD_IRET_ERR;

  ulogd_log(ULOGD_DEBUG, "Appending key/value: %s %s\n", key, value);

  ctx->cmd.argc++;
  ctx->cmd.argv[ctx->cmd.argc-1] = strdup(key);
  ctx->cmd.argvlen[ctx->cmd.argc-1] = klen;

  ctx->cmd.argc++;
  ctx->cmd.argv[ctx->cmd.argc-1] = strdup(value);
  ctx->cmd.argvlen[ctx->cmd.argc-1] = vlen;

  return ULOGD_IRET_OK;
}

static int write_redis(struct ulogd_pluginstance *upi)
{
  int ret, i;
  unsigned int x;
  int keyavail = ULOGD_REDIS_MAX_DATA_SIZE-1;
  char tmpbuf[64];
  redisReply *reply;
  char *keyfmt = redis_keyfmt_ce(upi).u.string;
  struct redis_ctx *ctx = (struct redis_ctx *)upi->private;

  memset(&ctx->keybuf, 0, ULOGD_REDIS_MAX_DATA_SIZE);
  strncpy(ctx->keybuf, keyfmt, ULOGD_REDIS_MAX_DATA_SIZE);

  /**
   * We will backfill argv[0] and argv[1] later
   */ 
  ctx->cmd.argc = 2;

  for (x = 0; x < upi->input.num_keys; x++) {
    struct ulogd_key *key = upi->input.keys[x].u.source;
    char *field_name;

    if (!key || !IS_VALID(*key))
      continue;

    field_name = get_field_name(key);
    if (field_is_excluded(ctx, field_name))
      continue;

    switch (key->type) {
      case ULOGD_RET_STRING:
        snprintf(tmpbuf, min(key->len, sizeof(tmpbuf)), "%s", (char *)key->u.value.ptr);
        redis_append_cmd(ctx, field_name, strlen(field_name), tmpbuf, strlen(tmpbuf));
        break;
      case ULOGD_RET_BOOL:
      case ULOGD_RET_INT8:
      case ULOGD_RET_INT16:
      case ULOGD_RET_INT32:
        snprintf(tmpbuf, sizeof(tmpbuf), "%d", key->u.value.i32);
        redis_append_cmd(ctx, field_name, strlen(field_name), tmpbuf, strlen(tmpbuf));
        break;
      case ULOGD_RET_UINT8:
      case ULOGD_RET_UINT16:
      case ULOGD_RET_UINT32:
        snprintf(tmpbuf, sizeof(tmpbuf), "%d", key->u.value.ui32);
        redis_append_cmd(ctx, field_name, strlen(field_name), tmpbuf, strlen(tmpbuf));
        break;
      case ULOGD_RET_UINT64:
        snprintf(tmpbuf, sizeof(tmpbuf), "%lu", key->u.value.ui64);
        redis_append_cmd(ctx, field_name, strlen(field_name), tmpbuf, strlen(tmpbuf));
        break;
      case ULOGD_RET_IPADDR:
        snprintf(tmpbuf, sizeof(tmpbuf), "%u.%u.%u.%u", NIPQUAD(key->u.value.ui32));
        redis_append_cmd(ctx, field_name, strlen(field_name), tmpbuf, strlen(tmpbuf));
        break;
      default:
        ulogd_log(ULOGD_DEBUG, "Unknown key type: %d for field: %s\n", key->type, field_name);
        break;
    }

    if (!strlen(tmpbuf))
      continue;

    for (i = 0; i < ULOGD_REDIS_MAX_MACRO_FIELDS; ++i) {
      if (!strcmp(field_name, ctx->macros[i].field)) {
        keyavail -= strlen(tmpbuf);
        if (keyavail >= 0)
          str_replace(ctx->keybuf, ctx->macros[i].macro, tmpbuf);
        break;
      }
    }
  }

  /**
   * Set argv[1] to the hash key
   */ 
  ctx->cmd.argv[1] = strdup(ctx->keybuf);
  ctx->cmd.argvlen[1] = strlen(ctx->keybuf);

  redisAppendCommandArgv(ctx->c, ctx->cmd.argc, (const char **)ctx->cmd.argv, ctx->cmd.argvlen);

  if (redis_expire_ce(upi).u.value > 0)
    redisAppendCommand(ctx->c, "EXPIRE %s %d", ctx->keybuf, redis_expire_ce(upi).u.value);

  /**
   *  No need to free argv[0] since it will always contain HMSET
   */
  for (i = 1; i < ctx->cmd.argc; ++i)
    free(ctx->cmd.argv[i]);

  ctx->pipelined++;

retry_redis_write:

  if (ctx->pipelined >= redis_pipeline_ce(upi).u.value) {
    ulogd_log(ULOGD_DEBUG, "Sending %d pipelined commands to Redis\n", ctx->pipelined);

    while (ctx->pipelined) {

      ret = redisGetReply(ctx->c, (void **)&reply);

      if (ret == REDIS_ERR) {

        ulogd_log(ULOGD_ERROR, "Failed to send data to redis: %s\n", ctx->c->errstr);

        if (ctx->c->err == REDIS_ERR_IO || ctx->c->err == REDIS_ERR_EOF) {
          /* Attempt to reconnect to Redis */
          ulogd_log(ULOGD_INFO, "Attempting to reconnect to Redis...\n");
          disconnect_redis(ctx->c);
          if (connect_redis(upi) == ULOGD_IRET_OK)
            goto retry_redis_write;
        }
      }
      if (reply->type == REDIS_REPLY_ERROR) {
        ulogd_log(ULOGD_ERROR, "Redis command failed: %s\n", reply->str);
      }
      freeReplyObject(reply);
      ctx->pipelined--;
    }
  }

  return ULOGD_IRET_OK;
}

static int start(struct ulogd_pluginstance *upi)
{
  struct redis_ctx *ctx = (struct redis_ctx *)upi->private;

  ctx->cmd.argv[0] = "HMSET";
  ctx->cmd.argvlen[0] = 5;

  return connect_redis(upi);
}

static int stop(struct ulogd_pluginstance *upi)
{
  struct redis_ctx *ctx = (struct redis_ctx *)upi->private;

  if (ctx->excludes_buf)
    free(ctx->excludes_buf);

  if (ctx->excludes)
    free(ctx->excludes);

  return disconnect_redis(ctx->c);
}

static int configure(struct ulogd_pluginstance *upi, struct ulogd_pluginstance_stack *stack)
{
  int ret = 0;

  ulogd_log(ULOGD_DEBUG, "parsing config file section %s\n", upi->id);

  ret = ulogd_wildcard_inputkeys(upi);
  if (ret < 0)
    return ret;

  ret = config_parse_file(upi->id, upi->config_kset);
  if (ret)
    return ret;

  ret = parse_keyfmt_macros(upi);
  if (ret)
    return ret;

  ret = setup_excluded_fields(upi);
  if (ret)
    return ret;

  return ret;
}

/* XXX TODO */
static void sig_handler(struct ulogd_pluginstance *upi, int sig) {
  return;
}

static struct ulogd_plugin redis_pluging = {
  .name = "REDIS",
  .input = {
    .type = ULOGD_DTYPE_RAW | ULOGD_DTYPE_PACKET | ULOGD_DTYPE_FLOW | ULOGD_DTYPE_SUM,
  },
  .output = {
    .type = ULOGD_DTYPE_SINK,
  },
  .config_kset = &redis_cfg_kset,
  .priv_size   = sizeof(struct redis_ctx),
  .configure   = &configure,
  .start       = &start,
  .stop        = &stop,
  .signal      = &sig_handler,
  .interp      = &write_redis,
  .version     = VERSION,
};

void __attribute__ ((constructor)) init(void);

void init(void)
{
  ulogd_register_plugin(&redis_pluging);
}
