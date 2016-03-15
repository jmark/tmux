#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tcl.h>

#include "tmux.h"

#include "array.h"

#include <assert.h>

enum cmd_retval cmd_tcl_exec(struct cmd *self, struct cmd_q *cmdq);

const struct cmd_entry cmd_tcl_entry /* avoid auto-create cmd for this */
=
{
	.name = "tcl",
	.alias = "t",

	.args = { "", 0, -1 },
	.usage = "[command]",

	//.cflag = CMD_CLIENT_CANFAIL,
	//.tflag = CMD_PANE,

	//.flags = 0,

	.exec = cmd_tcl_exec
};

Tcl_Interp * tcl_interp = NULL;

const char stacktrace[] = ""
"proc stacktrace {} {\n"
"    set stack \"Stack trace:\\n\"\n"
"    for {set i 1} {$i < [info level]} {incr i} {\n"
"        set lvl [info level -$i]\n"
"        set pname [lindex $lvl 0]\n"
"        append stack [string repeat \" \" $i]$pname\n"
"        foreach value [lrange $lvl 1 end] arg [info args $pname] {\n"
"            if {$value eq \"\"} {\n"
"                info default $pname $arg value\n"
"            }\n"
"            append stack \" $arg='$value'\"\n"
"        }\n"
"        append stack \\n\n"
"    }\n"
"    return $stack\n"
"}\n"
;

struct tcl_global_context;
struct tcl_global_context {
  struct cmd_q *cmdq;

  struct client         *c;
  struct session        *s;
  struct window         *w;
  struct window_pane    *wp;
  struct winlink        *wl;

  enum cmd_retval cmd_retval; /* = CMD_RETURN_NORMAL */

  struct tcl_global_context * prev;
} *tcl_g = NULL;

extern const struct cmd_entry cmd_attach_session_entry;

#define tcl2tmux_call tcl2tmux_call2

#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-label"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types-discards-qualifiers"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-label"

//void set_global_cmdq(struct cmd_q *cmdq)
//{
//  if (global.cmdq == cmdq) return;
//  if (global.cmdq) cmdq_free(global.cmdq);
//  global.cmdq = cmdq;
//  if (global.cmdq) global.cmdq->references++;
//}

void release_global(struct tcl_global_context *g)
{
  /* Just in case, do it in reverse order */

  /* no refcount for window_pane */
  if (g->w)     window_remove_ref(g->w);
  /* no refcount for winlink */
  if (g->s)     session_unref(g->s);
  if (g->c)     server_client_unref(g->c);
  if (g->cmdq)  cmdq_free(g->cmdq);
}

void release_reset_global(struct tcl_global_context *g)
{
  release_global(g);
  memset(g, 0, sizeof(*g));
}

void addref_global(struct tcl_global_context *g)
{
  /* no refcount for window_pane */
  if (g->w)     g->w->references++;
  /* no refcount for winlink */
  if (g->s)     g->s->references++;
  if (g->c)     g->c->references++;
  if (g->cmdq)  g->cmdq->references++;
}

/* supply values or fill in from cmdq -> client -> session -> all */
void fill_global(struct tcl_global_context *g,
  struct cmd_q          *cmdq,
  struct client         *c,
  struct session        *s,
  struct window         *w,
  struct window_pane    *wp,
  struct winlink        *wl)
{
  g->cmdq = cmdq;
  g->c = c;
  g->s = s;
  g->wl = wl;
  g->w = w;
  g->wp = wp;

  /* cmdq -> client */
  if (!g->c && cmdq) g->c = cmdq->client;
  /* client -> session */
  if (!g->s && g->c) g->s = g->c->session;
  if (g->s && g->c && (g->c->flags & (CLIENT_DEAD|CLIENT_SUSPENDED)) == 0) {
    if (g->s->curw) {
      /* session -> winlink */
      if (!g->wl) g->wl = g->s->curw;
      /* winlink -> window */
      if (!g->w) g->w = g->s->curw->window;
    }
  }
  /* window -> window_pane */
  if (!g->wp && g->w) g->wp = g->w->active;
}

/* fill in global state from client */
void fill_global_from_client(struct tcl_global_context *g, struct client *c)
{
  g->c = c;
  if (c == NULL) return;
  g->s = c->session;
  if (g->s == NULL || (g->c->flags & (CLIENT_DEAD|CLIENT_SUSPENDED)) != 0) return;
  if (g->s->curw) {
    g->wl = g->s->curw;
    g->w = g->s->curw->window;
  }
  if (!g->w) return;
  g->wp = g->w->active;
}

/* fill in global state from cmdq */
void fill_global_from_cmdq(struct tcl_global_context *g, struct cmd_q *cmdq)
{
  g->cmdq = cmdq;
  if (cmdq) fill_global_from_client(g, cmdq->client);
}

void global_enter(
  struct cmd_q          *cmdq,
  struct client         *c,
  struct session        *s,
  struct window         *w,
  struct window_pane    *wp,
  struct winlink        *wl)
{
  struct tcl_global_context *g = xcalloc(1, sizeof(*g));
  memset(g, 0, sizeof(*g));
  fill_global(g, cmdq, c, s, w, wp, wl);
  addref_global(g);
  g->prev = tcl_g;
  tcl_g = g;
}

void global_enter_q(struct cmd_q *cmdq)
{
  struct tcl_global_context *g = xcalloc(1, sizeof(*g));
  memset(g, 0, sizeof(*g));
  fill_global_from_cmdq(g, cmdq);
  addref_global(g);
  g->prev = tcl_g;
  tcl_g = g;
}

void global_enter_c(struct client *c)
{
  struct tcl_global_context *g = xcalloc(1, sizeof(*g));
  memset(g, 0, sizeof(*g));
  fill_global_from_client(g, c);
  addref_global(g);
  g->prev = tcl_g;
  tcl_g = g;
}

void global_leave()
{
  if (tcl_g == NULL) {
    fatal("%s:%d: %s: internal error: tcl global state stack empty", __FILE__, __LINE__, __func__);
  }
  struct tcl_global_context *g = tcl_g;
  tcl_g = g->prev;
  release_global(g);
  free(g);
}

void
tcl_cmdq_error(struct cmd_q *cmdq, const char *fmt, ...)
{
  va_list ap;
  char *msg;

  va_start(ap, fmt);
  xvasprintf(&msg, fmt, ap);
  va_end(ap);

  char * msg1 = msg;
  while(1) {
    char * msg1e = strchr(msg1, '\n');
    if (msg1e) *msg1e = 0;
    cmdq_error(cmdq, "%s", msg1);
    if (!msg1e) break;
    msg1 = msg1e+1;
    if (!*msg1) break; // strip last CR
  }

  free(msg);
}
int tcl2tmux_call1(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  struct cmd c;
  memset(&c, 0, sizeof(c));

  c.entry = (struct cmd_entry *)clientData;
  c.args = args_parse(c.entry->args.template, argc, argv);

  // do some job of cmd_parse
  if ((c.args == NULL) ||
      (c.entry->args.lower != -1 && c.args->argc < c.entry->args.lower) ||
      (c.entry->args.upper != -1 && c.args->argc > c.entry->args.upper))
  {
    cmdq_error(tcl_g->cmdq, "tmux::usage: %s %s", c.entry->name, c.entry->usage);
    Tcl_AddErrorInfo(tcl_interp, "tmux wrong number of args");
    Tcl_SetErrorCode(tcl_interp, "tmux wrong number of args", NULL);
    Tcl_SetResult(tcl_interp, "tmux wrong number of args", NULL);
    tcl_g->cmd_retval = CMD_RETURN_ERROR;
    return TCL_ERROR;
  }

  struct cmd *cmd_old = tcl_g->cmdq->cmd;
  tcl_g->cmdq->cmd = &c;

  if (cmd_prepare_state(&c, tcl_g->cmdq, NULL) != 0) {
    tcl_g->cmd_retval = CMD_RETURN_ERROR;
  } else {
    tcl_g->cmd_retval = (*c.entry->exec)(&c, tcl_g->cmdq);
  }
  tcl_g->cmdq->cmd = cmd_old;

  args_free(c.args);
  //free(c.args);

  return tcl_g->cmd_retval == CMD_RETURN_NORMAL ? TCL_OK : TCL_ERROR;
}


