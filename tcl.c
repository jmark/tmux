#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tcl.h>

#include "tmux.h"

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

//int impl_command1(
//    ClientData clientData,
//    Tcl_Interp *interp,
//    int objc,
//    Tcl_Obj * const * objv)
//{
//  printf("tcl call: cmd1\n");
//  // Tcl_SetObjResult(interp, obj)
//  Tcl_Eval(tcl_interp, "puts qwe");
//  Tcl_Eval(tcl_interp, stacktrace);
//  Tcl_Eval(tcl_interp, "puts [stacktrace]");
//  Tcl_Eval(tcl_interp, "puts asd");
//  return TCL_OK;
//}

struct tcl_global {
  struct cmd_q *cmdq;

  struct window_pane *wp;
  struct client *c;
  struct session *s;
  struct winlink *wl;

  enum cmd_retval cmd_retval;
} global = {
  .cmdq = NULL,
  .wp = NULL,
  .c = NULL,
  .s = NULL,
  .wl = NULL,
  .cmd_retval = CMD_RETURN_NORMAL
};

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

void set_global_cmdq(struct cmd_q *cmdq)
{
  if (global.cmdq == cmdq) return;
  if (global.cmdq) cmdq_free(global.cmdq);
  global.cmdq = cmdq;
  if (global.cmdq) global.cmdq->references++;
}

/*
int setglobal_from_cmdq(struct cmd_q *cmdq)
{
  set_global_cmdq(cmdq);

  struct cmd cmd;
  memset(&cmd, 0, sizeof(cmd));

  cmd.entry = &cmd_attach_session_entry;
  char *aa[] = {"", NULL};
  cmd.args = args_parse(cmd.entry->args.template, 1, aa);

  if (cmd_prepare_state(&cmd, global.cmdq, NULL) != 0) {
    args_free(cmd.args);
    return 0;
  }

  global.s = global.cmdq->state.tflag.s;
  global.c = global.cmdq->client;
  global.wl = global.cmdq->state.tflag.wl;
  global.wp = global.cmdq->state.tflag.wp;

  args_free(cmd.args);
  return 1;
}

int setglobal_from_all(struct client *c, struct session *s,
    struct winlink *wl, struct window_pane *wp)
{
  // from format_defaults
  if (s == NULL && c != NULL)
    s = c->session;
  if (wl == NULL && s != NULL)
    wl = s->curw;
  if (wp == NULL && wl != NULL)
    wp = wl->window->active;

  set_global_cmdq(NULL);
  global.wp = wp;
  global.c = c;
  global.s = s;
  global.wl = wl;
}
*/

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
    cmdq_error(global.cmdq, "tmux::usage: %s %s", c.entry->name, c.entry->usage);
    Tcl_AddErrorInfo(tcl_interp, "tmux wrong number of args");
    Tcl_SetErrorCode(tcl_interp, "tmux wrong number of args", NULL);
    Tcl_SetResult(tcl_interp, "tmux wrong number of args", NULL);
    global.cmd_retval = CMD_RETURN_ERROR;
    return TCL_ERROR;
  }

  struct cmd *cmd_old = global.cmdq->cmd;
  global.cmdq->cmd = &c;

  if (cmd_prepare_state(&c, global.cmdq, NULL) != 0) {
    global.cmd_retval = CMD_RETURN_ERROR;
  } else {
    global.cmd_retval = (*c.entry->exec)(&c, global.cmdq);
  }
  global.cmdq->cmd = cmd_old;

  args_free(c.args);
  //free(c.args);

  return global.cmd_retval == CMD_RETURN_NORMAL ? TCL_OK : TCL_ERROR;
}


int tcl2tmux_call2(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  global.cmd_retval = CMD_RETURN_NORMAL;

  struct cmd *cmd = xcalloc(1, sizeof *cmd);
  cmd->entry = (struct cmd_entry *)clientData;
  cmd->args = args_parse(cmd->entry->args.template, argc, argv);

  // do some job of cmd_parse
  if ((cmd->args == NULL) ||
      (cmd->entry->args.lower != -1 && cmd->args->argc < cmd->entry->args.lower) ||
      (cmd->entry->args.upper != -1 && cmd->args->argc > cmd->entry->args.upper))
  {
    free(cmd);
    cmdq_error(global.cmdq, "tmux::usage: %s %s", cmd->entry->name, cmd->entry->usage);
    Tcl_AddErrorInfo(tcl_interp, "tmux wrong number of args");
    Tcl_SetErrorCode(tcl_interp, "tmux wrong number of args", NULL);
    Tcl_SetResult(tcl_interp, "tmux wrong number of args", NULL);
    global.cmd_retval = CMD_RETURN_ERROR;
    return TCL_ERROR;
  }

  struct cmd_list *cmdlist = xcalloc(1, sizeof *cmdlist);
  cmdlist->references = 1;
  TAILQ_INIT(&cmdlist->list);

  struct cmd_q *cmdq = cmdq_new(global.cmdq->client);
  cmdq->parent = global.cmdq;

  TAILQ_INSERT_TAIL(&cmdlist->list, cmd, qentry);

  cmdq_run(cmdq, cmdlist, NULL);

  cmd_list_free(cmdlist);
  cmdq_free(cmdq);

  return global.cmd_retval == CMD_RETURN_NORMAL ? TCL_OK : TCL_ERROR;
}


