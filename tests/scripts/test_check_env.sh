#!/bin/sh
# Behavioural coverage for the environment-check profiles.  Each trace below
# comes from executing the real script, so it catches optional checks leaking
# into the wrong profile.
set -u

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
check_env="$repo_root/scripts/check_env.sh"
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

trace_has() {
  trace_file=$1
  trace_text=$2
  grep -F -- "$trace_text" "$trace_file" >/dev/null 2>&1
}

trace_lacks_optional_ml() {
  ! grep -E 'numpy|pandas|sklearn|joblib' "$1" >/dev/null 2>&1
}

trace_lacks_optional_web() {
  ! grep -E '(^|[^[:alnum:]_])(node|npm)([^[:alnum:]_]|$)' "$1" >/dev/null 2>&1
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

default_trace="$tmp_dir/default.trace"
if sh -x "$check_env" >/dev/null 2>"$default_trace"; then
  expected_core_commands='git cmake make g++ qmake6 qtpaths6 python3 pkg-config'
  actual_core_commands=$(sed -n 's/^+ command -v //p' "$default_trace" | tr '\n' ' ' | sed 's/ $//')
  if [ "$actual_core_commands" = "$expected_core_commands" ] && \
     trace_lacks_optional_web "$default_trace" && trace_lacks_optional_ml "$default_trace"; then
    pass 'default trace checks exactly core commands and no optional dependencies'
  else
    fail 'default trace checks exactly core commands and no optional dependencies' \
      "saw command checks [$actual_core_commands] or optional trace activity"
  fi
else
  fail 'default trace checks exactly core commands and no optional dependencies' 'traced core check returned non-zero'
fi

web_trace="$tmp_dir/web.trace"
if sh -x "$check_env" --with-web >/dev/null 2>"$web_trace" && \
   trace_has "$web_trace" '+ command -v node' && \
   trace_has "$web_trace" '+ command -v npm' && \
   trace_lacks_optional_ml "$web_trace"; then
  pass 'web trace executes node and npm checks without ML imports'
else
  fail 'web trace executes node and npm checks without ML imports' 'missing Web checks or ML checks leaked into Web profile'
fi

ml_trace="$tmp_dir/ml.trace"
if sh -x "$check_env" --with-ml >/dev/null 2>"$ml_trace" && \
   trace_has "$ml_trace" 'import numpy, pandas, sklearn, joblib' && \
   trace_lacks_optional_web "$ml_trace"; then
  pass 'ml trace imports all ML dependencies without node or npm checks'
else
  fail 'ml trace imports all ML dependencies without node or npm checks' 'missing ML import or Web checks leaked into ML profile'
fi

printf '%s tests: %s passed, %s failed\n' "$total" "$passed" "$failed"
[ "$failed" -eq 0 ]