int tcl2tmux_call2(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  tcl_g->cmd_retval = CMD_RETURN_NORMAL;

  struct cmd *cmd = xcalloc(1, sizeof *cmd);
  cmd->entry = (struct cmd_entry *)clientData;
  cmd->args = args_parse(cmd->entry->args.template, argc, argv);

  // do some job of cmd_parse
  if ((cmd->args == NULL) ||
      (cmd->entry->args.lower != -1 && cmd->args->argc < cmd->entry->args.lower) ||
      (cmd->entry->args.upper != -1 && cmd->args->argc > cmd->entry->args.upper))
  {
    free(cmd);
    cmdq_error(tcl_g->cmdq, "tmux::usage: %s %s", cmd->entry->name, cmd->entry->usage);
    Tcl_AddErrorInfo(tcl_interp, "tmux wrong number of args");
    Tcl_SetErrorCode(tcl_interp, "tmux wrong number of args", NULL);
    Tcl_SetResult(tcl_interp, "tmux wrong number of args", NULL);
    tcl_g->cmd_retval = CMD_RETURN_ERROR;
    return TCL_ERROR;
  }

  struct cmd_list *cmdlist = xcalloc(1, sizeof *cmdlist);
  cmdlist->references = 1;
  TAILQ_INIT(&cmdlist->list);

  struct cmd_q *cmdq = cmdq_new(tcl_g->cmdq->client);
  cmdq->parent = tcl_g->cmdq;

  TAILQ_INSERT_TAIL(&cmdlist->list, cmd, qentry);

  cmdq_run(cmdq, cmdlist, NULL);

  cmd_list_free(cmdlist);
  cmdq_free(cmdq);

  return tcl_g->cmd_retval == CMD_RETURN_NORMAL ? TCL_OK : TCL_ERROR;
}


void tcl_error(const char * str)
{
    tcl_cmdq_error(tcl_g->cmdq, "%s", str);
    Tcl_AddErrorInfo(tcl_interp, str);
    Tcl_SetErrorCode(tcl_interp, str, NULL);
    Tcl_SetResult(tcl_interp, str, NULL);
}

void tcl_error_q(const char * str)
{
    Tcl_AddErrorInfo(tcl_interp, str);
    Tcl_SetErrorCode(tcl_interp, str, NULL);
    Tcl_SetResult(tcl_interp, str, NULL);
}


int tcl_format_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  tcl_g->cmd_retval = CMD_RETURN_NORMAL;
  if (argc != 2) {
    tcl_error("Usage: format string | format-time string");
    tcl_g->cmd_retval = CMD_RETURN_ERROR;
    return TCL_ERROR;
  }

  //struct cmd cmd;
  //memset(&cmd, 0, sizeof(cmd));

  //cmd.entry = &cmd_attach_session_entry;
  //char *aa[] = {"", NULL};
  //cmd.args = args_parse(cmd.entry->args.template, 1, aa);

  //if (cmd_prepare_state(&cmd, tcl_g->cmdq, NULL) != 0) {
  //  tcl_error("format: prepare_state fail");
  //  tcl_g->cmd_retval = CMD_RETURN_ERROR;
  //} else {
    //struct session	*s = global.cmdq->state.tflag.s;
    //struct client	*c = global.cmdq->client;
    //struct winlink	*wl = global.cmdq->state.tflag.wl;
    //struct window_pane	*wp = global.cmdq->state.tflag.wp;
    struct format_tree	*ft;

    ft = format_create(tcl_g->cmdq, 0);
    format_defaults(ft, tcl_g->c, tcl_g->s, tcl_g->wl, tcl_g->wp);
    char * str;
    if (clientData == 0) {
      str = format_expand(ft, argv[1]);
    } else {
      str = format_expand_time(ft, argv[1], time(NULL));
    }
// warning: incompatible pointer types passing 'void (void *)' to parameter of type 'Tcl_FreeProc *' (aka 'void (*)(char *)') [-Wincompatible-pointer-types]
    Tcl_SetResult(tcl_interp, str, (Tcl_FreeProc*)free);

    //free(str);
    format_free(ft);
  //}

  //args_free(cmd.args);

  return tcl_g->cmd_retval == CMD_RETURN_NORMAL ? TCL_OK : TCL_ERROR;
}

int tcl_tmuxparse_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  //
  if (argc != 2) {
    tcl_error("Usage: tmux::parse tmux-command-string");
    return TCL_ERROR;
  }

  //
  struct cmd_list *cmdlist = NULL;
  char * cause = NULL;

  if (cmd_string_parse(argv[1], &cmdlist, argv[1], 1, &cause) || !cmdlist) {
    tcl_error(cause);
    if (cause) free(cause);
    return TCL_ERROR;
  }
  if (cause) free(cause);

  //
  if ((int)clientData == 0) {
    Tcl_Obj *ret = Tcl_NewListObj(0, NULL);

    struct cmd *cmd;
    TAILQ_FOREACH(cmd, &cmdlist->list, qentry) {
      Tcl_Obj * tcl_cmd = Tcl_NewListObj(0, NULL);
      Tcl_ListObjAppendElement(interp, tcl_cmd, Tcl_NewStringObj(cmd->entry->name, -1));
      for (int i = 0; i < cmd->args->argc; i++) {
        Tcl_ListObjAppendElement(interp, tcl_cmd, Tcl_NewStringObj(cmd->args->argv[i], -1));
      }
      Tcl_ListObjAppendElement(interp, ret, tcl_cmd);
    }

    Tcl_SetObjResult(tcl_interp, ret);
  } else if ((int)clientData == 1) {
    Tcl_DString dstr;
    Tcl_DStringInit(&dstr);

    struct cmd *cmd;
    TAILQ_FOREACH(cmd, &cmdlist->list, qentry) {
      Tcl_DStringAppendElement(&dstr, cmd->entry->name);
      for (int i = 0; i < cmd->args->argc; i++) {
        Tcl_DStringAppendElement(&dstr, cmd->args->argv[i]);
      }
      Tcl_DStringAppend(&dstr, " ; \n", -1);
    }

    Tcl_DStringResult(interp, &dstr);
    Tcl_DStringFree(&dstr);
  } else {
    fatal("%s:%d: %s: internal error", __FILE__, __LINE__, __func__);
  }

  //
  cmd_list_free(cmdlist);

  return TCL_OK;
}

