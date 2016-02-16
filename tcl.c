#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tcl.h>

#include "tmux.h"

enum cmd_retval cmd_tcl_exec(struct cmd *self, struct cmd_q *cmdq);

const struct cmd_entry cmd_tcl_entry /* avoid auto-create cmd for this */
=
{
	.name = "exec-tcl",
	.alias = "tcl",

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

struct cmd_q *global_cmdq;
enum cmd_retval global_cmd_retval = CMD_RETURN_NORMAL;

#define tcl2tmux_call tcl2tmux_call2

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types-discards-qualifiers"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-label"
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
    cmdq_error(global_cmdq, "tmux::usage: %s %s", c.entry->name, c.entry->usage);
    Tcl_AddErrorInfo(tcl_interp, "tmux wrong number of args");
    Tcl_SetErrorCode(tcl_interp, "tmux wrong number of args", NULL);
    Tcl_SetResult(tcl_interp, "tmux wrong number of args", NULL);
    global_cmd_retval = CMD_RETURN_ERROR;
    return TCL_ERROR;
  }

  struct cmd *cmd_old = global_cmdq->cmd;
  global_cmdq->cmd = &c;

  if (cmd_prepare_state(&c, global_cmdq, NULL) != 0) {
    global_cmd_retval = CMD_RETURN_ERROR;
  } else {
    global_cmd_retval = (*c.entry->exec)(&c, global_cmdq);
  }
  global_cmdq->cmd = cmd_old;

  args_free(c.args);
  //free(c.args);

  return global_cmd_retval == CMD_RETURN_NORMAL ? TCL_OK : TCL_ERROR;
}


int tcl2tmux_call2(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  global_cmd_retval = CMD_RETURN_NORMAL;

  struct cmd *cmd = xcalloc(1, sizeof *cmd);
  cmd->entry = (struct cmd_entry *)clientData;
  cmd->args = args_parse(cmd->entry->args.template, argc, argv);

  // do some job of cmd_parse
  if ((cmd->args == NULL) ||
      (cmd->entry->args.lower != -1 && cmd->args->argc < cmd->entry->args.lower) ||
      (cmd->entry->args.upper != -1 && cmd->args->argc > cmd->entry->args.upper))
  {
    free(cmd);
    cmdq_error(global_cmdq, "tmux::usage: %s %s", cmd->entry->name, cmd->entry->usage);
    Tcl_AddErrorInfo(tcl_interp, "tmux wrong number of args");
    Tcl_SetErrorCode(tcl_interp, "tmux wrong number of args", NULL);
    Tcl_SetResult(tcl_interp, "tmux wrong number of args", NULL);
    global_cmd_retval = CMD_RETURN_ERROR;
    return TCL_ERROR;
  }

  struct cmd_list *cmdlist = xcalloc(1, sizeof *cmdlist);
  cmdlist->references = 1;
  TAILQ_INIT(&cmdlist->list);

  struct cmd_q *cmdq = cmdq_new(global_cmdq->client);
  cmdq->parent = global_cmdq;

  TAILQ_INSERT_TAIL(&cmdlist->list, cmd, qentry);

  cmdq_run(cmdq, cmdlist, NULL);

  cmd_list_free(cmdlist);
  cmdq_free(cmdq);

  return global_cmd_retval == CMD_RETURN_NORMAL ? TCL_OK : TCL_ERROR;
}


void tcl_error(const char * str)
{
    cmdq_error(global_cmdq, "%s", str);
    Tcl_AddErrorInfo(tcl_interp, str);
    Tcl_SetErrorCode(tcl_interp, str, NULL);
    Tcl_SetResult(tcl_interp, str, NULL);
}


extern const struct cmd_entry cmd_attach_session_entry;

