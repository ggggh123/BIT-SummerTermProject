#!/bin/sh
# Behavioural coverage for the environment-check profiles.  Each trace below
# comes from executing the real script, so it catches optional checks leaking
# into the wrong profile.
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
check_env=${CHECK_ENV:-"$repo_root/scripts/check_env.sh"}
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/check-env-test.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

total=0
passed=0
failed=0

pass() {
  total=$((total + 1))
  passed=$((passed + 1))
  printf 'PASS %s\n' "$1"
}

fail() {
  total=$((total + 1))
  failed=$((failed + 1))
  printf 'FAIL %s: %s\n' "$1" "$2"
}

trace_command_checks() {
  sed -n 's/^+ command -v //p' "$1" | tr '\n' ' ' | sed 's/ $//'
}

trace_qt_checks() {
  sed -n 's/^+ pkg-config --atleast-version=6\.2 //p' "$1" | tr '\n' ' ' | sed 's/ $//'
}

trace_python_imports() {
  sed -n 's/^+ python3 -c //p' "$1" | tr '\n' '|' | sed 's/|$//'
}

check_profile_trace() {
  profile_name=$1
  trace_file=$2
  expected_commands=$3
  expected_imports=$4
  shift 4

  if sh -x "$check_env" "$@" >/dev/null 2>"$trace_file"; then
    actual_commands=$(trace_command_checks "$trace_file")
    actual_qt_modules=$(trace_qt_checks "$trace_file")
    actual_imports=$(trace_python_imports "$trace_file")
    if [ "$actual_commands" = "$expected_commands" ] && \
       [ "$actual_qt_modules" = "$expected_qt_modules" ] && \
       [ "$actual_imports" = "$expected_imports" ]; then
      pass "$profile_name trace checks exact commands, Qt modules, and Python imports"
    else
      fail "$profile_name trace checks exact commands, Qt modules, and Python imports" \
        "commands [$actual_commands]; Qt [$actual_qt_modules]; imports [$actual_imports]"
    fi
  else
    fail "$profile_name trace checks exact commands, Qt modules, and Python imports" \
      'traced profile check returned non-zero'
  fi
}

if sh -n "$check_env"; then
  pass 'check_env syntax is valid POSIX sh'
else
  fail 'check_env syntax is valid POSIX sh' 'sh -n failed'
fi

if help_output=$("$check_env" --help 2>&1); then
  if printf '%s\n' "$help_output" | grep -F -- '--with-web' >/dev/null 2>&1 && \
     printf '%s\n' "$help_output" | grep -F -- '--with-ml' >/dev/null 2>&1; then
    pass 'help succeeds and documents both optional profiles'
  else
    fail 'help succeeds and documents both optional profiles' 'optional flags were not listed'
  fi
else
  fail 'help succeeds and documents both optional profiles' 'help returned non-zero'
fi

if unknown_output=$("$check_env" --unknown 2>&1); then
  fail 'unknown option returns usage exit status 2' 'command returned 0'
else
  unknown_status=$?
  if [ "$unknown_status" -eq 2 ] && printf '%s\n' "$unknown_output" | grep -F -- 'Usage:' >/dev/null 2>&1; then
    pass 'unknown option returns usage exit status 2'
  else
    fail 'unknown option returns usage exit status 2' "got status $unknown_status"
  fi
fi

if "$check_env" >/dev/null 2>&1; then
  pass 'default core environment check succeeds'
else
  fail 'default core environment check succeeds' 'core check returned non-zero'
fi

if "$check_env" --with-web >/dev/null 2>&1; then
  pass 'web profile environment check succeeds'
else
  fail 'web profile environment check succeeds' 'web check returned non-zero'
fi

if "$check_env" --with-ml >/dev/null 2>&1; then
  pass 'ml profile environment check succeeds'
else
  fail 'ml profile environment check succeeds' 'ml check returned non-zero'
fi

if "$check_env" --with-web --with-ml >/dev/null 2>&1; then
  pass 'web and ml profiles succeed together'
else
  fail 'web and ml profiles succeed together' 'combined check returned non-zero'
fi

if "$check_env" --with-ml --with-web >/dev/null 2>&1; then
  pass 'optional profile order does not matter'
else
  fail 'optional profile order does not matter' 'reversed combined check returned non-zero'
fi

expected_core_commands='git cmake ninja make g++ qmake6 qtpaths6 python3 pkg-config'
expected_web_commands="$expected_core_commands node npm"
expected_qt_modules='Qt6Core Qt6Network Qt6Widgets Qt6WebEngineWidgets Qt6Charts Qt6Test'
expected_core_imports='import pytest'
expected_ml_imports='import pytest|import numpy, pandas, sklearn, joblib'

check_profile_trace 'default' "$tmp_dir/default.trace" \
  "$expected_core_commands" "$expected_core_imports"
check_profile_trace 'web' "$tmp_dir/web.trace" \
  "$expected_web_commands" "$expected_core_imports" --with-web
check_profile_trace 'ml' "$tmp_dir/ml.trace" \
  "$expected_core_commands" "$expected_ml_imports" --with-ml
check_profile_trace 'web and ml' "$tmp_dir/web-ml.trace" \
  "$expected_web_commands" "$expected_ml_imports" --with-web --with-ml
check_profile_trace 'ml and web' "$tmp_dir/ml-web.trace" \
  "$expected_web_commands" "$expected_ml_imports" --with-ml --with-web

printf '%s tests: %s passed, %s failed\n' "$total" "$passed" "$failed"
[ "$failed" -eq 0 ]
