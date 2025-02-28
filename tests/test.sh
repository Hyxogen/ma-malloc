#!/bin/bash

HOSTTYPE="$(uname -m)_$(uname -s)"
LIB_NAME="libft_malloc_$HOSTTYPE.so"
LIB_DIR=$PWD

file "$LIB_DIR/$LIB_NAME"

escape() {
	echo "$@" | sed 's/"/\\"/g'
}

wrapper() {
	LD_PRELOAD="$LIB_DIR/$LIB_NAME" $@
}

eprint() {
	echo "$@" >&2
}

compare_output() {
	MINE_STDOUT=$(mktemp)
	MINE_STDERR=$(mktemp)
	THEIR_STDOUT=$(mktemp)
	THEIR_STDERR=$(mktemp)

	wrapper $@ > $MINE_STDOUT 2> $MINE_STDERR
	MINE_STATUS=$?
	$@ > $THEIR_STDOUT 2> $THEIR_STDERR
	THEIR_STATUS=$?

	diff -q $MINE_STDERR $THEIR_STDERR >/dev/null
	if [ $? != 0 ]; then
		eprint "KO: stderr does not match"
		eprint "command: $(escape $@)"
		eprint "< mine"
		eprint "> theirs"

		diff $MINE_STDERR $THEIR_STDERR --color
		exit 1
	fi


	diff -q $MINE_STDOUT $THEIR_STDOUT >/dev/null
	if [ $? != 0 ]; then
		eprint "KO: stdout does not match"
		eprint "command: $(escape $@)"
		eprint "< mine"
		eprint "> theirs"

		diff $MINE_STDOUT $THEIR_STDOUT >> stdout.diff
		exit 1
	fi

	if [ "$MINE_STATUS" != "$THEIR_STATUS" ]; then
		eprint "KO: exit codes do not match. mine=$MINE_STATUS theirs=$THEIR_STATUS"
		eprint "command: $(escape $@)"
		exit 1
	fi


	rm -f $MINE_STDOUT
	rm -f $MINE_STDERR
	rm -f $THEIR_STDOUT
	rm -f $THEIR_STDERR
}

compare_output grep -r "int" | sort
compare_output /usr/bin/ls
compare_output cat "$LIB_DIR/Makefile"
compare_output nm "$LIB_DIR/$LIB_NAME"
echo "OK"