//Tcl_DString *tcl_global_dstrptr = NULL;
void (*on_cmdq_print)(char * txt, char * txt_utf8) = NULL;

void printflike(2, 3) tcl_cmdq_print_divert(struct cmd_q *cmdq, const char *fmt, ...)
{
  va_list ap;
  char *txt, *msg;

  va_start(ap, fmt);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
  vasprintf(&txt, fmt, ap);
#pragma clang diagnostic pop
  msg = utf8_sanitize(txt);
  on_cmdq_print(txt, msg);
  free(txt);
  free(msg);

  va_end(ap);
}

void cmdq_print_divert_start(void (*cb)(char * txt, char * txt_utf8))
{
  on_cmdq_print = cb;
  cmdq_print = tcl_cmdq_print_divert;
}

void cmdq_print_divert_end()
{
  on_cmdq_print = NULL;
  cmdq_print = cmdq_print_orig;
}

int cmdq_print_is_diverting()
{
  return on_cmdq_print != NULL;
}



Tcl_DString tcl_outputdivert_str;

void tcl_outputof_proc_capture_txt_cb(char * txt, char * txt_utf8)
{
  log_debug("%s:%d: output divert txt: <%s>", __FILE__, __LINE__, txt);
  log_debug("%s:%d: output divert utf: <%s>", __FILE__, __LINE__, txt_utf8);

  if (Tcl_DStringLength(&tcl_outputdivert_str)) {
    Tcl_DStringAppend(&tcl_outputdivert_str, "\n", 1);
  }
  Tcl_DStringAppend(&tcl_outputdivert_str, txt, -1);
}

void tcl_outputof_proc_capture_list_cb(char * txt, char * txt_utf8)
{
  log_debug("%s:%d: output divert txt: <%s>", __FILE__, __LINE__, txt);
  log_debug("%s:%d: output divert utf: <%s>", __FILE__, __LINE__, txt_utf8);

  Tcl_DStringAppendElement(&tcl_outputdivert_str, txt);
}

int tcl_outputdivert_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  if (argc < 2) goto usage;
  int is_start = strcmp(argv[1], "start") == 0;
  int is_end = strcmp(argv[1], "end") == 0;
  if (!is_start && !is_end) goto usage;

  //
  if (is_start) {
    if (argc != 3) goto usage;
    int is_list = strcmp(argv[2], "list") == 0;
    int is_txt = strcmp(argv[2], "txt") == 0;
    if (!is_list && !is_txt) goto usage;

    if (cmdq_print_is_diverting()) {
      return TCL_OK;
    }
    Tcl_DStringInit(&tcl_outputdivert_str);
    cmdq_print_divert_start(is_txt ? tcl_outputof_proc_capture_txt_cb : tcl_outputof_proc_capture_list_cb);

    return TCL_OK;
  } else {
    // end
    if (argc != 2) goto usage;

    if (!cmdq_print_is_diverting()) {
      tcl_error("Not diverting");
      return TCL_ERROR;
    }
    Tcl_DStringResult(interp, &tcl_outputdivert_str);
    Tcl_DStringFree(&tcl_outputdivert_str);
    cmdq_print_divert_end();

    return TCL_OK;
  }

usage:
  tcl_error(
      "Usage: tmux::_output-divert {start {txt | list} | end}}\n"
      "    tmux::_output-divert start txt\n"
      "    tmux::_output-divert start list\n"
      "    set x [tmux::_output-divert end]\n"
      " Press ^B ~ for full usage message");
  return TCL_ERROR;
}

int tcl_pblist_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  Tcl_DString ret;
  Tcl_DStringInit(&ret);

  struct paste_buffer *pb = NULL;
  while ((pb = paste_walk(pb)) != NULL) {
    // TODO: fix possible zero-byte in string
    Tcl_DStringAppendElement(&ret, paste_buffer_data(pb, NULL));
  }

  Tcl_DStringResult(interp, &ret);
  Tcl_DStringFree(&ret);

  return TCL_OK;
}

int tcl_pbcopy_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  if (argc != 2) {
    tcl_error(
        "Usage: pbcopy string : Put the string into the paste buffer");
    return TCL_ERROR;
  }

  // TODO: fix possible zero-byte in string
  paste_add(xstrdup(argv[1]), strlen(argv[1]));

  return TCL_OK;
}

int tcl_pbpaste_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  tcl_error("pbpaste not implemented. use :send-keys [pbcontent]");
  return TCL_ERROR;
}

int tcl_pbcontent_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  if (argc != 1) {
    tcl_error(
        "Usage: pbcontent : Return the current paste buffer content");
    return TCL_ERROR;
  }

  struct paste_buffer *pb = paste_get_top(NULL);
  if (pb) {
    Tcl_SetResult(interp,
        xstrdup(paste_buffer_data(pb, NULL)), (Tcl_FreeProc*)free);
  } else {
    Tcl_SetResult(interp, "", NULL);
  }

  return TCL_OK;
}

/*
 * This one works only when bound to a key in copy-mode
*/
extern const struct window_mode window_copy_mode;
void * window_copy_get_selection(struct window_pane *wp, size_t *len);
int tcl_copymodeselection_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  struct window_pane *wp = tcl_g->wp;
  if (!wp || wp->mode != &window_copy_mode) {
    Tcl_SetResult(interp, "", NULL);
    return TCL_OK;
  }

  size_t len;
  char * buf = window_copy_get_selection(wp, &len);
  buf[len] = 0;
  Tcl_SetResult(interp, buf, (Tcl_FreeProc*)free);

  return TCL_OK;
}

int tcl_print_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  if (!tcl_g->cmdq) return TCL_OK;
  for (int i=1; i < argc; i++) {
    cmdq_print(tcl_g->cmdq, "%s", argv[i]);
  }
  return TCL_OK;
}

int tcl_nop_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  return TCL_OK;
}


static const char CTX_SESSION[] = {"session"};
static const char CTX_CLIENT[] = {"client"};
static const char CTX_WINDOW[] = {"window"};
static const char CTX_PANE[] = {"pane"};

#define SHIFT_TO_ARG(NAME)  \
      if ((++argi) >= argc) { \
        cmdq_error(tcl_g->cmdq, "using " NAME ": argument required"); \
        Tcl_AddErrorInfo(tcl_interp, "using " NAME ": argument required"); \
        goto error; \
      } else {}

#define DEFINE_FINDSTATE() \
      struct cmd_find_state fs, fsCurrent = { \
        .cmdq = cmdq, \
	.flags = quiet ? CMD_FIND_QUIET : 0, \
	.current = NULL, \
	.s = s, \
	.wl = wl, \
	.w = w, \
	.wp = wp, \
	.idx = wl ? wl->idx : 0, \
      };

