#include "common/cmd.h"
#include "allocator/allocator.h"
#include "common/stringutil.h"
#include "common/pimcpy.h"
#include "common/cvar.h"
#include "containers/dict.h"
#include "containers/queue.h"
#include "io/fd.h"
#include "assets/asset_system.h"
#include "common/time.h"
#include "common/profiler.h"

// ----------------------------------------------------------------------------

typedef struct cmdalias_s
{
    char* value;
} cmdalias_t;

typedef struct cmdbrain_s
{
    cbuf_t cbuf;
    i32 yieldframe;
} cmdbrain_t;

// ----------------------------------------------------------------------------

static bool IsSpecialChar(char c);
static char** cmd_tokenize(const char* text, i32* argcOut);
static const char* cmd_parse(const char* text, char** tokenOut);
static cmdstat_t cmd_alias_fn(i32 argc, const char** argv);
static cmdstat_t cmd_execfile_fn(i32 argc, const char** argv);
static cmdstat_t cmd_wait_fn(i32 argc, const char** argv);

// ----------------------------------------------------------------------------

static dict_t ms_cmds;
static dict_t ms_aliases;
static queue_t ms_cmdqueue;

// ----------------------------------------------------------------------------

void cmd_sys_init(void)
{
    dict_new(&ms_cmds, sizeof(cmdfn_t), EAlloc_Perm);
    dict_new(&ms_aliases, sizeof(cmdalias_t), EAlloc_Perm);
    queue_create(&ms_cmdqueue, sizeof(cmdbrain_t), EAlloc_Perm);
    cmd_reg("alias", cmd_alias_fn);
    cmd_reg("exec", cmd_execfile_fn);
    cmd_reg("wait", cmd_wait_fn);
}

ProfileMark(pm_update, cmd_sys_update)
void cmd_sys_update(void)
{
    ProfileBegin(pm_update);

    const i32 curFrame = time_framecount();

    cmdbrain_t brain;
    while (queue_trypop(&ms_cmdqueue, &brain, sizeof(brain)))
    {
        if (brain.yieldframe == curFrame)
        {
            queue_push(&ms_cmdqueue, &brain, sizeof(brain));
            return;
        }
        else
        {
            brain.yieldframe = curFrame;
            cmdstat_t status = cbuf_exec(&brain.cbuf);
            if (status == cmdstat_yield)
            {
                queue_push(&ms_cmdqueue, &brain, sizeof(brain));
            }
            else
            {
                cbuf_del(&brain.cbuf);
            }
        }
    }

    ProfileEnd(pm_update);
}

void cmd_sys_shutdown(void)
{
    dict_del(&ms_cmds);
    dict_del(&ms_aliases);
    cmdbrain_t brain;
    while (queue_trypop(&ms_cmdqueue, &brain, sizeof(brain)))
    {
        cbuf_del(&brain.cbuf);
    }
    queue_destroy(&ms_cmdqueue);
}

void cbuf_new(cbuf_t* buf, EAlloc allocator)
{
    ASSERT(buf);
    buf->ptr = NULL;
    buf->length = 0;
    buf->allocator = allocator;
}

void cbuf_del(cbuf_t* buf)
{
    ASSERT(buf);
    pim_free(buf->ptr);
    buf->ptr = NULL;
    buf->length = 0;
}

void cbuf_clear(cbuf_t* buf)
{
    ASSERT(buf);
    buf->length = 0;
    char* ptr = buf->ptr;
    if (ptr)
    {
        ptr[0] = 0;
    }
}

void cbuf_reserve(cbuf_t* buf, i32 size)
{
    ASSERT(buf);
    ASSERT(size >= 0);
    if (size > buf->length)
    {
        buf->ptr = pim_realloc(buf->allocator, buf->ptr, size + 1);
    }
}

void cbuf_pushfront(cbuf_t* buf, const char* text)
{
    ASSERT(buf);
    ASSERT(text);

    const i32 textLen = StrLen(text);
    const i32 bufLen = buf->length;
    const i32 newLen = bufLen + textLen;
    cbuf_reserve(buf, newLen);
    char* ptr = buf->ptr;
    pimmove(ptr + bufLen, ptr, bufLen);
    pimcpy(ptr, text, textLen);
    ptr[newLen] = 0;
    buf->length = newLen;
}

