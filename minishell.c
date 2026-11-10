#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_ARGS 64
#define MAX_CMDS 16

struct cmd
{
    char *argv[MAX_ARGS];
    int argc;
    char *infile;
    char *outfile;
    int append;
};

static struct sigaction sa_dfl;

static void sigint_handler(int sig)
{
    ssize_t r;

    (void)sig;
    r = write(STDOUT_FILENO, "\n", 1);
    (void)r;
}

static void sigchld_handler(int sig)
{
    int saved_errno = errno;

    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        continue;
    }
    errno = saved_errno;
}

static int is_operator(const char *s)
{
    return strcmp(s, "|") == 0 || strcmp(s, "<") == 0 ||
           strcmp(s, ">") == 0 || strcmp(s, ">>") == 0 ||
           strcmp(s, "&") == 0;
}

static int parse(char *line, struct cmd cmds[], int *ncmds, int *background)
{
    char *token;
    int c = 0;
    int i;

    *background = 0;
    for (i = 0; i < MAX_CMDS; i++)
    {
        cmds[i].argc = 0;
        cmds[i].argv[0] = NULL;
        cmds[i].infile = NULL;
        cmds[i].outfile = NULL;
        cmds[i].append = 0;
    }

    token = strtok(line, " \t");
    if (token == NULL)
    {
        return 1;
    }

    while (token != NULL)
    {
        if (strcmp(token, "|") == 0)
        {
            if (cmds[c].argc == 0)
            {
                fprintf(stderr, "minishell: syntax error: missing command before '|'\n");
                return -1;
            }
            c++;
            if (c >= MAX_CMDS)
            {
                fprintf(stderr, "minishell: too many commands in a pipeline (maximum %d)\n", MAX_CMDS);
                return -1;
            }
        }
        else if (strcmp(token, "<") == 0 || strcmp(token, ">") == 0 ||
                 strcmp(token, ">>") == 0)
        {
            char *op = token;
            char *file = strtok(NULL, " \t");

            if (file == NULL || is_operator(file))
            {
                fprintf(stderr, "minishell: syntax error: missing file after '%s'\n", op);
                return -1;
            }
            if (strcmp(op, "<") == 0)
            {
                cmds[c].infile = file;
            }
            else
            {
                cmds[c].outfile = file;
                cmds[c].append = (strcmp(op, ">>") == 0);
            }
        }
        else if (strcmp(token, "&") == 0)
        {
            if (strtok(NULL, " \t") != NULL)
            {
                fprintf(stderr, "minishell: syntax error: '&' must be the last word\n");
                return -1;
            }
            *background = 1;
            break;
        }
        else
        {
            if (cmds[c].argc >= MAX_ARGS - 1)
            {
                fprintf(stderr, "minishell: too many arguments (maximum %d)\n", MAX_ARGS - 1);
                return -1;
            }
            cmds[c].argv[cmds[c].argc] = token;
            cmds[c].argc++;
            cmds[c].argv[cmds[c].argc] = NULL;
        }
        token = strtok(NULL, " \t");
    }

    if (cmds[c].argc == 0)
    {
        if (c > 0)
        {
            fprintf(stderr, "minishell: syntax error: missing command after '|'\n");
        }
        else
        {
            fprintf(stderr, "minishell: syntax error: missing command\n");
        }
        return -1;
    }

    *ncmds = c + 1;
    return 0;
}

static int is_builtin(const char *name)
{
    return strcmp(name, "cd") == 0 || strcmp(name, "pwd") == 0 ||
           strcmp(name, "help") == 0;
}

static void run_builtin(char **argv)
{
    if (strcmp(argv[0], "cd") == 0)
    {
        char *target = argv[1];

        if (target == NULL)
        {
            target = getenv("HOME");
        }
        if (chdir(target) != 0)
        {
            perror("cd");
        }
    }
    else if (strcmp(argv[0], "pwd") == 0)
    {
        char cwd[1024];

        if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
            printf("%s\n", cwd);
        }
        else
        {
            perror("pwd");
        }
    }
    else if (strcmp(argv[0], "help") == 0)
    {
        printf("minishell built-in commands:\n");
        printf("  cd [dir]  change the directory (no dir = home)\n");
        printf("  pwd       print the current directory\n");
        printf("  help      show this message\n");
        printf("  exit      leave the shell\n");
        printf("Redirection: cmd < file, cmd > file, cmd >> file\n");
        printf("Pipes: cmd1 | cmd2 | cmd3 ...\n");
        printf("Background: add & at the end of a command\n");
        printf("Ctrl+C stops a running command, but not the shell.\n");
    }
}

