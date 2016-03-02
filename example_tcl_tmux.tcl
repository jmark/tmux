
# default tcl mode for command line
:set tcl

# Mark the current word:
bind-key -t vi-copy M-Enter tcl {send-keys "B E"}
#bind-key -t vi-copy M-Enter tcl {display [copy-mode-selection]}

# Open selection in a vim mini-window (no shell and files)
bind-key -t vi-copy Y tcl {split-window -c [f #{pane_current_path}] -l 5 "echo -n [shell-quote [copy-mode-selection]] | vim -R -"}