void tcl_error(const char * str)
{
    cmdq_error(global.cmdq, "%s", str);
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
  global.cmd_retval = CMD_RETURN_NORMAL;
  if (argc != 2) {
    tcl_error("Usage: format string | format-time string");
    global.cmd_retval = CMD_RETURN_ERROR;
    return TCL_ERROR;
  }

  struct cmd cmd;
  memset(&cmd, 0, sizeof(cmd));

  cmd.entry = &cmd_attach_session_entry;
  char *aa[] = {"", NULL};
  cmd.args = args_parse(cmd.entry->args.template, 1, aa);

  if (cmd_prepare_state(&cmd, global.cmdq, NULL) != 0) {
    tcl_error("format: prepare_state fail");
    global.cmd_retval = CMD_RETURN_ERROR;
  } else {
    struct session	*s = global.cmdq->state.tflag.s;
    struct client	*c = global.cmdq->client;
    struct winlink	*wl = global.cmdq->state.tflag.wl;
    struct window_pane	*wp = global.cmdq->state.tflag.wp;
    struct format_tree	*ft;

    ft = format_create(global.cmdq, 0);
    format_defaults(ft, c, s, wl, wp);
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
  }

  args_free(cmd.args);

  return global.cmd_retval == CMD_RETURN_NORMAL ? TCL_OK : TCL_ERROR;
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
      );
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
        "Usage: pbcopy string\n"
        " Put the string into the paste buffer");
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
        "Usage: pbcontent\n"
        " Return the current paste buffer content");
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
  struct window_pane *wp = global.wp;
  if (!wp || wp->mode != &window_copy_mode) {
    Tcl_SetResult(interp, "", NULL);
    return TCL_OK;
  }

  size_t len;
  char * buf = window_copy_get_selection(wp, &len);
  Tcl_SetResult(interp, buf, (Tcl_FreeProc*)free);

  return TCL_OK;
}