void cbuf_pushback(cbuf_t* buf, const char* text)
{
    ASSERT(buf);
    ASSERT(text);

    const i32 textLen = StrLen(text);
    const i32 bufLen = buf->length;
    const i32 newLen = bufLen + textLen;
    cbuf_reserve(buf, newLen);
    char* ptr = buf->ptr;
    pimcpy(ptr + bufLen, text, textLen);
    ptr[newLen] = 0;
    buf->length = newLen;
}

cmdstat_t cbuf_exec(cbuf_t* buf)
{
    ASSERT(buf);

    char line[1024];

    while (buf->length > 0)
    {
        char* text = buf->ptr;
        i32 i = 0;
        i32 q = 0;

        // get a line of command text
        for (; i < buf->length; ++i)
        {
            char c = text[i];
            if (c == '"')
            {
                // escape quoted characters
                ++q;
            }
            else if (!(q & 1) && (c == ';'))
            {
                break;
            }
            else if (c == '\n')
            {
                break;
            }
        }

        // copy line of text to the stack
        ASSERT(i < NELEM(line));
        pimcpy(line, text, i);
        line[i] = 0;

        // shift remaining text to front of buffer
        // in case cmd_exec pushes to front of cbuf
        if (i == buf->length)
        {
            cbuf_clear(buf);
        }
        else
        {
            ++i;
            buf->length -= i;
            pimcpy(text, text + i, buf->length);
        }

        cmdbrain_t brain;
        cmdstat_t status = cmd_exec(buf, line, cmdsrc_buffer);
        switch (status)
        {
        case cmdstat_yield:
            brain.cbuf = *buf;
            brain.yieldframe = time_framecount();
            queue_push(&ms_cmdqueue, &brain, sizeof(brain));
            return cmdstat_yield;
        break;
        case cmdstat_err:
            cbuf_del(buf);
            return cmdstat_err;
        break;
        }
    }

    cbuf_del(buf);
    return cmdstat_ok;
}

void cmd_reg(const char* name, cmdfn_t fn)
{
    ASSERT(name);
    ASSERT(fn);
    if (!dict_add(&ms_cmds, name, &fn))
    {
        dict_set(&ms_cmds, name, &fn);
    }
}

bool cmd_exists(const char* name)
{
    ASSERT(name);
    return dict_find(&ms_cmds, name) != -1;
}

const char* cmd_complete(const char* namePart)
{
    ASSERT(namePart);
    const i32 partLen = StrLen(namePart);
    const u32 width = ms_cmds.width;
    const char** names = ms_cmds.keys;
    for (u32 i = 0; i < width; ++i)
    {
        const char* name = names[i];
        if (name && !StrCmp(namePart, partLen, name))
        {
            return name;
        }
    }
    return NULL;
}

cmdstat_t cmd_exec(cbuf_t* buf, const char* line, cmdsrc_t src)
{
    ASSERT(buf);
    ASSERT(line);

    i32 argc = 0;
    char** argv = cmd_tokenize(line, &argc);
    if (!argc)
    {
        return cmdstat_err;
    }
    ASSERT(argv);

    // commands
    cmdfn_t cmd = NULL;
    if (dict_get(&ms_cmds, argv[0], &cmd))
    {
        return cmd(argc, argv);
    }

    // aliases (macros to expand into front of cbuf)
    cmdalias_t alias = { 0 };
    if (dict_get(&ms_aliases, argv[0], &alias))
    {
        cbuf_pushfront(buf, alias.value);
        return cmdstat_ok;
    }

    // cvars
    cvar_t* cvar = cvar_find(argv[0]);
    if (cvar)
    {
        if (argc == 1)
        {
            // todo: print to console the cvar name and value
            fd_printf(fd_stdout, "\"%s\" is \"%s\"\n", cvar->name, cvar->value);
            return cmdstat_ok;
        }
        if (argc >= 2)
        {
            cvar_set_str(cvar, argv[1]);
            return cmdstat_ok;
        }
    }

    fd_printf(fd_stdout, "Unknown command \"%s\"\n", argv[0]);
    return cmdstat_err;
}

// ----------------------------------------------------------------------------

static bool IsSpecialChar(char c)
{
    switch (c)
    {
    default:
        return false;
    case '{':
    case '}':
    case '(':
    case ')':
    case '\'':
    case ':':
        return true;
    }
}

