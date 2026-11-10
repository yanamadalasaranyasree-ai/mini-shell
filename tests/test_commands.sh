#!/bin/bash
# Automatic tests for minishell (roadmap T01 to T15)

cd "$(dirname "$0")/.." || exit 1
BIN="$PWD/minishell"

if [ ! -x "$BIN" ]; then
    echo "minishell not found. Run 'make' first."
    exit 1
fi

PASS=0
FAIL=0

run_shell()
{
    local dir
    dir=$(mktemp -d /tmp/minishell_test.XXXXXX)
    (cd "$dir" && printf '%b\n' "$1" | timeout 15 "$BIN" 2>&1)
    rm -rf "$dir"
}

check()
{
    local id="$1" desc="$2" input="$3" expected="$4"
    local output

    output=$(run_shell "$input")
    if printf '%s\n' "$output" | grep -qF -- "$expected"; then
        echo "PASS  $id  $desc"
        PASS=$((PASS + 1))
    else
        echo "FAIL  $id  $desc"
        echo "      expected to find: $expected"
        FAIL=$((FAIL + 1))
    fi
}

check_not()
{
    local id="$1" desc="$2" input="$3" unwanted="$4"
    local output

    output=$(run_shell "$input")
    if printf '%s\n' "$output" | grep -qF -- "$unwanted"; then
        echo "FAIL  $id  $desc"
        echo "      should NOT contain: $unwanted"
        FAIL=$((FAIL + 1))
    else
        echo "PASS  $id  $desc"
        PASS=$((PASS + 1))
    fi
}

check "T01" "pwd prints the current directory" "pwd" "/tmp/minishell_test"
check "T02" "ls -l runs and output appears" "ls -l" "total 0"
check "T03" "echo prints text" "echo Hello Linux" "Hello Linux"
check "T04" "cd changes the directory" "cd /usr\npwd" "/usr"
check "T05" "cd /invalid shows an error" "cd /invalid" "No such file or directory"
check "T05" "shell continues after a bad cd" "cd /invalid\necho still_alive" "still_alive"
check "T06" "ls > files.txt stores the output" "ls > files.txt\ncat files.txt" "files.txt"
check "T07" "echo >> appends and keeps old text" "echo Linux >> log.txt\necho Linux >> log.txt\nwc -l < log.txt" "2"
check "T08" "cat < file reads the file as input" "echo alpha > in.txt\ncat < in.txt" "alpha"
check "T09" "ls | grep passes output along" "echo x > demo.c\nls | grep demo" "demo.c"
check "T10" "three-command pipeline works" "echo Hello Linux >> file.txt\necho Linux shell >> file.txt\necho other >> file.txt\ncat file.txt | grep Linux | wc -l" "2"
check "T11" "background job prints its pid" "sleep 1 &\necho back" "[background pid:"
echo "MANUAL T12  Ctrl+C during a foreground sleep 10 (try it by hand)"
check "T13" "unknown command gives an error" "invalidcommand" "command not found"
check_not "T14" "no zombie processes remain" "sleep 1 &\nsleep 1 &\nsleep 1 &\nsleep 2\nps -e -o stat,args | grep sleep" "defunct"
check "T15" "exit ends the shell cleanly" "exit" "Goodbye!"

check "E1" "syntax error: pipe first" "| ls" "syntax error"
check "E2" "syntax error: missing file" "ls >" "syntax error"
check "E3" "missing input file" "cat < nofile.txt" "No such file or directory"
check "C1" "redirection and pipe together" "echo hello > a.txt\ncat < a.txt | grep hello > b.txt\ncat b.txt" "hello"

echo
echo "Passed: $PASS   Failed: $FAIL"
[ "$FAIL" -eq 0 ]