int tcl_using_context_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  if (argc == 1) goto error;
  enum cmd_find_type find_type;

  struct cmd_q          *cmdq = tcl_g->cmdq;
  struct client         *c = tcl_g->c;
  struct session        *s = tcl_g->s;
  struct window         *w = tcl_g->w;
  struct window_pane    *wp = tcl_g->wp;
  struct winlink        *wl = tcl_g->wl;
  int quiet = 0;

  int argi;
  for (argi = 1; argi < argc; argi++) {
    if (!strcmp(argv[argi], "-q")) {
      quiet = 1;
    } else if (!strcmp(argv[argi], "-noq")) {
      quiet = 0;
    } else if (!strcmp(argv[argi], CTX_CLIENT)) {
      SHIFT_TO_ARG("client");
      if (!(c = cmd_find_client(cmdq, argv[argi], quiet))) {
        tcl_error("Client not found");
        return TCL_ERROR;
      }
      /* from cmd_find_current_session */
      s = c->session;
      wl = s ? s->curw : NULL;
      w = wl ? wl->window : NULL;
      wp = w ? w->active : NULL;
    } else if (!strcmp(argv[argi], CTX_SESSION)) {
      SHIFT_TO_ARG("session");
      DEFINE_FINDSTATE();
      cmd_find_clear_state(&fs, cmdq, quiet ? CMD_FIND_QUIET : 0);
      if (cmd_find_target(&fs, &fsCurrent, cmdq, argv[argi], CMD_FIND_SESSION, quiet ? CMD_FIND_QUIET : 0) != 0) {
        tcl_error("Session not found");
        return TCL_ERROR;
      }
      s = fs.s; w = fs.w; wp = fs.wp; wl = fs.wl;
      //w=0; wp=0;
    } else if (!strcmp(argv[argi], CTX_WINDOW)) {
      SHIFT_TO_ARG("window");
      DEFINE_FINDSTATE();
      cmd_find_clear_state(&fs, cmdq, quiet ? CMD_FIND_QUIET : 0);
      if (cmd_find_target(&fs, &fsCurrent, cmdq, argv[argi], CMD_FIND_SESSION, quiet ? CMD_FIND_QUIET : 0) != 0) {
        tcl_error("Window not found");
        return TCL_ERROR;
      }
      s = fs.s; w = fs.w; wp = fs.wp; wl = fs.wl;
      //w=0; wp=0;
    } else if (!strcmp(argv[argi], CTX_PANE)) {
      SHIFT_TO_ARG("pane");
      DEFINE_FINDSTATE();
      cmd_find_clear_state(&fs, cmdq, quiet ? CMD_FIND_QUIET : 0);
      if (cmd_find_target(&fs, &fsCurrent, cmdq, argv[argi], CMD_FIND_SESSION, quiet ? CMD_FIND_QUIET : 0) != 0) {
        tcl_error("Pane not found");
        return TCL_ERROR;
      }
      s = fs.s; w = fs.w; wp = fs.wp; wl = fs.wl;
    } else {
      if (argc - argi != 1) {
        cmdq_error(tcl_g->cmdq, "using: unknown word: %s", argv[argi]);
        Tcl_AddErrorInfo(tcl_interp, "using: unknown word");
        goto error;
      }
      break;
    }
  }
  if (argi >= argc) {
    cmdq_error(tcl_g->cmdq, "using: no script supplied");
    Tcl_AddErrorInfo(tcl_interp, "using: no script supplied");
    goto error;
  }

  global_enter(cmdq, c, s, w, wp, wl);

  int ret = Tcl_Eval(interp, argv[argi]);

  global_leave();

  return ret;

error:
  tcl_error(
      "Usage: using [-q|-noq] CONTEXT { SCRIPT }\n"
      " Execute the SCRIPT in CONTEXT where CONTEXT is: {client c | session s | window w | pane p}\n"
      " see man tmux: target-client, target-session target-window, or target-pane."
      " Press ^B~ for full usage message");
  return TCL_ERROR;
}
#undef SHIFT_TO_ARG


char * window_choose_mode_data_get_cmd_ok(struct window_choose_mode_data *modedata);
char * window_choose_mode_data_set_cmd_ok(struct window_choose_mode_data *modedata, char * cmd);
typedef void window_choose_callback(struct window_pane *wp, struct window_choose_mode_data *data, struct window_choose_data *wcd);
void tcl_window_choose_callback(struct window_pane *wp, struct window_choose_mode_data *data, struct window_choose_data *wcd)
{
  /* from window_choose_default_callback */
  if (wcd && wcd->start_client && wcd->start_client->flags & CLIENT_DEAD)
    return;

  if (wcd == NULL) {
    //Tcl_CmdInfo cmdInfo;
    //if (Tcl_GetCommandInfo(tcl_interp, "choose-from-list-cancel", &cmdInfo)) {
    //  global_enter(
    //      NULL, //wcd->start_client ? wcd->start_client->cmdq : NULL,
    //      NULL, //wcd->start_client,
    //      NULL, //wcd->start_session,
    //      wp->window,
    //      wp,
    //      NULL // wcd->wl
    //      );
    //  if (Tcl_Eval(tcl_interp, "choose-from-list-cancel") == TCL_ERROR) {
    //    log_debug("tcl error: %s", Tcl_GetStringResult(tcl_interp));
    //    tcl_cmdq_error(tcl_g->cmdq, "Error: %s", Tcl_GetStringResult(tcl_interp));
    //  }
    //  global_leave();
    //}
    return;
  }

  global_enter(
      wcd->start_client ? wcd->start_client->cmdq : NULL,
      wcd->start_client,
      wcd->start_session,
      wcd->wl ? wcd->wl->window : NULL,
      wcd->wl ? window_pane_at_index(wcd->wl->window, wcd->pane_id) : NULL,
      wcd->wl);

  Tcl_SetVar2(tcl_interp, "_", NULL, wcd->id_tag, TCL_GLOBAL_ONLY);

  if (wcd->command && Tcl_Eval(tcl_interp, wcd->command) == TCL_ERROR) {
    log_debug("tcl error: %s", Tcl_GetStringResult(tcl_interp));
    tcl_cmdq_error(tcl_g->cmdq, "Error: %s", Tcl_GetStringResult(tcl_interp));
  }

  char * cmd = window_choose_mode_data_get_cmd_ok(data);
  if (cmd && Tcl_Eval(tcl_interp, cmd) == TCL_ERROR) {
    log_debug("tcl error: %s", Tcl_GetStringResult(tcl_interp));
    tcl_cmdq_error(tcl_g->cmdq, "Error: %s", Tcl_GetStringResult(tcl_interp));
  }

  Tcl_CmdInfo cmdInfo;
  if (Tcl_GetCommandInfo(tcl_interp, "choose-from-list-ok", &cmdInfo) &&
      Tcl_Eval(tcl_interp, "choose-from-list-ok") == TCL_ERROR)
  {
    log_debug("tcl error: %s", Tcl_GetStringResult(tcl_interp));
    tcl_cmdq_error(tcl_g->cmdq, "Error: %s", Tcl_GetStringResult(tcl_interp));
  }

  //Tcl_UnsetVar2(tcl_interp, "_", NULL, TCL_GLOBAL_ONLY);

  global_leave();
}

ARRAY_DECL(array_pwindow_choose_data, struct window_choose_data *);

#define SHIFT_ARG(NAME)  \
      if ((++argi) >= argc) { \
        cmdq_error(tcl_g->cmdq, "choose-from-list " NAME ": argument required"); \
        Tcl_AddErrorInfo(tcl_interp, "choose-from-list " NAME ": argument required"); \
        goto error; \
      } else { arg = argv[argi]; }
