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

	.tflag = 0,

	.flags = 0,
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
enum cmd_retval global_cmd_retval;

int tcl2tmux_call(
       ClientData clientData,
       Tcl_Interp *interp,
       int argc,
       const char **argv)
{
  struct cmd c;
  memset(&c, 0, sizeof(c));

  c.entry = (struct cmd_entry *)clientData;
  c.args = args_parse(c.entry->args.template, argc, argv);
  // c.file = NULL;
  // c.line = 0;
  // c.flags = 0;
  // c.qentry ?

  global_cmd_retval = (*c.entry->exec)(&c, global_cmdq);

  free(c.args);

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

  log_debug("tcl init ok");
  return;

cleanup:
  log_debug("tcl init failed");
  tcl_interp = NULL;
}

enum cmd_retval
cmd_tcl_exec(struct cmd *self, struct cmd_q *cmdq)
{
  struct args		*args = self->args;
  struct client		*c = cmdq->state.c;
  struct session	*s = cmdq->state.tflag.s;
  struct winlink	*wl = cmdq->state.tflag.wl;
  struct window_pane	*wp = cmdq->state.tflag.wp;

  global_cmdq = cmdq;

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
  if (Tcl_Eval(tcl_interp, args->argv[0]) != TCL_OK) {
    log_debug("tcl error: %s", Tcl_GetStringResult(tcl_interp));
    cmdq_error(cmdq, "Error: %s", Tcl_GetStringResult(tcl_interp));
    return CMD_RETURN_ERROR;
  }
  log_debug("tcl ok: %s", Tcl_GetStringResult(tcl_interp));
  cmdq_print(cmdq, "%s", Tcl_GetStringResult(tcl_interp));
  status_message_set(c, "%s", Tcl_GetStringResult(tcl_interp));
  return CMD_RETURN_NORMAL;
}