int tcl_format_proc(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  global_cmd_retval = CMD_RETURN_NORMAL;
  if (argc != 2) {
    tcl_error("Usage: format string");
    global_cmd_retval = CMD_RETURN_ERROR;
    return TCL_ERROR;
  }

  struct cmd cmd;
  memset(&cmd, 0, sizeof(cmd));

  cmd.entry = &cmd_attach_session_entry;
  char *aa[] = {"", NULL};
  cmd.args = args_parse(cmd.entry->args.template, 1, aa);

  if (cmd_prepare_state(&cmd, global_cmdq, NULL) != 0) {
    tcl_error("format: prepare_state fail");
    global_cmd_retval = CMD_RETURN_ERROR;
  } else {
    struct session	*s = global_cmdq->state.tflag.s;
    struct client	*c = global_cmdq->client;
    struct winlink	*wl = global_cmdq->state.tflag.wl;
    struct window_pane	*wp = global_cmdq->state.tflag.wp;
    struct format_tree	*ft;

    ft = format_create(global_cmdq, 0);
    format_defaults(ft, c, s, wl, wp);
    char * str = format_expand(ft, argv[1]);
// warning: incompatible pointer types passing 'void (void *)' to parameter of type 'Tcl_FreeProc *' (aka 'void (*)(char *)') [-Wincompatible-pointer-types]
    Tcl_SetResult(tcl_interp, str, (Tcl_FreeProc*)free);

    //free(str);
    format_free(ft);
  }

  args_free(cmd.args);

  return global_cmd_retval == CMD_RETURN_NORMAL ? TCL_OK : TCL_ERROR;
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

  char cmdName[100] = "::tmux::";
  for (const struct cmd_entry **pcmd_e = cmd_table; *pcmd_e; pcmd_e++) {
    const struct cmd_entry *cmd_e = *pcmd_e;
    if (cmd_e == &cmd_tcl_entry) continue;

    Tcl_Command tcl_cmd;

    if (cmd_e->name) {
      strcpy(cmdName+8, cmd_e->name);
      tcl_cmd = Tcl_CreateCommand(tcl_interp, cmdName, &tcl2tmux_call, (ClientData)cmd_e, NULL);
      log_debug("Tcl_CreateCommand %s = %p", cmdName, tcl_cmd);
    }

    if (cmd_e->alias) {
      strcpy(cmdName+8, cmd_e->alias);
      tcl_cmd = Tcl_CreateCommand(tcl_interp, cmdName, &tcl2tmux_call, (ClientData)cmd_e, NULL);
      log_debug("Tcl_CreateCommand %s = %p", cmdName, tcl_cmd);
    }
  }

  Tcl_Eval(tcl_interp, "proc tmux {args} { namespace eval ::tmux {*}$args }");

  Tcl_CreateCommand( tcl_interp, "::tmux::format", &tcl_format_proc,
      (ClientData) NULL, NULL ) ;

  log_debug("tcl init ok");
  return;

cleanup:
  log_debug("tcl init failed");
  tcl_interp = NULL;
}

#pragma clang diagnostic pop


enum cmd_retval
cmd_tcl_exec(struct cmd *self, struct cmd_q *cmdq)
{
  struct args		*args = self->args;
  struct client		*c = cmdq->state.c;
  struct session	*s = cmdq->state.tflag.s;
  struct winlink	*wl = cmdq->state.tflag.wl;
  struct window_pane	*wp = cmdq->state.tflag.wp;

  global_cmdq = cmdq;

  log_debug("%s:%d s=%p c=%p wl=%p wp=%p", __FILE__, __LINE__, s, c, wl, wp);

  if (args->argc == 0) return CMD_RETURN_NORMAL;
  for (int i=0; i < args->argc; i++) {
    log_debug(" tcl arg[%d]: <%s>", i, args->argv[i]);
  }
  if (args->argc > 1) {
    cmdq_error(cmdq, "Usage: tcl command: got %d args", args->argc);
    return CMD_RETURN_ERROR;
  }
  if (tcl_interp == NULL) {
    log_debug("tcl init...");
    tcl_init(0, NULL);
    if (tcl_interp == NULL) {
      return CMD_RETURN_ERROR;
    }
    log_debug("tcl init ok");
  }
  if (Tcl_Eval(tcl_interp, args->argv[0]) == TCL_ERROR) {
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