int tcl_choose_from_list_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  struct {
    const char * id;
    const char * cmd;
    int selected;
  } item_options = {NULL,NULL,0};
  const char * choose_ok_cmd = NULL;
  const char * choose_cancel_cmd = NULL;
  u_int selected_idx = 0;
  const char * selected_tag;
  int doargs = 1;
  struct window_choose_data * cdata;

  struct array_pwindow_choose_data items = ARRAY_INITIALIZER;

  if (argc <= 1) goto usage;

  if (!tcl_g->c || !tcl_g->s || !tcl_g->wp) {
    tcl_error("choose-from-list: no client/session/windowpane");
    return TCL_ERROR;
  }

  int argi;
  for (argi = 1; argi < argc; argi++) {
    const char * arg = argv[argi];
    if (doargs && *arg == '-') {
      if (!strcmp(arg, "-id") || !strcmp(arg, "-tag")) {
        SHIFT_ARG("-id");
        item_options.id = arg;
        continue;
      } else if (!strcmp(arg, "-cmd")) {
        SHIFT_ARG("-cmd");
        item_options.cmd = arg;
        continue;
      } else if (!strcmp(arg, "-selected")) {
        item_options.selected = 1;
        continue;
      } else if (!strcmp(arg, "-selected-idx")) {
        SHIFT_ARG("-selected-idx");
        selected_idx = atoi(arg);
        continue;
      } else if (!strcmp(arg, "-selected-id") || !strcmp(arg, "-selected-tag")) {
        SHIFT_ARG("-selected-id");
        selected_tag = arg;
        continue;
      } else if (!strcmp(arg, "-onselect")) {
        SHIFT_ARG("-onselect");
        choose_ok_cmd = arg;
        continue;
      } else if (!strcmp(arg, "-oncancel")) {
        SHIFT_ARG("-oncancel");
        choose_cancel_cmd = arg;
        continue;
      } else if (!strcmp(arg, "-list")) {
        SHIFT_ARG("-list");
        int argc2;
        Tcl_Obj *argObj = Tcl_NewStringObj(arg, -1);
        Tcl_IncrRefCount(argObj);
        Tcl_Obj **argv2;
        if (Tcl_ListObjGetElements(tcl_interp, argObj, &argc2, &argv2) == TCL_ERROR) {
          goto error;
        }
        for (int argi2 = 0; argi2 < argc2; argi2++) {
          const char * arg2 = Tcl_GetString(argv2[argi2]);
          cdata = window_choose_data_create(TREE_OTHER, tcl_g->c, tcl_g->c->session);
          cdata->idx = ARRAY_LENGTH(&items);
          cdata->ft_template = xstrdup(arg2); // text
          cdata->id_tag = xstrdup(arg2);
          if (item_options.cmd) cdata->command = xstrdup(item_options.cmd);
          ARRAY_ADD(&items, cdata);
        }
        Tcl_DecrRefCount(argObj);
        memset(&item_options, 0, sizeof(item_options));
        continue;
      } else if (!strcmp(arg, "-val")) {
        SHIFT_ARG("-val");
        goto add_list_element;
      } else if (!strcmp(arg, "--")) {
        doargs = 0;
        continue;
      } else {
        cmdq_error(tcl_g->cmdq, "choose-from-list : unknown option %s", arg);
        tcl_error_q("choose-from-list : unknown option");
        goto error;
      }
    } else {
add_list_element:
      cdata = window_choose_data_create(TREE_OTHER, tcl_g->c, tcl_g->c->session);

      cdata->idx = ARRAY_LENGTH(&items);
      cdata->ft_template = xstrdup(arg); // text
      if (item_options.cmd) cdata->command = xstrdup(item_options.cmd);
      cdata->id_tag = xstrdup(item_options.id ? item_options.id : arg);
      if (item_options.selected) selected_idx = cdata->idx;

      ARRAY_ADD(&items, cdata);

      memset(&item_options, 0, sizeof(item_options));
    }
  }

  if (window_pane_set_mode(tcl_g->wp, &window_choose_mode) != 0) {
    tcl_error("choose-from-list : pane is already in mode");
    goto error;
  }
  if (choose_ok_cmd) {
    window_choose_mode_data_set_cmd_ok(tcl_g->wp->modedata, choose_ok_cmd);
  }

  for (u_int i = 0; i < ARRAY_LENGTH(&items); i++)  {
    cdata = ARRAY_ITEM(&items, i);
    window_choose_add(tcl_g->wp, cdata);
    if (selected_tag && !strcmp(selected_tag, cdata->id_tag)) {
      selected_idx = i; //cdata->idx; // should match but can be arbitrary
    }
  }

  window_choose_ready(tcl_g->wp, selected_idx, &tcl_window_choose_callback);

  return TCL_OK;

error:
  for (u_int i = 0; i < ARRAY_LENGTH(&items); i++)  {
    window_choose_data_free(ARRAY_ITEM(&items, i));
  }
  return TCL_ERROR;

usage:
  tcl_error(
      "Usage: choose-from-list {ITEM ...}\n"
      " Switch into choose-mode, make a choice from the list of items\n"
      " if an item starts with '-', this is an option for the item that follows:\n"
      "  -val : the following element IS the string to display\n"
      "  -id | -tag : identifier for the item\n"
      "  -cmd: script to execute when this item is selected\n"
      "  -selected: this item is initially selected\n"
      "  -selected-idx: specify the item index selected\n"
      "  -selected-id | -selected-tag: specify tag selected\n"
      "  -onselect: script to execute when the choose-mode ends with a selection\n"
      "  -oncancel: script to execute when the user cancels choose-mode\n"
      "  -list: add this list of elements\n"
      "  --   : no more options\n"
      " Press ^B ~ for full usage message");
  return TCL_ERROR;
}
#undef SHIFT_ARG


int tcl_status_msg_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  if (argc != 2) goto error;
  status_message_set(tcl_g->c, "%s", argv[1]);
  return TCL_OK;

error:
  tcl_error(
      " Usage: status-msg MESSAGE\n"
      " Usage: status-msg-clear\n"
      );
  return TCL_ERROR;
}
int tcl_status_msg_clear_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  if (argc != 1) goto error;
  status_message_clear(tcl_g->c);
  return TCL_OK;

error:
  tcl_error(
      " Usage: status-msg MESSAGE\n"
      " Usage: status-msg-clear\n"
      );
  return TCL_ERROR;
}


extern const struct mode_key_cmdstr mode_key_cmdstr_copy[];
extern const struct mode_key_cmdstr mode_key_cmdstr_choice[];
extern const struct mode_key_cmdstr mode_key_cmdstr_edit[];
typedef void mode_key_handle_t(struct window_pane *, struct client *, struct session *, key_code, struct mouse_event *);
void window_copy_key(struct window_pane *wp, struct client *c, struct session *sess, key_code key, struct mouse_event *m);
void window_choose_key(struct window_pane *wp, struct client *c, struct session *sess, key_code key, struct mouse_event *m);

static const char MODENAME_CHOICE[] = {"choice"};
static const char MODENAME_COPY[] = {"copy"};
struct mode_command_decr {
  char *modeName;
  mode_key_handle_t *mode_key_fn;
  enum mode_key_cmd cmd;
  const char * cmd_name;
};

