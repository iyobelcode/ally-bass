#!/usr/bin/env bash
# ==============================================================================
# ALLY 1300 SUB SYNTH — Projecthygiëne & Release-Metadata Controlescript
#
# Controleert getrackte bronbestanden, commits, metadata en distributie-
# configuraties op productconsistentie en de afwezigheid van ongewenste
# toolbranding en hardcoded ontwikkelpaden.
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

ERRORS=0

log_pass() {
    echo -e "  ${GREEN}[PASS]${NC} $1"
}

log_fail() {
    echo -e "  ${RED}[FAIL]${NC} $1"
    ERRORS=$((ERRORS + 1))
}

log_info() {
    echo -e "${CYAN}=== $1 ===${NC}"
}

echo ""
echo "======================================================================="
echo "  ALLY - 1300 SUB SYNTH | PROJECTHYGIËNE & METADATA CONTROLE"
echo "======================================================================="
echo ""

# ------------------------------------------------------------------------------
# 1. Controleer op ongewenste toolbranding en chatmetadata in bronbestanden
# ------------------------------------------------------------------------------
log_info "1. Scan broncode en documentatie op ongewenste toolbranding"

# Zoekpatronen voor AI-assistenten, codegeneratietools en trailers.
# Sluit externe bibliotheken (JUCE/), buildmappen en git-objecten uit.
# Sluit dit controlescript zelf uit om zelfreferentie te voorkomen.
TOOL_PATTERN='\b(claude|chatgpt|openai|anthropic|gemini|copilot|windsurf|deepmind|antigravity|co-authored-by)\b'

MATCHING_FILES=$(git ls-files | grep -v -E '^(JUCE/|scripts/check_project_hygiene\.sh)' | while read -r file; do
    if [ -f "$file" ]; then
        if grep -inE "${TOOL_PATTERN}" "$file" >/dev/null 2>&1; then
            echo "$file"
        fi
    fi
done)

if [ -z "${MATCHING_FILES}" ]; then
    log_pass "Geen ongewenste toolbranding of assistent-verwijzingen aangetroffen."
else
    log_fail "Ongewenste toolverwijzingen aangetroffen in:"
    echo "${MATCHING_FILES}" | while read -r line; do
        echo -e "    ${YELLOW}${line}${NC}"
        grep -inE "${TOOL_PATTERN}" "${line}" | head -n 3 | sed 's/^/      /'
    done
fi

# ------------------------------------------------------------------------------
# 2. Controleer Git-geschiedenis op auteurschap en neutrale commitberichten
# ------------------------------------------------------------------------------
log_info "2. Controleer Git-commits en auteurschap"

EXPECTED_AUTHOR_EMAIL="juiced.unicorn.nft@gmail.com"
INVALID_AUTHORS=$(git log --format="%an <%ae>" | grep -v "${EXPECTED_AUTHOR_EMAIL}" | sort -u || true)

if [ -z "${INVALID_AUTHORS}" ]; then
    log_pass "Alle commits hebben het gevalideerde auteursadres (${EXPECTED_AUTHOR_EMAIL})."
else
    log_fail "Afwijkende commit-auteurs aangetroffen: ${INVALID_AUTHORS}"
fi

# Controleer commitberichten op toolbranding
INVALID_COMMITS=$(git log --format="%h %s %b" | grep -iE "${TOOL_PATTERN}" || true)

if [ -z "${INVALID_COMMITS}" ]; then
    log_pass "Alle commitberichten zijn neutraal en technisch beschrijvend."
else
    log_fail "Commitberichten bevatten toolverwijzingen: ${INVALID_COMMITS}"
fi

# ------------------------------------------------------------------------------
# 3. Controleer product- en fabrikantmetadata
# ------------------------------------------------------------------------------
log_info "3. Controleer productmetadata en identifiers"

check_file_contains() {
    local file="$1"
    local expected="$2"
    local description="$3"

    if [ ! -f "$file" ]; then
        log_fail "Bestand ontbreekt: $file"
        return
    fi

    if grep -F "${expected}" "$file" >/dev/null 2>&1; then
        log_pass "${description}"
    else
        log_fail "${description} ontbreekt in ${file} (verwacht: '${expected}')"
    fi
}

check_file_contains "CMakeLists.txt" 'PRODUCT_NAME "ALLY - 1300 SUB SYNTH"' "CMakeLists: PRODUCT_NAME = ALLY - 1300 SUB SYNTH"
check_file_contains "CMakeLists.txt" 'COMPANY_NAME "JUICED UNICORN"' "CMakeLists: COMPANY_NAME = JUICED UNICORN"
check_file_contains "CMakeLists.txt" 'PLUGIN_MANUFACTURER_CODE JUnc' "CMakeLists: Fabrikantcode = JUnc"
check_file_contains "CMakeLists.txt" 'PLUGIN_CODE AlBa' "CMakeLists: Plugincode = AlBa"
check_file_contains "CMakeLists.txt" 'BUNDLE_ID "com.juicedunicorn.allybass"' "CMakeLists: Bundle ID = com.juicedunicorn.allybass"

check_file_contains "installer/AllyBass-Windows.iss" '#define AppName    "ALLY - 1300 SUB SYNTH"' "Inno Setup: AppName = ALLY - 1300 SUB SYNTH"
check_file_contains "installer/AllyBass-Windows.iss" '#define AppPublisher "JUICED UNICORN / IZHIA ARTS / 42GRAM"' "Inno Setup: AppPublisher = JUICED UNICORN / IZHIA ARTS / 42GRAM"
check_file_contains "installer/AllyBass-Windows.iss" 'VST3Dir    "staging\ALLY - 1300 SUB SYNTH.vst3"' "Inno Setup: VST3 staging bundle pad"

check_file_contains ".github/workflows/build-windows.yml" 'ALLY - 1300 SUB SYNTH.vst3' "GitHub Actions: VST3 zoekfilter"

check_file_contains "README.md" '# ALLY - 1300 SUB SYNTH' "README: Titel = ALLY - 1300 SUB SYNTH"
check_file_contains "WINDOWS-BUILD.md" '# ALLY - 1300 SUB SYNTH' "WINDOWS-BUILD: Titel = ALLY - 1300 SUB SYNTH"

# ------------------------------------------------------------------------------
# 4. Controleer op hardcoded machine- en gebruikerspaden in bronbestanden
# ------------------------------------------------------------------------------
log_info "4. Controleer op hardcoded lokale machinepaden in broncode"

HARDCODED_PATHS=$(git ls-files 'Source/**' 'CMakeLists.txt' | while read -r file; do
    if grep -nE '(/Users/[a-zA-Z0-9_-]+|C:\\Users\\[a-zA-Z0-9_-]+)' "$file" >/dev/null 2>&1; then
        echo "$file"
    fi
done)

if [ -z "${HARDCODED_PATHS}" ]; then
    log_pass "Geen hardcoded lokale gebruikerspaden in broncode of CMake."
else
    log_fail "Lokale machinepaden aangetroffen in: ${HARDCODED_PATHS}"
fi

# ------------------------------------------------------------------------------
# 5. Overzicht en exitcode
# ------------------------------------------------------------------------------
echo ""
echo "======================================================================="
if [ ${ERRORS} -eq 0 ]; then
    echo -e "  ${GREEN}PROJECTHYGIËNE CONTROLE GESLAAGD (0 fouten)${NC}"
    echo "======================================================================="
    echo ""
    exit 0
else
    echo -e "  ${RED}PROJECTHYGIËNE CONTROLE MISLUKT (${ERRORS} fouten aangetroffen)${NC}"
    echo "======================================================================="
    echo ""
    exit 1
fi