int tcl_print_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  for (int i=1; i < argc; i++) {
    cmdq_print(global.cmdq, "%s", argv[i]);
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

void tcl_init(int argc, char **argv)
{
  Tcl_FindExecutable(NULL /*argv[0]*/);
  tcl_interp = Tcl_CreateInterp();
  if (tcl_interp == NULL) {
    log_debug("Could not create interpreter!\n");
    return;
  }
  // Tcl_InitStubs(interp, "8.*", 0) ?

  //Tcl_CreateObjCommand( tcl_interp, "cmd1", &impl_command1,
  //    (ClientData) NULL, NULL ) ;

  //Tcl_Eval(tcl_interp, "puts 1");
  //Tcl_Eval(tcl_interp, "cmd1");

  //Tcl_Finalize();

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

  Tcl_Eval(tcl_interp, "proc read_file {fname} { set fd [open $fname r]; set ret [read $fd]; close $fd; return $ret; }");
  Tcl_Eval(tcl_interp, "proc write_file {fname, txt} { set fd [open $fname w]; put -nonewline $fd $txt; close $fd; }");

  tcl_create_command_and_aliases(tcl_interp, "format", &tcl_format_proc,
      (ClientData) 0, NULL ) ;
  tcl_create_command_and_aliases(tcl_interp, "f", &tcl_format_proc,
      (ClientData) 0, NULL ) ;
  tcl_create_command_and_aliases(tcl_interp, "format-time", &tcl_format_proc,
      (ClientData) 1, NULL ) ;
  tcl_create_command_and_aliases(tcl_interp, "ft", &tcl_format_proc,
      (ClientData) 1, NULL ) ;

  tcl_create_command_and_aliases(tcl_interp, "parse", &tcl_tmuxparse_proc,
      (ClientData) 0, NULL ) ;
  tcl_create_command_and_aliases(tcl_interp, "parse2script", &tcl_tmuxparse_proc,
      (ClientData) 1, NULL ) ;
  Tcl_Eval(tcl_interp, "proc         parse2eval {str} { return [list namespace eval ::tmux [:parse2script $str]] }");
  Tcl_Eval(tcl_interp, "proc        :parse2eval {str} { return [list namespace eval ::tmux [:parse2script $str]] }");
  Tcl_Eval(tcl_interp, "proc         parse_exec {str} { namespace eval ::tmux [:parse2script $str] }");
  Tcl_Eval(tcl_interp, "proc        :parse_exec {str} { namespace eval ::tmux [:parse2script $str] }");

  tcl_create_command_and_aliases(tcl_interp, "_output-divert", &tcl_outputdivert_proc,
      (ClientData) 0, NULL ) ;
  Tcl_Eval(tcl_interp, "proc output-of-txt {code} { :_output-divert start txt; uplevel $code; return [:_output-divert end]; }");
  Tcl_Eval(tcl_interp, "proc output-of-list {code} { :_output-divert start list; uplevel $code; return [:_output-divert end]; }");

  tcl_create_command_and_aliases(tcl_interp, "pbcopy", &tcl_pbcopy_proc,
      (ClientData) 0, NULL ) ;
  tcl_create_command_and_aliases(tcl_interp, "pbpaste", &tcl_pbpaste_proc,
      (ClientData) 0, NULL ) ;
  tcl_create_command_and_aliases(tcl_interp, "pbcontent", &tcl_pbcontent_proc,
      (ClientData) 0, NULL ) ;
  tcl_create_command_and_aliases(tcl_interp, "pblist", &tcl_pblist_proc,
      (ClientData) 0, NULL ) ;

  tcl_create_command_and_aliases(tcl_interp, "copy-mode-selection", &tcl_copymodeselection_proc,
      (ClientData) 0, NULL ) ;

  tcl_create_command_and_aliases(tcl_interp, "print", &tcl_print_proc,
      (ClientData) 0, NULL ) ;

  tcl_create_command_and_aliases(tcl_interp, "nop", &tcl_nop_proc,
      (ClientData) 0, NULL ) ;

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
      return global.cmd_retval = (ERROR_CODE); \
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
  struct args		*args = self->args;
  struct client		*c = cmdq->state.c;
  //struct session	*s = cmdq->state.tflag.s;
  //struct winlink	*wl = cmdq->state.tflag.wl;
  //struct window_pane	*wp = cmdq->state.tflag.wp;

  set_global_cmdq(cmdq);

  //log_debug("%s:%d s=%p c=%p wl=%p wp=%p", __FILE__, __LINE__, s, c, wl, wp);
  log_debug("%s:%d args[%d]", __FILE__, __LINE__, args->argc);

  if (args->argc == 0) return CMD_RETURN_NORMAL;
  for (int i=0; i < args->argc; i++) {
    log_debug(" tcl arg[%d]: <%s>", i, args->argv[i]);
  }

  TCL_INTERP_CHECKINIT(CMD_RETURN_ERROR);

  if (tcl_eval_args(tcl_interp, args->argc, args->argv) == TCL_ERROR) {
    log_debug("tcl error: %s", Tcl_GetStringResult(tcl_interp));
    cmdq_error(cmdq, "Error: %s", Tcl_GetStringResult(tcl_interp));
    return CMD_RETURN_ERROR;
  }

  log_debug("tcl ok: \"%s\"", Tcl_GetStringResult(tcl_interp));
  if (*Tcl_GetStringResult(tcl_interp)) {
    cmdq_print(cmdq, "%s", Tcl_GetStringResult(tcl_interp));
    status_message_set(c, "%s", Tcl_GetStringResult(tcl_interp));
  }

  return CMD_RETURN_NORMAL;
}

int tcl_eval_client(const char *tcl_str,
    struct client *client, struct session *session, struct window_pane *wp)
{
  TCL_INTERP_CHECKINIT(CMD_RETURN_ERROR);

  global.cmd_retval = CMD_RETURN_NORMAL;
  global.s = session;
  global.wp = wp;

  struct cmd *cmd = xcalloc(1, sizeof *cmd);
  cmd->entry = &cmd_tcl_entry;
  const char *argv[] = {"tcl", tcl_str, NULL};
  cmd->args = args_parse(cmd->entry->args.template, 2, (char**)argv);

  int ret = cmd_tcl_exec(cmd, client->cmdq);

  // cmd_free: from cmd_list_free()
  args_free(cmd->args);
  free(cmd->file);
  free(cmd);

  global.wp = NULL;
  global.s = NULL;

  return ret;
}

int tcl_eval_cmdq(const char *tcl_str, struct cmd_q *cmdq)
{
  TCL_INTERP_CHECKINIT(CMD_RETURN_ERROR);

  global.cmd_retval = CMD_RETURN_NORMAL;

  struct cmd *cmd = xcalloc(1, sizeof *cmd);
  cmd->entry = &cmd_tcl_entry;
  const char *argv[] = {"tcl", tcl_str, NULL};
  cmd->args = args_parse(cmd->entry->args.template, 2, (char**)argv);

  int ret = cmd_tcl_exec(cmd, cmdq);

  // cmd_free: from cmd_list_free()
  args_free(cmd->args);
  free(cmd->file);
  free(cmd);

  return ret;
}

int tcl_load_config(const char *fname, struct cmd_q *cmdq)
{
  TCL_INTERP_CHECKINIT(CMD_RETURN_ERROR);

  int ret = 0;
  set_global_cmdq(cmdq);
  if (Tcl_EvalFile(tcl_interp, fname) == TCL_ERROR) {
    cfg_add_cause("%s: %s", fname, Tcl_GetStringResult(tcl_interp));
    ret = -1;
  }
  set_global_cmdq(NULL);

  return ret;
}