int tcl_modecmd_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  struct mode_command_decr *mcd = (struct mode_command_decr*)clientData;

  struct client         *c = tcl_g->c;
  struct session        *s = tcl_g->s;
  //struct window         *w = tcl_g->w;
  struct window_pane    *wp = tcl_g->wp;

  enum mode_key_cmd cmd = MODEKEY_NONE;

  if (s == NULL || (c->flags & (CLIENT_DEAD|CLIENT_SUSPENDED)) != 0) {
    tcl_error_q("mode command: no session or dead/suspended");
    return TCL_ERROR;
  }
  if (!wp) {
    tcl_error_q("mode command: no window pane");
    return TCL_ERROR;
  }

  if (wp->screen == &wp->base) {
    tcl_error_q("mode command: pane not in 'mode'");
    return TCL_ERROR;
  }

  // TODO:: check for edit mode: c->prompt_string != NULL (grep status_prompt_key)
  // TODO:: set/clead edit mode: status_prompt_set status_prompt_clear
  if (mcd->modeName) {
    /* known mode: ensure the mode */
    if (!strcmp(mcd->modeName, MODENAME_CHOICE)) {
      if (wp->mode != &window_choose_mode) {
        tcl_error_q("choose mode command: mode is not 'choose'");
        return TCL_ERROR;
      }
      if (wp->mode->key != window_choose_key) {
        tcl_error_q("choose mode command: mode is 'choose' but key handler is weird");
        return TCL_ERROR;
      }

      cmd = mcd->cmd;
    } else if (!strcmp(mcd->modeName, MODENAME_COPY)) {
      if (wp->mode != &window_copy_mode) {
        tcl_error_q("copy mode command: mode is not 'copy'");
        return TCL_ERROR;
      }
      if (wp->mode->key != window_copy_key) {
        tcl_error_q("copy mode command: mode is 'copy' but key handler is weird");
        return TCL_ERROR;
      }

      cmd = mcd->cmd;
    } else {
      tcl_error_q("mode command: unknown mode");
      return TCL_ERROR;
    }
  } else {
    /* auto-detect mode */
    struct mode_key_cmdstr *cmdstr;
    if (wp->mode == &window_choose_mode) {
      if (wp->mode->key != window_choose_key) {
        tcl_error_q("auto mode command: mode is 'choose' but key handler is weird");
        return TCL_ERROR;
      }
      cmdstr = mode_key_cmdstr_choice;
    } else if (wp->mode == &window_copy_mode) {
      if (wp->mode->key != window_copy_key) {
        tcl_error_q("auto mode command: mode is 'copy' but key handler is weird");
        return TCL_ERROR;
      }
      cmdstr = mode_key_cmdstr_copy;
    } else {
      tcl_error_q("auto mode command: unknown mode");
      return TCL_ERROR;
    }
    cmd = mode_key_fromstring(cmdstr, mcd->cmd_name);
  }

  struct mouse_event me = {};
  wp->mode->key(wp, c, s, cmd | KEYC_DISPATCH, &me);

  return TCL_OK;


  // execute 'mode' command(s):
  // mode [MODE = edit|choice|copy] [COMMAND ...]
  // e.g.
  //   mode copy {cancel previous-space begin-selection next-space-end}
  // Note that some commands lead to mode switch -- like goto-line -> edit


  // TODO::
  // 1. Import 'mode' commands in mode-key.c into their respective namespaces:
  //    ::tmux::mode::copy::cancel
  //    The command would check the pane is in THIS mode.
  // 2. Import commands with automatic mode detect/dispatch:
  //    ::tmux::mode::cancel would send 'cancel' to the current mode via mode_key_fromstring
  // 3. Import commands that don't conflict with the existing ones to ::tmux
  // 4. Import commands that don't conflict with the existing ones to :*
  // 5. Import commands that don't conflict with the existing ones to global

  // mode::copy::{set|get}-input - get/set 'mode' user input: data->inputstr, see window-copy.c
  // function to get/set data->numprefix for the 'mode' command


  // don't call window_copy_key directly
  // try to use the current pane's mode structures
  // to look up the string->commandId
  // and wp->mode->key to dispatch the command
}

// TODO:: temporary key-table to temporrary modify an existing one (until mode exit)
// TODO:: -OR- key-table inheritance:
//              add 'inherit' table name to 'struct key_table'
//              add inheritance loop to server_client_handle_key/RB_FIND keytable
//              add 'inherit' table name to 'struct mode_key_data'
//              process inheritance for mode keys in mode_key_lookup
// TODO:: -OR- introduce 'soft' keytables:
//              all keypresses are forwarded to a script
//              func to 'get-key-action {TABLE} {KEY}' to forward the action
//              can be done via existing keytables with 'fallback' command

// TODO:: callback on window_pane_set_mode and window_pane_reset_mode

// TODO:: callback on every keypress(?)

// TODO:: function to operate with screen buffer: read and write

// TODO:: functions to set/get 'current' objects to operate on:
//  Client
//  Session
//  Window
//  Pane
// ( see cmd_prepare_state* and cmd_find_* )
// see server_client_handle_key():
//  client -> session :
//      c->session
//  session -> window :
//	if (s == NULL || (c->flags & (CLIENT_DEAD|CLIENT_SUSPENDED)) != 0)
//		return;
//	w = s->curw->window;
//  window -> windowpane:
//      wp = w->active;

// TODO:: in command prompt accept multi-line tcl commands: Tcl_CommandComplete cmd-command-prompt.c

// TODO:: <Tab> expansion for tcl commands in tcl mode

// TODO:: TCL interface to window-choose mode: choose from list; choose from list of [tag, val]

// TODO:: functions (for copy-mode?): get-line(index), get-cursor-[xy], get-screen-[xy], get-line(y), get-char(x,y), (?)get-char-under-cursor

void /*Tcl_Command*/ tcl_create_command_override(Tcl_Interp *interp,
				const char *cmdName, Tcl_CmdProc *proc,
				ClientData clientData,
				Tcl_CmdDeleteProc *deleteProc)
{
  Tcl_Command tcl_cmd = Tcl_CreateCommand(tcl_interp, cmdName, proc, clientData, deleteProc);
  log_debug("Tcl_CreateCommand %s = %p", cmdName, tcl_cmd);
  //return tcl_cmd;
}

void /*Tcl_Command*/ tcl_create_command_nooverride(Tcl_Interp *interp,
				const char *cmdName, Tcl_CmdProc *proc,
				ClientData clientData,
				Tcl_CmdDeleteProc *deleteProc)
{
  Tcl_CmdInfo cmdInfo;
  if (!Tcl_GetCommandInfo(interp, cmdName, &cmdInfo)) {
    Tcl_Command tcl_cmd = Tcl_CreateCommand(interp, cmdName, proc, clientData, deleteProc);
    log_debug("Tcl_CreateCommand %s = %p", cmdName, tcl_cmd);
    //return tcl_cmd;
  } else {
    log_debug("Tcl_CreateCommand %s not created due to name clash!", cmdName);
    //return NULL;
  }
}

