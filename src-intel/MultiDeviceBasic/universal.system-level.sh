#!/bin/sh

set -e
shopt -s extglob

progname=$(basename "$0")
curdirname=$(dirname "$0")
function say() {
    echo "$progname: $*" >&2
}; # function say

function die() {
    say "$@"
    exit ${status:-3}
}; # function die

function err() {
    status=3
    die "Oops, unexpected error occurred."
}; # function err

trap err ERR

if [[ $# -gt 0 && "$1" == "--help" ]]; then
    #    0          1        2         3         4         5         6         7         8
    #    012345678901234567890123456789012345678901234567890123456789012345678901234567890
    echo "NAME"
    echo "    $progname -- Run several jobs in parallel."
    echo ""
    echo "SYNOPSIS"
    echo "    $progname MICNUM CPUNUM"
    echo "    $progname --help"
    echo ""
    echo "DESCRIPTION"
    echo "    TO DO "
    echo ""
    echo "OPTIONS"
    echo "    --help  -- Show this help and exit."
    echo ""
    echo "ARGUMENTS"
    echo "    MICNUM  -- Number of jobs to be launched on MIC cards [0,number MIC cards]."
    echo "    CPUNUM  -- Number of jobs to be launched on CPUs [0,number of CPUs]."
    echo ""
    echo "EXIT STATUS"
    echo "    0 -- All the jobs completed successfully."
    echo "    1 -- One or more jobs failed (e. g. returned non-zero status)."
    echo "    2 -- Usage error."
    echo "    3 -- Unexpected error."
    echo ""
    exit 0
fi

# Report errors as usage errors.
status=2

[[ $# -gt 0 ]] || die "Number of jobs is not specified."

# Parse number of jobs.
micnum=$1
[[ "$micnum" == +([0-9]) ]] || die "Bad first argument: \`$micnum'; expected a cardinal number."
cpunum=$2
[[ "$cpunum" == +([0-9]) ]] || die "Bad first argument: \`$cpunum'; expected a cardinal number."
number=$[micnum+cpunum]

# Parse the command.
miccommand=( "$curdirname/multidevice -t acc -c system --instance-count $number --instance-index %n" )
cpucommand=( "$curdirname/multidevice -t cpu -c system --instance-count $number --instance-index %n" )

# Following errors are runtime errors.
status=1
n=1

# Start jobs.
for (( ; n <= cpunum; n = n + 1 )); do
    say "Starting job %$n on CPU..."
    cmd=( ${cpucommand[@]//%n/$[n-1]} )
    say "${cmd[@]}"
    { eval "${cmd[@]}"; } > "job-$n.out" 2> "job-$n.err" &
    say "Job %$n started on CPU."
done

for (( ; n <= $number; n = n + 1 )); do
    say "Starting job %$n on MIC..."
    cmd=( OFFLOAD_DEVICES=$[n-cpunum-1] ${miccommand[@]//%n/$[n-1]} )
    say "${cmd[@]}"
    { eval "${cmd[@]}"; } > "job-$n.out" 2> "job-$n.err" &
    say "Job %$n started on MIC."
done


# Wait jobs, show results.
success=0
failure=0
for (( n = 1; n <= number; n = n + 1 )); do
    say "Waiting job %$n..."
    js=0
    wait %$n || js=$?
    say "Job %$n finished."
    say "Job %$n status: $js"
    if [[ -s "job-$n.err" ]]; then
        say "Job %$n stderr:"
        cat "job-$n.err"
    else
        say "Job %$n stderr: (empty)"
    fi
    if [[ -s "job-$n.out" ]]; then
        say "Job %$n stdout:"
        cat "job-$n.out"
    else
        say "Job %$n stdout: (empty)"
    fi
    if [[ $js == 0 ]]; then
        success=$(( success + 1 ))
    else
        failure=$(( failure + 1 ))
    fi
done

# Show overall result and exit.
say "Success: $success of $number"
say "Failure: $failure of $number"
if [[ $failure == "0" ]]; then
    say "Overall: +++ Success +++"
    rc=0
else
    say "Overall: --- Failure ---"
    rc=1
fi
exit $rc

# end of file #
