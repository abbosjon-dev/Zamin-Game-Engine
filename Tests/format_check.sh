#!/usr/bin/env bash
# clang-format check for the lines you changed (.clang-format: the code base's existing style). The code base is
# not reformatted as a whole: only the lines that differ from a commit are checked (git clang-format), so a change
# is formatted the way the code around it is written.
#   Tests/format_check.sh                     uncommitted changes (working tree and index) against HEAD
#   Tests/format_check.sh --base main         everything changed since the branch left main (commits + uncommitted)
#   Tests/format_check.sh --base HEAD~3       the last three commits and the uncommitted changes
#   Tests/format_check.sh --fix [--base REF]  format those lines in place
#   Tests/format_check.sh --files A.cpp B.h   whole files (--fix: in place)
#   Tests/format_check.sh --github            GitHub Actions: a warning annotation per hunk, the diff on the run's
#                                             page; exits 0 (advisory - see below)
#   Tests/format_check.sh --stats [--top N]   how far the whole code base is from .clang-format (Tests/format_stats.py)
# New files must be known to git (git add, or git add -N) to be checked. vendor/ is never formatted
# (vendor/.clang-format: DisableFormat). CLANG_FORMAT=<binary> picks clang-format (17 or newer; 18 by default).
# Some house conventions cannot be expressed in clang-format (several short statements on one line, one-line short
# enums - docs/quality.md); a suggestion on such a line may be ignored, which is why --github only warns.
# Exit code: 0 formatted, 1 lines to change (the diff is printed), 2 bad usage / clang-format missing.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
extensions="cpp,h,hpp,c,cc,inl"