void tcl_create_mode_command(char *modeName, mode_key_handle_t mode_key_fn, enum mode_key_cmd cmd, const char * cmd_name)
{
  struct mode_command_decr * mcd;
  char cmdName2[100];

  // create ::tmux::command
  mcd = xcalloc(1, sizeof(*mcd));
  mcd->modeName = modeName;
  mcd->mode_key_fn = mode_key_fn;
  mcd->cmd = cmd;
  mcd->cmd_name = cmd_name;

  snprintf(cmdName2, 100, "::tmux::mode::%s::%s", modeName, cmd_name);
  tcl_create_command_override(tcl_interp, cmdName2, tcl_modecmd_proc, (ClientData)mcd, NULL);

  // create auto mode commands
  mcd = xcalloc(1, sizeof(*mcd));
  mcd->modeName = NULL;
  mcd->mode_key_fn = NULL;
  mcd->cmd = 0;
  mcd->cmd_name = cmd_name;

  snprintf(cmdName2, 100, "::tmux::mode::%s", cmd_name);
  tcl_create_command_override(tcl_interp, cmdName2, tcl_modecmd_proc, (ClientData)mcd, NULL);

  snprintf(cmdName2, 100, "::tmux::%s", cmd_name);
  tcl_create_command_nooverride(tcl_interp, cmdName2, tcl_modecmd_proc, (ClientData)mcd, NULL);

  snprintf(cmdName2, 100, "%s", cmd_name);
  tcl_create_command_nooverride(tcl_interp, cmdName2, tcl_modecmd_proc, (ClientData)mcd, NULL);
}

void tcl_create_mode_commands()
{
  struct {
    char * n;
    struct mode_key_cmdstr *m;
    mode_key_handle_t *key1;
    mode_key_handle_t *key2;
  } modes[] = {
//    {"edit",   mode_key_cmdstr_edit   },  // defined by c->prompt_string != NULL, see server_client_handle_key()
    {MODENAME_CHOICE, mode_key_cmdstr_choice, window_choose_mode.key, &window_choose_key },
    {MODENAME_COPY,   mode_key_cmdstr_copy,   window_copy_mode.key,   &window_copy_key   },
  }, *mode;

  mode = modes;
  for (size_t i = 0; i < sizeof(modes)/sizeof(*modes); mode++,i++) {
    assert(mode->key1 == mode->key2); // ?
    for (struct mode_key_cmdstr *mode_cmds = mode->m; mode_cmds->name; mode_cmds++) {
      tcl_create_mode_command(mode->n, mode->key1, mode_cmds->cmd, mode_cmds->name);
    }
  }
}


// TODO:: make ':' and root versions of tmux commands via 'interp alias'
void tcl_create_command_and_aliases(
    Tcl_Interp *interp,
    const char *cmdName1, Tcl_CmdProc *proc,
    ClientData clientData,
    Tcl_CmdDeleteProc *deleteProc)
{
  Tcl_Command tcl_cmd;
  char cmdName2[100] = "::tmux::";

  strcpy(cmdName2+8, cmdName1);

  // create ::tmux::command
  tcl_cmd = Tcl_CreateCommand(tcl_interp, cmdName2, proc, clientData, deleteProc);
  log_debug("Tcl_CreateCommand %s -> %s = %p", cmdName1, cmdName2, tcl_cmd);
  // create :command
  tcl_cmd = Tcl_CreateCommand(tcl_interp, cmdName2+7, proc, clientData, deleteProc);
  log_debug("Tcl_CreateCommand %s -> %s = %p", cmdName1, cmdName2+7, tcl_cmd);

  // ::command
  Tcl_CmdInfo cmdInfo;
  if (!Tcl_GetCommandInfo(interp, cmdName2+6, &cmdInfo)) {
    tcl_cmd = Tcl_CreateCommand(tcl_interp, cmdName2+6, proc, clientData, deleteProc);
    log_debug("Tcl_CreateCommand %s -> %s = %p", cmdName1, cmdName2+6, tcl_cmd);
  } else {
    log_debug("Tcl_CreateCommand %s -> %s not created due to name clash!", cmdName1, cmdName2+6);
  }
}

// TODO: send-keys-global -> server_client_handle_key(...)


void tcl_init(int argc, char **argv)
{
  Tcl_FindExecutable(NULL /*argv[0]*/);
  tcl_interp = Tcl_CreateInterp();
  if (tcl_interp == NULL) {
    log_debug("Could not create interpreter!\n");
    return;
  }
  // Tcl_InitStubs(interp, "8.*", 0) ?

  // TODO:: Tcl_Finalize() at the end?

  /* Global all-null context */
  global_enter_q(NULL);

  for (const struct cmd_entry **pcmd_e = cmd_table; *pcmd_e; pcmd_e++) {
    const struct cmd_entry *cmd_e = *pcmd_e;
    if (cmd_e == &cmd_tcl_entry) continue;

    if (cmd_e->name) {
      tcl_create_command_and_aliases(tcl_interp, cmd_e->name, &tcl2tmux_call, (ClientData)cmd_e, NULL);
    }

    if (cmd_e->alias) {
      tcl_create_command_and_aliases(tcl_interp, cmd_e->alias, &tcl2tmux_call, (ClientData)cmd_e, NULL);
    }
  }

  Tcl_Eval(tcl_interp, "proc tmux {args} { namespace eval ::tmux {*}$args }");

  /* prevent from adding these from other namespaces */
  Tcl_Eval(tcl_interp, "interp alias {} tcl {} eval");
  Tcl_Eval(tcl_interp, "interp alias {} t {} eval");
  Tcl_Eval(tcl_interp, "interp alias {} :tcl {} eval");
  Tcl_Eval(tcl_interp, "interp alias {} :t {} eval");

  Tcl_Eval(tcl_interp, "proc read_file {fname} { set fd [open $fname r]; set ret [read $fd]; close $fd; return $ret; }");
  Tcl_Eval(tcl_interp, "proc write_file {fname, txt} { set fd [open $fname w]; put -nonewline $fd $txt; close $fd; }");

  Tcl_Eval(tcl_interp, "proc shell-quote {s} { return \"'[string map {' '\"'\"' \\\\ \\\\\\\\} $s]'\" }");

  tcl_create_command_nooverride(tcl_interp, "format", &tcl_format_proc,
      (ClientData) 0, NULL ) ;
  tcl_create_command_override(tcl_interp, "f", &tcl_format_proc,
      (ClientData) 0, NULL ) ;
  tcl_create_command_nooverride(tcl_interp, "format-time", &tcl_format_proc,
      (ClientData) 1, NULL ) ;
  tcl_create_command_override(tcl_interp, "ft", &tcl_format_proc,
      (ClientData) 1, NULL ) ;

  tcl_create_command_nooverride(tcl_interp, "parse", &tcl_tmuxparse_proc,
      (ClientData) 0, NULL ) ;
  tcl_create_command_nooverride(tcl_interp, "parse2script", &tcl_tmuxparse_proc,
      (ClientData) 1, NULL ) ;
  Tcl_Eval(tcl_interp, "proc         parse2eval {str} { return [list namespace eval ::tmux [:parse2script $str]] }");
  Tcl_Eval(tcl_interp, "proc         parse_exec {str} { namespace eval ::tmux [:parse2script $str] }");

  tcl_create_command_override(tcl_interp, "_output-divert", &tcl_outputdivert_proc,
      (ClientData) 0, NULL ) ;
  Tcl_Eval(tcl_interp, "proc output-of-txt {code} { :_output-divert start txt; uplevel $code; return [:_output-divert end]; }");
  Tcl_Eval(tcl_interp, "proc output-of-list {code} { :_output-divert start list; uplevel $code; return [:_output-divert end]; }");

  tcl_create_command_override(tcl_interp, "pbcopy", &tcl_pbcopy_proc,
      (ClientData) 0, NULL ) ;
  tcl_create_command_override(tcl_interp, "pbpaste", &tcl_pbpaste_proc,
      (ClientData) 0, NULL ) ;
  tcl_create_command_override(tcl_interp, "pbcontent", &tcl_pbcontent_proc,
      (ClientData) 0, NULL ) ;
  tcl_create_command_override(tcl_interp, "pblist", &tcl_pblist_proc,
      (ClientData) 0, NULL ) ;

  tcl_create_command_override(tcl_interp, "copy-mode-selection", &tcl_copymodeselection_proc,
      (ClientData) 0, NULL ) ;

  tcl_create_command_override(tcl_interp, "print", &tcl_print_proc,
      (ClientData) 0, NULL ) ;

  tcl_create_command_override(tcl_interp, "nop", &tcl_nop_proc,
      (ClientData) 0, NULL ) ;

  tcl_create_command_override(tcl_interp, "using", &tcl_using_context_proc,
      (ClientData) 0, NULL ) ;

  tcl_create_command_override(tcl_interp, "status-msg", &tcl_status_msg_proc,
      (ClientData) 0, NULL ) ;
  tcl_create_command_override(tcl_interp, "status-msg-clear", &tcl_status_msg_clear_proc,
      (ClientData) 0, NULL ) ;

  tcl_create_command_override(tcl_interp, "choose-from-list", &tcl_choose_from_list_proc,
      (ClientData) 0, NULL ) ;

  tcl_create_mode_commands();

  log_debug("tcl init ok");
  return;

cleanup:
  log_debug("tcl init failed");
  tcl_interp = NULL;
}

