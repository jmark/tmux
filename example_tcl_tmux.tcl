
# default tcl mode for command line
:set tcl 1

# Mark the current word in copy-mode:
bind-key -t vi-copy M-Enter tcl {
	mark-current-word
}
#bind-key -t vi-copy M-Enter tcl {display [copy-mode-selection]}

# Jump to the next word and select
bind-key -t vi-copy Tab tcl {
	clear-selection
	next-space
	mark-current-word
}
bind-key -t vi-copy M-Right tcl {
	clear-selection
	next-space
	mark-current-word
}
# Jump to the previous word and select
bind-key -t vi-copy BTab tcl {
	clear-selection
	previous-space
	mark-current-word
}
bind-key -t vi-copy M-Left tcl {
	clear-selection
	previous-space
	mark-current-word
}

bind-key -t vi-copy M-Up tcl {
	clear-selection
	cursor-up
	mark-current-word
}
bind-key -t vi-copy M-Down tcl {
	clear-selection
	cursor-down
	mark-current-word
}


proc is_word_char {c} {
	print [scan $c %c]
	return [expr {$c > " " && $c != "\x7f"}]
}

proc mark-current-word {} {
	clear-selection
	set l [copy-mode-screenline]
	set x [copy-mode-get-cx]
	print "<[string range $l $x $x]>"
	if {![is_word_char [string range $l $x $x]]} return
	incr x
	while {[is_word_char [string range $l $x $x]]} {
		cursor-right
		incr x
	}
	incr x -2
	begin-selection
	while {[is_word_char [string range $l $x $x]]} {
		cursor-left
		if {$x < 1} return
		incr x -1
	}
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

bind-key -Troot M-` tcl { status-msg "xprefix: ? for help"; switch-client -Txprefix }

bind-key -Txprefix ? tcl {
	show-messages
	print "Xtended-Prefix mode help:"
	print "? : help"
	print "* : (in a shell) choose file or folder"
	print "H : (in a shell) choose from history"
	#history-bottom
	bottom-line
}

proc browse-dir {d prev} {
	set d [file normalize $d]
	set prev "[file normalize $prev]/"
	if {[catch {glob -type d "$d/*"} dirs]} { set dirs "" }
	if {[catch {glob -type f "$d/*"} files]} { set files "" }
	set dirs [linsert $dirs 0 $d [file dirname $d] ]
	choose-from-list \
		-selected-id $prev \
		-cmd [format {browse-dir [string range $_ 0 end-1] %s} [list "$d/"]] \
			-list [lmap d $dirs {list "$d/"}] \
		"" \
		-cmd {print {*}[split [read_file $_] "\n"]} \
			-list $files
}

bind-key -Txprefix l tcl {
	browse-dir [f #{pane_current_path}] [f #{pane_current_path}]
}

bind-key -Txprefix * tcl {
	set files [split [exec ls -la] "\n"]
	split-window;
	choose-from-list -onselect {
		kill-pane
		send-keys [regsub {^\s*(\S+\s+){8}} $_ {}]
	} -oncancel {
	} -- {*}$files
	# select-pane +
}

bind-key -Txprefix H tcl {
	if {[f #{pane_current_command}] eq "bash"} {
		choose-from-list -onselect {
			send-keys $_
		} -- {*}[
			lcomp {$x} for x in [
				split [read_file ~/.bash_history] "\n"
			] if {[string range $x 0 0] ne "#"}
		]
		end-of-list
		up
	} else {
		print "Bash?"
	}
}

bind-key -Txprefix Space tcl {
	status-msg "Use arrow keys to navigate"
	copy-mode
	#cursor-up ; start-of-line
	#mark-current-word
	switch-client -Tsel
}

bind-key -Tsel Up tcl {
	clear-selection
	cursor-up
	mark-current-word
	switch-client -Tsel
}

bind-key -Tsel Down tcl {
	clear-selection
	cursor-down
	mark-current-word
	switch-client -Tsel
}

bind-key -Tsel Left tcl {
	clear-selection
	previous-space
	mark-current-word
	switch-client -Tsel
}

bind-key -Tsel Right tcl {
	clear-selection
	next-space
	mark-current-word
	switch-client -Tsel
}

bind-key -Tsel Enter tcl {
	status-msg "Selection copied; mode exit"
	copy-selection
}

bind-key -Tsel q tcl {
	status-msg "Mode cancel"
	cancel
}

bind-key -Tsel M-` tcl {
	status-msg "Mode cancel"
	cancel
}



#####################################################
# http://wiki.tcl.tk/12574
# Iteration type	[foreach] example	[lcomp] example
# Simple	foreach a $list {...}	lcomp {...} for a in $list
# Striding	foreach {a b} $list {...}	lcomp {...} for {a b} in $list
# Unpacking	foreach _ $list {lassign $_ a b; ...}	lcomp {...} for {a b} inside $list
# Combinatorial	foreach a $list1 {foreach b $list2 {...}}	lcomp {...} for a in $list1 for b in $list2
# Parallel	foreach a $list1 b $list2 {...}	lcomp {...} for a in $list1 and b in $list2
# Conditional	foreach a $list {if {cond} {...}}	lcomp {...} for a in $list if {cond}
#
# [lcomp]	[foreach]
# lcomp {$a * 2} for a in $list	set _ {}; foreach a $list {lappend _ [expr {$a * 2}]}; set _
# lcomp {$b} {$a} for {a b} in $list	set _ {}; foreach {a b} $list {lappend _ $b $a}; set _

proc lcomp {expression args} {
    set __0__ "lappend __1__ \[expr [list $expression]\]"
    while {[llength $args] && [lindex $args 0] ni {for if with}} {
        append __0__ " \[expr [list [lindex $args 0]]\]"
        set args [lrange $args 1 end]
    }
    set tmpvar 2
    set structure {}
    set upvars {}
    while {[llength $args]} {
        set prefix ""
        switch [lindex $args 0] {
        for {
            set nest [list foreach]
            while {[llength $nest] == 1 || [lindex $args 0] eq "and"} {
                if {[llength $args] < 4 || [lindex $args 2] ni {in inside}} {
                    error "wrong # operands: must be \"for\" vars \"in?side?\"\
                           vals ?\"and\" vars \"in?side?\" vals? ?...?"
                }
                switch [lindex $args 2] {
                in {
                    lappend nest [lindex $args 1] [lindex $args 3]
                } inside {
                    lappend nest __${tmpvar}__ [lindex $args 3]
                    append prefix "lassign \$__${tmpvar}__ [lindex $args 1]\n"
                    incr tmpvar
                }}
                set args [lrange $args 4 end]
            }
            lappend structure $nest $prefix
        } if {
            if {[llength $args] < 2} {
                error "wrong # operands: must be \"if\" condition"
            }
            lappend structure [list if [lindex $args 1]] $prefix
            set args [lrange $args 2 end]
        } with {
            if {[llength $args] < 2} {
                error "wrong # operands: must be \"with\" varlist"
            }
            foreach var [lindex $args 1] {
                lappend upvars $var $var
            }
            set args [lrange $args 2 end]
        } default {
            error "bad opcode \"[lindex $args 0]\": must be for, if, or with"
        }}
    }
    foreach {prefix nest} [lreverse $structure] {
        set __0__ [concat $nest [list \n$prefix$__0__]]
    }
    if {[llength $upvars]} {
        set __0__ "upvar 1 $upvars; $__0__"
    }
    unset -nocomplain expression args tmpvar prefix nest structure var upvars
    set __1__ ""
    eval $__0__
    return $__1__
}