static const char* cmd_parse(const char* text, char** tokenOut)
{
    ASSERT(text);
    ASSERT(tokenOut);
    *tokenOut = NULL;
    i32 len = 0;
    char c = 0;
    char token[1024];

wspace:
    // whitespace
    while ((c = *text) <= ' ')
    {
        if (!c)
        {
            return NULL;
        }
        ++text;
    }

    // comments
    if (c == '/' && text[1] == '/')
    {
        while (*text && (*text != '\n'))
        {
            ++text;
        }
        goto wspace;
    }

    // quotes
    if (c == '"')
    {
        ++text;
        while (true)
        {
            c = *text++;
            if (!c || c == '"')
            {
                ASSERT(len < NELEM(token));
                token[len] = 0;
                *tokenOut = StrDup(token, EAlloc_Temp);
                return text;
            }
            token[len++] = c;
        }
    }

    // special characters
    if (IsSpecialChar(c))
    {
        ASSERT(len < NELEM(token));
        token[len++] = c;
        token[len] = 0;
        *tokenOut = StrDup(token, EAlloc_Temp);
        return text + 1;
    }

    // words
    do
    {
        ASSERT(len < NELEM(token));
        token[len++] = c;
        c = *text++;
    } while (c > ' ' && !IsSpecialChar(c));

    ASSERT(len < NELEM(token));
    token[len] = 0;
    *tokenOut = StrDup(token, EAlloc_Temp);
    return text;
}

static char** cmd_tokenize(const char* text, i32* argcOut)
{
    ASSERT(text);
    ASSERT(argcOut);
    i32 argc = 0;
    char** argv = NULL;
    i32 i = 0;
    while (text)
    {
        char c = *text;
        while (c && (c <= ' ') && (c != '\n'))
        {
            ++text;
            c = *text;
        }
        if (!c || (c == '\n'))
        {
            break;
        }
        char* token = NULL;
        text = cmd_parse(text, &token);
        if (token)
        {
            ++argc;
            argv = pim_realloc(EAlloc_Temp, argv, sizeof(*argv) * argc);
            argv[argc - 1] = token;
        }
    }
    *argcOut = argc;
    return argv;
}

static cmdstat_t cmd_alias_fn(i32 argc, const char** argv)
{
    if (argc <= 1)
    {
        fd_puts(fd_stdout, "Current alias commands:");
        const u32 width = ms_aliases.width;
        const char** names = ms_aliases.keys;
        const cmdalias_t* aliases = ms_aliases.values;
        for (u32 i = 0; i < width; ++i)
        {
            const char* name = names[i];
            if (name)
            {
                fd_printf(fd_stdout, "%s : %s\n", name, aliases[i].value);
            }
        }
        return cmdstat_ok;
    }

    const char* name = argv[1];
    cmdalias_t alias = { 0 };
    if (dict_rm(&ms_aliases, name, &alias))
    {
        pim_free(alias.value);
        alias.value = NULL;
    }

    char cmd[1024];
    cmd[0] = 0;
    for (i32 i = 2; i < argc; ++i)
    {
        StrCat(ARGS(cmd), argv[i]);
        if ((i + 1) < argc)
        {
            StrCat(ARGS(cmd), " ");
        }
    }
    StrCat(ARGS(cmd), "\n");

    alias.value = StrDup(cmd, EAlloc_Perm);
    bool added = dict_add(&ms_aliases, name, &alias);
    ASSERT(added);

    return cmdstat_ok;
}

static cmdstat_t cmd_execfile_fn(i32 argc, const char** argv)
{
    if (argc != 2)
    {
        fd_puts(fd_stdout, "exec <filename> : executes a script file");
        return cmdstat_err;
    }
    asset_t asset;
    if (asset_get(argv[1], &asset))
    {
        cbuf_t cbuf;
        cbuf_new(&cbuf, EAlloc_Perm);
        cbuf_pushback(&cbuf, asset.pData);
        return cbuf_exec(&cbuf);
    }
    return cmdstat_err;
}

static cmdstat_t cmd_wait_fn(i32 argc, const char** argv)
{
    if (argc != 1)
    {
        fd_puts(fd_stdout, "wait : yields execution for one frame");
        return cmdstat_err;
    }
    return cmdstat_yield;
}