#pragma clang diagnostic pop

#define TCL_INTERP_CHECKINIT(ERROR_CODE) \
  if (tcl_interp == NULL) { \
    log_debug("tcl init..."); \
    tcl_init(0, NULL); \
    if (tcl_interp == NULL) { \
      return tcl_g->cmd_retval = (ERROR_CODE); \
    } \
    log_debug("tcl init ok"); \
  } else {} \
// macro end


int Tcl_EvalObjv(Tcl_Interp *interp,
    int objc, Tcl_Obj *const objv[],
    int flags);

Tcl_DString * dstring_from_args(int argc, char ** argv)
{
  fatal("%s:%d: function %s not implemented", __FILE__, __LINE__, __func__);
  return 0;
}

Tcl_Obj ** tclobjlist_from_args(int argc, char ** argv)
{
  Tcl_Obj ** ret = xcalloc(argc+1, sizeof(*ret));
  for (int i = 0; i < argc; i++) {
    Tcl_IncrRefCount(ret[i] = Tcl_NewStringObj(argv[i], -1));
  }
  ret[argc] = NULL;
  return ret;
}

void tclobjv_free(Tcl_Obj **objv) {
  for (Tcl_Obj **o = objv; *o; o++) {
    Tcl_DecrRefCount(*o);
  }
  free(objv);
}

int tcl_eval_args(Tcl_Interp *interp, int argc, char **argv)
{
  if (argc == 0) return TCL_OK;
  if (argc == 1) return Tcl_Eval(interp, argv[0]);

  Tcl_Obj **objv = tclobjlist_from_args(argc, argv);
  int ret = Tcl_EvalObjv(interp, argc, objv, 0);
  tclobjv_free(objv);

  return ret;
}

// eval_in_ns_and_convert_args ***
int tcl_eval_args_in_namespace(Tcl_Interp *interp, const char *ns, int argc, char **argv)
{
  // make evalobjv: "namespace" "eval" ns *argv
  return 0;
}

enum cmd_retval
cmd_tcl_exec(struct cmd *self, struct cmd_q *cmdq)
{
  TCL_INTERP_CHECKINIT(CMD_RETURN_ERROR);

  global_enter_q(cmdq);

  struct args		*args = self->args;
  //struct client		*c = cmdq->state.c;
  //struct session	*s = cmdq->state.tflag.s;
  //struct winlink	*wl = cmdq->state.tflag.wl;
  //struct window_pane	*wp = cmdq->state.tflag.wp;

  //log_debug("%s:%d s=%p c=%p wl=%p wp=%p", __FILE__, __LINE__, s, c, wl, wp);
  log_debug("%s:%d args[%d]", __FILE__, __LINE__, args->argc);

  if (args->argc == 0) return CMD_RETURN_NORMAL;
  for (int i=0; i < args->argc; i++) {
    log_debug(" tcl arg[%d]: <%s>", i, args->argv[i]);
  }

  if (tcl_eval_args(tcl_interp, args->argc, args->argv) == TCL_ERROR) {
    log_debug("tcl error: %s", Tcl_GetStringResult(tcl_interp));
    tcl_cmdq_error(cmdq, "Error: %s", Tcl_GetStringResult(tcl_interp));

    global_leave();
    return CMD_RETURN_ERROR;
  }

  log_debug("tcl ok: \"%s\"", Tcl_GetStringResult(tcl_interp));
  if (*Tcl_GetStringResult(tcl_interp)) {
    cmdq_print(cmdq, "%s", Tcl_GetStringResult(tcl_interp));
    status_message_set(tcl_g->c, "%s", Tcl_GetStringResult(tcl_interp));
  }

  global_leave();
  return CMD_RETURN_NORMAL;
}

int tcl_eval_client(const char *tcl_str,
    struct client *client, struct session *session, struct window_pane *wp)
{
  TCL_INTERP_CHECKINIT(CMD_RETURN_ERROR);

  global_enter(NULL, client, session, NULL, wp, NULL);

  struct cmd *cmd = xcalloc(1, sizeof *cmd);
  cmd->entry = &cmd_tcl_entry;
  const char *argv[] = {"tcl", tcl_str, NULL};
  cmd->args = args_parse(cmd->entry->args.template, 2, (char**)argv);

  int ret = cmd_tcl_exec(cmd, client->cmdq);

  // cmd_free: from cmd_list_free()
  args_free(cmd->args);
  free(cmd->file);
  free(cmd);

  global_leave();
  return ret;
}

int tcl_eval_cmdq(const char *tcl_str, struct cmd_q *cmdq)
{
  TCL_INTERP_CHECKINIT(CMD_RETURN_ERROR);

  global_enter_q(cmdq);

  struct cmd *cmd = xcalloc(1, sizeof *cmd);
  cmd->entry = &cmd_tcl_entry;
  const char *argv[] = {"tcl", tcl_str, NULL};
  cmd->args = args_parse(cmd->entry->args.template, 2, (char**)argv);

  int ret = cmd_tcl_exec(cmd, cmdq);

  // cmd_free: from cmd_list_free()
  args_free(cmd->args);
  free(cmd->file);
  free(cmd);

  global_leave();
  return ret;
}

int tcl_load_config(const char *fname, struct cmd_q *cmdq)
{
  TCL_INTERP_CHECKINIT(CMD_RETURN_ERROR);

  int ret = 0;
  global_enter_q(cmdq);
  if (Tcl_EvalFile(tcl_interp, fname) == TCL_ERROR) {
    cfg_add_cause("%s: %s", fname, Tcl_GetStringResult(tcl_interp));
    ret = -1;
  }
  global_leave();

  return ret;
}