base=""
fix=0
github=0
stats=0
files=()
stats_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --base) base="$2"; shift 2 ;;
        --fix) fix=1; shift ;;
        --github) github=1; shift ;;
        --stats) stats=1; shift ;;
        --top|--json|--max-percent|--show) stats_args+=("$1" "$2"); shift 2 ;;
        --files) shift; while [[ $# -gt 0 && "$1" != --* ]]; do files+=("$1"); shift; done ;;
        -h|--help) sed -n '2,19p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "noma'lum variant: $1 (--help)" >&2; exit 2 ;;
    esac
done

# ---- clang-format 17+ (AlignConsecutiveShortCaseStatements) and its git helper --------------------------------
cf="${CLANG_FORMAT:-}"
if [[ -z "$cf" ]]; then
    for c in clang-format-18 clang-format-19 clang-format-20 clang-format-17 clang-format; do
        if command -v "$c" >/dev/null 2>&1; then cf="$c"; break; fi
    done
fi
if [[ -z "$cf" ]] || ! command -v "$cf" >/dev/null 2>&1; then
    echo "clang-format topilmadi (o'rnating: sudo apt-get install clang-format-18, yoki CLANG_FORMAT=<yo'l>)" >&2
    exit 2
fi
version="$("$cf" --version | sed -n 's/.*version \([0-9]*\).*/\1/p' | head -n 1)"
if [[ -z "$version" || "$version" -lt 17 ]]; then
    echo "$cf versiyasi ${version:-?}: kamida 17 kerak (.clang-format AlignConsecutiveShortCaseStatements ishlatadi)" >&2
    exit 2
fi

if [[ $stats -eq 1 ]]; then
    exec python3 "$root/Tests/format_stats.py" --clang-format "$cf" "${stats_args[@]}"
fi

cd "$root" || exit 2

# ---- whole files ----------------------------------------------------------------------------------------------
if [[ ${#files[@]} -gt 0 ]]; then
    if [[ $fix -eq 1 ]]; then
        "$cf" -i --style=file "${files[@]}" || exit 2
        echo "Formatlandi: ${#files[@]} ta fayl"
        exit 0
    fi
    bad=0
    for f in "${files[@]}"; do
        if ! out="$("$cf" --style=file "$f" 2>&1)"; then echo "$out" >&2; exit 2; fi
        d="$(diff -u --label "a/$f" --label "b/$f" "$f" - <<<"$out")"
        if [[ -n "$d" ]]; then echo "$d"; bad=$((bad + 1)); fi
    done
    if [[ $bad -eq 0 ]]; then echo "clang-format: ${#files[@]} ta fayl formatlangan"; exit 0; fi
    echo "clang-format: $bad ta faylda o'zgartiriladigan qatorlar bor (--fix --files ... tuzatadi)" >&2
    exit 1
fi

# ---- the changed lines (git clang-format) ---------------------------------------------------------------------
gcf=""
for c in "git-clang-format-$version" git-clang-format; do
    if command -v "$c" >/dev/null 2>&1; then gcf="$c"; break; fi
done
if [[ -z "$gcf" ]]; then
    echo "git-clang-format topilmadi (clang-format paketi bilan keladi)" >&2
    exit 2
fi
commit="HEAD"
if [[ -n "$base" ]]; then
    if ! git rev-parse --verify -q "$base^{commit}" >/dev/null; then
        echo "commit topilmadi: $base" >&2
        exit 2
    fi
    # A branch: from where it left that branch (merge base); a plain older commit is its own merge base.
    commit="$(git merge-base "$base" HEAD 2>/dev/null || git rev-parse "$base")"
fi
what="$(git rev-parse --short "$commit")"
[[ -n "$base" ]] && what="$base ($what)"

if [[ $fix -eq 1 ]]; then
    # (git clang-format exits with 1 when it changed files.)
    fix_out="$("$gcf" --binary "$(command -v "$cf")" --extensions "$extensions" --force "$commit" 2>&1)"
    code=$?
    echo "$fix_out"
    if [[ $code -ne 0 && "$fix_out" != *"changed files:"* ]]; then exit 2; fi
    exit 0
fi

diff_out="$("$gcf" --binary "$(command -v "$cf")" --extensions "$extensions" --diff "$commit" 2>&1)"
code=$?
if [[ "$diff_out" != diff* && "$diff_out" != *$'\n'diff* ]]; then
    if [[ $code -ne 0 && "$diff_out" != *"did not modify"* && "$diff_out" != *"no modified files"* ]]; then
        echo "$diff_out" >&2
        exit 2
    fi
    echo "clang-format: $what dan beri o'zgargan qatorlar formatlangan"
    exit 0
fi

# The changed lines of each hunk, as lines of the current file ("@@ -a,b +c,d @@" starts at line c; the context
# lines around a change are left out).
hunks="$(awk '
    function flush() { if (first > 0) print f ":" first ":" last; first = 0 }
    /^diff --git / { flush(); next }
    /^\+\+\+ / { f = substr($0, 7); next }
    /^--- / { next }
    /^@@ / { flush(); split($3, p, ","); n = substr(p[1], 2) + 0; next }
    /^ / { n++; next }
    /^[-+]/ { line = (n > 0 ? n : 1); if (first == 0) first = line; last = line; if (substr($0, 1, 1) == "+") n++; next }
    END { flush() }' <<<"$diff_out")"
count="$(grep -c . <<<"$hunks")"
if [[ $github -eq 1 ]]; then
    while IFS=: read -r f s e; do
        [[ -z "$f" ]] && continue
        echo "::warning file=$f,line=$s,endLine=$e,title=clang-format::.clang-format bu qatorlarni boshqacha yozadi (Tests/format_check.sh --fix)"
    done <<<"$hunks"
    if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
        {
            echo "### clang-format: $count ta bo'lak ($what dan beri o'zgargan qatorlar)"
            echo
            echo '```diff'
            echo "$diff_out" | head -n 400
            echo '```'
        } >>"$GITHUB_STEP_SUMMARY"
    fi
    echo "$diff_out"
    echo "clang-format: $count ta bo'lak boshqacha formatlanadi (ogohlantirish; tuzatish: Tests/format_check.sh --fix --base <commit>)"
    exit 0
fi
echo "$diff_out"
echo
echo "clang-format: $count ta bo'lak boshqacha formatlanadi ($what dan beri o'zgargan qatorlarda); tuzatish: Tests/format_check.sh --fix${base:+ --base $base}" >&2
exit 1
