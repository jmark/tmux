
# default tcl mode for command line
:set tcl 1

# Mark the current word:
bind-key -t vi-copy M-Enter tcl {
	# mark the current word in copy-mode
	clear-selection
	previous-space
	begin-selection
	next-space-end
}
#bind-key -t vi-copy M-Enter tcl {display [copy-mode-selection]}

# Jump to the next word and select
bind-key -t vi-copy Tab tcl {
	# mark the current word in copy-mode
	clear-selection
	next-space
	begin-selection
	next-space-end
}
# Jump to the previous word and select
bind-key -t vi-copy BTab tcl {
	# mark the current word in copy-mode
	clear-selection
	#cursor-right
	previous-space
	previous-space
	begin-selection
	next-space-end
}

# Open selection in a vim mini-window (no shell and files)
bind-key -t vi-copy y tcl {
	split-window -c [f #{pane_current_path}] -l 5 "
		echo -n [shell-quote [copy-mode-selection]] | vim -R -"
}

# Open selection in a emacs mini-window (no shell and files)
bind-key -t emacs-copy y tcl {
	split-window -c [f #{pane_current_path}] -l 5 "
		emacs --insert <(echo -n [shell-quote [copy-mode-selection]])"
}

# Current directory listing, if a shell is running
# TODO: select by Tab, S-Tab; paste by Enter
# ......mark individual files; mark/unmark all
bind-key * tcl {
	switch -- [f #{pane_current_command}] bash - zsh - tcsh {
		print "Directory listing of [f #{pane_current_path}]:"
		print {*}[split \
			[exec $env(SHELL) -c "ls -lF [shell-quote [f #{pane_current_path}]]"] \
			"\n"]
	}
}

# Select an action from bash history
# TODO: select by Tab, S-Tab; paste by Enter
bind-key H tcl {
	if {[f #{pane_current_command}] eq "bash"} {
		print {*}[split \
			[read_file ~/.bash_history] \
			"\n"]
		#send-keys Gk
		history-bottom
		cursor-up
		start-of-line
	} else {
		print "Bash?"
	}
}

