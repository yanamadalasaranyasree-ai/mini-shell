# Mini Shell

A small Linux shell written in C. It shows the minishell> prompt, reads a command, and runs it using the same system calls a real shell uses (fork, execvp, waitpid, pipe, dup2, sigaction).

Language: C on GNU/Linux
Build: GCC + Make

## Build and run

    make          build the shell
    make run      build (if needed) and start it
    make clean    remove the compiled program
    ./minishell   start it directly

## Features

- Prompt loop: shows `minishell> `, reads a line, ignores empty lines.
- External commands: ls, date, echo, grep and any other program (fork, execvp, waitpid).
- Built-ins: cd, pwd, help, exit.
- Redirection: cmd < file, cmd > file, cmd >> file.
- Pipes: cmd1 | cmd2 | cmd3 (any number of commands, up to 16).
- Background jobs: cmd & returns the prompt at once and prints the pid.
- Signals: Ctrl+C stops a running command but not the shell. A SIGCHLD handler collects finished children, so no zombies remain.

## Architecture

    Prompt / input loop
          |
    parse()  -> a list of commands (words, <, >, >>, |, &)
          |
    built-in?  yes -> run inside the shell (cd, pwd, help), exit ends the loop
          |  no
    run_pipeline()  -> pipes, fork one child per command,
                       dup2 for pipes and files, execvp
          |
    parent: waitpid (foreground) or print pid (background)

Signals: SIGINT (Ctrl+C) is caught by the shell; SIGCHLD collects finished children.

## Source files

    minishell.c              the whole shell
    Makefile                 build rules
    tests/test_commands.sh   automatic tests
    README.md                this file

## Usage examples

    minishell> pwd
    minishell> ls -l | grep c | wc -l
    minishell> ls > files.txt
    minishell> echo Linux >> log.txt
    minishell> cat < files.txt
    minishell> sleep 10 &
    minishell> cd /tmp
    minishell> exit

## Tests

Run the automatic tests (roadmap cases T01 to T15 plus error cases):

    ./tests/test_commands.sh

The Ctrl+C test (T12) must be tried by hand: type sleep 10, press Ctrl+C, and check that the shell stays alive.

## Known limitations

- Words must be separated by spaces: write ls | wc, not ls|wc.
- No quotes: echo "a b" is not handled as one word.
- cd, pwd, help and exit work only as a single command (not inside a pipeline), and they ignore redirection.
- A line may have at most 63 words per command, and at most 16 commands in a pipeline.
- No command history, environment variables, wildcards or && / ||.
- Ctrl+\ (SIGQUIT) is not handled and ends the shell.
- cd ~ is not expanded. Plain cd goes to the home folder.

## Learning summary

In this project I learned how a Linux shell works internally. A shell runs a command by calling fork() to create a child process, then execvp() in the child to replace it with the requested program, while the parent calls waitpid() to wait for it to finish. The cd command must be a built-in because it changes the working directory of the process that runs it, and in a child process the change would be lost. Redirection and pipes work with file descriptors: dup2() makes descriptor 0 or 1 point to a file or to a pipe end, and every unused pipe descriptor must be closed so the reading command can see the end of its input. A pipeline of N commands needs N-1 pipes and one child per command. I also learned that background jobs are simply children the shell does not wait for, that a finished child which is never collected becomes a zombie, and that signal handlers set with sigaction (SIGINT for Ctrl+C and SIGCHLD for finished children) let the shell survive Ctrl+C and collect its children automatically.