static void run_pipeline(struct cmd cmds[], int n, int background)
{
    int fd[MAX_CMDS - 1][2];
    pid_t pids[MAX_CMDS];
    int i, j;
    int started = 0;

    for (i = 0; i < n - 1; i++)
    {
        if (pipe(fd[i]) < 0)
        {
            perror("pipe failed");
            for (j = 0; j < i; j++)
            {
                close(fd[j][0]);
                close(fd[j][1]);
            }
            return;
        }
    }

    for (i = 0; i < n; i++)
    {
        pids[i] = fork();

        if (pids[i] < 0)
        {
            perror("fork failed");
            break;
        }
        else if (pids[i] == 0)
        {
            sigaction(SIGINT, &sa_dfl, NULL);
            sigaction(SIGCHLD, &sa_dfl, NULL);

            if (background)
            {
                setpgid(0, 0);
            }

            if (i > 0)
            {
                dup2(fd[i - 1][0], STDIN_FILENO);
            }
            if (i < n - 1)
            {
                dup2(fd[i][1], STDOUT_FILENO);
            }

            for (j = 0; j < n - 1; j++)
            {
                close(fd[j][0]);
                close(fd[j][1]);
            }

            if (cmds[i].infile != NULL)
            {
                int fd_in = open(cmds[i].infile, O_RDONLY);

                if (fd_in < 0)
                {
                    perror(cmds[i].infile);
                    _exit(1);
                }
                dup2(fd_in, STDIN_FILENO);
                close(fd_in);
            }

            if (cmds[i].outfile != NULL)
            {
                int flags = O_WRONLY | O_CREAT;
                int fd_out;

                if (cmds[i].append)
                {
                    flags = flags | O_APPEND;
                }
                else
                {
                    flags = flags | O_TRUNC;
                }

                fd_out = open(cmds[i].outfile, flags, 0644);
                if (fd_out < 0)
                {
                    perror(cmds[i].outfile);
                    _exit(1);
                }
                dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
            }

            execvp(cmds[i].argv[0], cmds[i].argv);
            fprintf(stderr, "minishell: %s: command not found\n", cmds[i].argv[0]);
            _exit(1);
        }
        started++;
    }

    for (j = 0; j < n - 1; j++)
    {
        close(fd[j][0]);
        close(fd[j][1]);
    }

    if (background)
    {
        if (started > 0)
        {
            printf("[background pid: %d]\n", pids[started - 1]);
        }
    }
    else
    {
        for (i = 0; i < started; i++)
        {
            while (waitpid(pids[i], NULL, 0) < 0 && errno == EINTR)
            {
                continue;
            }
        }
    }
}

int main(void)
{
    char line[1024];
    struct cmd cmds[MAX_CMDS];
    int ncmds, background, r;
    struct sigaction sa_int, sa_chld;

    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    memset(&sa_dfl, 0, sizeof(sa_dfl));
    sa_dfl.sa_handler = SIG_DFL;
    sigemptyset(&sa_dfl.sa_mask);

    while (1)
    {
        printf("minishell> ");
        fflush(stdout);

        errno = 0;
        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            if (errno == EINTR)
            {
                clearerr(stdin);
                continue;
            }
            printf("\n");
            break;
        }

        line[strcspn(line, "\n")] = '\0';

        r = parse(line, cmds, &ncmds, &background);
        if (r != 0)
        {
            continue;
        }

        if (ncmds == 1 && strcmp(cmds[0].argv[0], "exit") == 0)
        {
            break;
        }

        if (ncmds == 1 && is_builtin(cmds[0].argv[0]))
        {
            run_builtin(cmds[0].argv);
            continue;
        }

        run_pipeline(cmds, ncmds, background);
    }

    printf("Goodbye!\n");
    return 0;
}
