#!/bin/bash

# =============================================================================
# BSD 3-Clause License
# =============================================================================
#
# Copyright (c) 2025, Edson Brandi
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# =============================================================================
# FreeBSD Device Drivers Book Build Script
# =============================================================================
# This script builds the book in PDF, EPUB, and/or HTML5 formats using Pandoc
# and the Eisvogel LaTeX template.
#
# Supported languages (content sources):
#   en_US  English  (original)          content/chapters,             content/appendices
#   pt_BR  Brazilian Portuguese         translations/pt_BR/chapters,  translations/pt_BR/appendices
#   es_ES  Spanish                      translations/es_ES/chapters,  translations/es_ES/appendices
#
# Running the script with no arguments builds every supported format
# (PDF + EPUB + HTML) in every supported language. Individual formats or
# languages may be selected with --pdf/--epub/--html/--all and --lang.
# =============================================================================

set -e  # Exit on any error

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

# Change to the root directory to ensure correct relative paths
cd "$ROOT_DIR"

# =============================================================================
# CONFIGURATION
# =============================================================================

# Book information
BOOK_TITLE="FreeBSD Device Drivers"
BOOK_AUTHOR="Edson Brandi"
BOOK_DATE="Version 2.0 - April, 22th 2026"

# File paths
TITLE_FILE="$SCRIPT_DIR/title.md"
METADATA_FILE="$SCRIPT_DIR/metadata.yaml"
OUTPUT_DIR="public/downloads"
OUTPUT_BASENAME="freebsd-device-drivers"

# Template and engine
EISVOGEL_TEMPLATE="$HOME/.local/share/pandoc/templates/eisvogel.latex"
PDF_ENGINE="xelatex"

# Supported languages in canonical form. The list order controls the order
# used by "build everything" runs and by the final summary.
LANGUAGES_ALL=(en_US pt_BR es_ES)

# =============================================================================
# LANGUAGE HELPERS
# =============================================================================

# Map a canonical language code to its chapters directory (relative to ROOT_DIR).
get_chapters_dir() {
    case "$1" in
        en_US) echo "content/chapters" ;;
        pt_BR) echo "translations/pt_BR/chapters" ;;
        es_ES) echo "translations/es_ES/chapters" ;;
        *)     echo ""; return 1 ;;
    esac
}

# Map a canonical language code to its appendices directory.
get_appendices_dir() {
    case "$1" in
        en_US) echo "content/appendices" ;;
        pt_BR) echo "translations/pt_BR/appendices" ;;
        es_ES) echo "translations/es_ES/appendices" ;;
        *)     echo ""; return 1 ;;
    esac
}

# Map a canonical language code to its Pandoc/LaTeX `lang` metadata value.
# Pandoc uses this to pick the right hyphenation/typography rules, which is
# why pt_BR and es_ES need it set explicitly instead of defaulting to en-US.
get_pandoc_lang() {
    case "$1" in
        en_US) echo "en-US" ;;
        pt_BR) echo "pt-BR" ;;
        es_ES) echo "es-ES" ;;
        *)     echo ""; return 1 ;;
    esac
}

# Human-readable display name for a canonical language code.
get_lang_display() {
    case "$1" in
        en_US) echo "English (en_US)" ;;
        pt_BR) echo "Brazilian Portuguese (pt_BR)" ;;
        es_ES) echo "Spanish (es_ES)" ;;
        *)     echo "$1" ;;
    esac
}

# Normalize any user-supplied language string into a canonical form.
# Case- and separator-insensitive: accepts en_US, en-US, EN_us, En-Us, etc.
# Also accepts "all" as a sentinel meaning "every supported language".
# Prints the canonical form on success, empty string on unknown input.
normalize_lang() {
    local input="$1"
    # Strip '-' and '_' separators, then lowercase.
    local stripped="${input//-/}"
    stripped="${stripped//_/}"
    stripped="${stripped,,}"
    case "$stripped" in
        all)  echo "all" ;;
        enus) echo "en_US" ;;
        ptbr) echo "pt_BR" ;;
        eses) echo "es_ES" ;;
        *)    echo "" ;;
    esac
}

# Append a canonical language code to the BUILD_LANGS array, preserving
# insertion order and skipping duplicates.
add_lang() {
    local canonical="$1"
    local l
    for l in "${BUILD_LANGS[@]}"; do
        [ "$l" = "$canonical" ] && return 0
    done
    BUILD_LANGS+=("$canonical")
}

# =============================================================================
# FILE HELPERS
# =============================================================================

# Function to filter markdown files by line count (exclude files with less than 20 lines)
filter_markdown_files() {
    local directory="$1"
    local show_warnings="${2:-true}"  # Default to showing warnings
    local filtered_files=()

    if [ -d "$directory" ]; then
        while IFS= read -r -d '' file; do
            # Check if file exists and is readable
            if [ -f "$file" ] && [ -r "$file" ]; then
                local line_count=$(wc -l < "$file" 2>/dev/null || echo "0")
                if [ "$line_count" -ge 20 ]; then
                    filtered_files+=("$file")
                else
                    if [ "$show_warnings" = "true" ]; then
                        echo "   ⚠ Skipping short file: $file ($line_count lines, minimum 20 required)"
                    fi
                fi
            else
                if [ "$show_warnings" = "true" ]; then
                    echo "   ⚠ Skipping inaccessible file: $file"
                fi
            fi
        done < <(find "$directory" -name "*.md" -print0 | sort -z)
    fi

    # Return array elements properly quoted
    printf '%s\n' "${filtered_files[@]}"
}

# Collect the ordered file list for a given language build:
# title file + chapters + appendices (short files are filtered out).
# Populates the globals:
#   BUILD_FILES              — array of input files for pandoc
#   BUILD_FILES_CHAPTERS     — number of chapters included
#   BUILD_FILES_APPENDICES   — number of appendices included
# Returns 0 on success, 1 if the language has no valid chapter files.
collect_build_files() {
    local lang="$1"
    local show_warnings="${2:-false}"
    local chapters_dir
    local appendices_dir
    chapters_dir="$(get_chapters_dir "$lang")"
    appendices_dir="$(get_appendices_dir "$lang")"

    BUILD_FILES=("$TITLE_FILE")
    BUILD_FILES_CHAPTERS=0
    BUILD_FILES_APPENDICES=0

    while IFS= read -r f; do
        if [ -n "$f" ]; then
            BUILD_FILES+=("$f")
            BUILD_FILES_CHAPTERS=$((BUILD_FILES_CHAPTERS + 1))
        fi
    done < <(filter_markdown_files "$chapters_dir" "$show_warnings")

    while IFS= read -r f; do
        if [ -n "$f" ]; then
            BUILD_FILES+=("$f")
            BUILD_FILES_APPENDICES=$((BUILD_FILES_APPENDICES + 1))
        fi
    done < <(filter_markdown_files "$appendices_dir" "$show_warnings")

    # Warn about files missing a top-level H1. Pandoc silently demotes the
    # whole file into a subsection of the previous chapter when this happens,
    # which means a chapter or appendix can disappear from the table of
    # contents without any error from the build itself.
    local missing_h1=0
    local f
    for f in "${BUILD_FILES[@]}"; do
        [ "$f" = "$TITLE_FILE" ] && continue
        # Match a top-level H1 line: starts with exactly one '#' followed by
        # a space. Skip YAML front matter so '#' inside metadata is ignored.
        if ! awk '
            BEGIN { in_fm = 0; seen_fm = 0 }
            NR == 1 && /^---[[:space:]]*$/ { in_fm = 1; seen_fm = 1; next }
            in_fm && /^---[[:space:]]*$/ { in_fm = 0; next }
            in_fm { next }
            /^# / { found = 1; exit }
            END { exit (found ? 0 : 1) }
        ' "$f"; then
            echo "   ⚠ Missing top-level H1 heading: $f" >&2
            echo "     Pandoc will fold this file into the previous chapter as a subsection." >&2
            missing_h1=$((missing_h1 + 1))
        fi
    done
    if [ "$missing_h1" -gt 0 ]; then
        echo "   ⚠ $missing_h1 file(s) lack an H1 heading; the resulting book will have fewer top-level parts than expected." >&2
    fi

    if [ "$BUILD_FILES_CHAPTERS" -eq 0 ]; then
        return 1
    fi
    return 0
}

# =============================================================================
# HELP AND TESTING
# =============================================================================

show_help() {
    cat << EOF
Usage: $0 [FORMAT...] [--lang CODE]... [OPTIONS]

Build "FreeBSD Device Drivers" in one or more formats and languages.

FORMATS (case-insensitive; may be combined; default: all three):
    --pdf       Build PDF
    --epub      Build EPUB
    --html      Build HTML5
    --all       Shorthand for --pdf --epub --html

LANGUAGES (case- and separator-insensitive; repeatable; default: all three):
    --lang CODE
    --lang=CODE
        CODE may be:
            en_US   English (original)
            pt_BR   Brazilian Portuguese
            es_ES   Spanish
            all     Every supported language (the default)
        Case and the separator are ignored, so the following are equivalent:
            pt_BR   pt-BR   PT_br   Pt-Br   ptbr   PTBR

OTHER OPTIONS:
    --test      Check that every build-system dependency is installed
    -h, --help  Show this help message

DEFAULTS:
    Running "$0" with no arguments builds every supported format (PDF, EPUB,
    HTML) in every supported language (en_US, pt_BR, es_ES).

OUTPUT DIRECTORY:
    Generated files are placed under: $OUTPUT_DIR/

OUTPUT FILES (per language):
    English (en_US):
        $OUTPUT_DIR/${OUTPUT_BASENAME}-en_US.pdf
        $OUTPUT_DIR/${OUTPUT_BASENAME}-en_US.epub
        $OUTPUT_DIR/${OUTPUT_BASENAME}-en_US.html
    Brazilian Portuguese (pt_BR):
        $OUTPUT_DIR/${OUTPUT_BASENAME}-pt_BR.pdf
        $OUTPUT_DIR/${OUTPUT_BASENAME}-pt_BR.epub
        $OUTPUT_DIR/${OUTPUT_BASENAME}-pt_BR.html
    Spanish (es_ES):
        $OUTPUT_DIR/${OUTPUT_BASENAME}-es_ES.pdf
        $OUTPUT_DIR/${OUTPUT_BASENAME}-es_ES.epub
        $OUTPUT_DIR/${OUTPUT_BASENAME}-es_ES.html

CONTENT SOURCES:
    en_US   content/chapters/              content/appendices/
    pt_BR   translations/pt_BR/chapters/   translations/pt_BR/appendices/
    es_ES   translations/es_ES/chapters/   translations/es_ES/appendices/

EXAMPLES:
    $0                              # Build every format in every language
    $0 --pdf                        # Build PDF in every language
    $0 --lang pt_BR                 # Build every format in Brazilian Portuguese
    $0 --lang pt-br --lang es-ES    # Build every format in pt_BR and es_ES
    $0 --pdf --lang en_US           # Build the English PDF only
    $0 --EPUB --HTML --Lang=Pt-Br   # Mixed case (all equivalent)
    $0 --test                       # Check dependencies without building

REQUIREMENTS:
    - Pandoc 3.0+
    - XeLaTeX (texlive-xetex)
    - Eisvogel template at: $EISVOGEL_TEMPLATE
    - LaTeX hyphenation packs for Portuguese and Spanish
      (e.g. texlive-lang-portuguese, texlive-lang-spanish)
    - Required fonts (Times New Roman, Arial, Liberation Mono)

For detailed installation instructions, see: $SCRIPT_DIR/BUILD-README.md
EOF
}

test_dependencies() {
    echo "🔍 Testing build system dependencies..."
    echo ""

    local all_good=true

    # Test Pandoc
    echo "1. Testing Pandoc..."
    if command -v pandoc >/dev/null 2>&1; then
        local pandoc_version=$(pandoc --version | head -n1)
        echo "   ✓ Pandoc found: $pandoc_version"

        # Check version
        local major_version=$(pandoc --version | head -n1 | grep -o '[0-9]\+\.[0-9]\+' | head -n1 | cut -d. -f1)
        if [ "$major_version" -ge 3 ]; then
            echo "   ✓ Pandoc version 3.0+ (compatible)"
        else
            echo "   ✗ Pandoc version $major_version.x (requires 3.0+)"
            all_good=false
        fi
    else
        echo "   ✗ Pandoc not found"
        all_good=false
    fi

    # Test XeLaTeX
    echo ""
    echo "2. Testing XeLaTeX..."
    if command -v xelatex >/dev/null 2>&1; then
        local xelatex_version=$(xelatex --version | head -n1)
        echo "   ✓ XeLaTeX found: $xelatex_version"
    else
        echo "   ✗ XeLaTeX not found"
        all_good=false
    fi

    # Test Eisvogel template
    echo ""
    echo "3. Testing Eisvogel template..."
    if [ -f "$EISVOGEL_TEMPLATE" ]; then
        echo "   ✓ Eisvogel template found: $EISVOGEL_TEMPLATE"
        local template_size=$(du -h "$EISVOGEL_TEMPLATE" | cut -f1)
        echo "   ✓ Template size: $template_size"
    else
        echo "   ✗ Eisvogel template not found at: $EISVOGEL_TEMPLATE"
        echo "   💡 Install with: wget -O $EISVOGEL_TEMPLATE https://raw.githubusercontent.com/Wandmalfarbe/pandoc-latex-template/master/eisvogel.tex"
        all_good=false
    fi

    # Test output directory
    echo ""
    echo "4. Testing output directory..."
    if [ -d "$OUTPUT_DIR" ]; then
        echo "   ✓ Output directory exists: $OUTPUT_DIR"
    else
        echo "   ⚠ Output directory does not exist, will be created: $OUTPUT_DIR"
    fi

    # Test title and metadata files
    echo ""
    echo "5. Testing title and metadata files..."
    if [ -f "$TITLE_FILE" ]; then
        echo "   ✓ Title file found: $TITLE_FILE"
    else
        echo "   ✗ Title file not found: $TITLE_FILE"
        all_good=false
    fi

    if [ -f "$METADATA_FILE" ]; then
        echo "   ✓ Metadata file found: $METADATA_FILE"
    else
        echo "   ✗ Metadata file not found: $METADATA_FILE"
        all_good=false
    fi

    # Test content files for every supported language
    echo ""
    echo "6. Testing content files per language..."
    local lang
    for lang in "${LANGUAGES_ALL[@]}"; do
        local chapters_dir
        local appendices_dir
        local display
        chapters_dir="$(get_chapters_dir "$lang")"
        appendices_dir="$(get_appendices_dir "$lang")"
        display="$(get_lang_display "$lang")"

        echo "   → $display"

        if [ -d "$chapters_dir" ]; then
            local chapter_files=()
            while IFS= read -r f; do
                [ -n "$f" ] && chapter_files+=("$f")
            done < <(filter_markdown_files "$chapters_dir" "false")

            if [ "${#chapter_files[@]}" -gt 0 ]; then
                echo "     ✓ Chapters:   ${#chapter_files[@]} files in $chapters_dir"
            else
                echo "     ✗ Chapters:   no valid files in $chapters_dir (all < 20 lines)"
                all_good=false
            fi
        else
            echo "     ✗ Chapters directory missing: $chapters_dir"
            all_good=false
        fi

        if [ -d "$appendices_dir" ]; then
            local appendix_files=()
            while IFS= read -r f; do
                [ -n "$f" ] && appendix_files+=("$f")
            done < <(filter_markdown_files "$appendices_dir" "false")

            if [ "${#appendix_files[@]}" -gt 0 ]; then
                echo "     ✓ Appendices: ${#appendix_files[@]} files in $appendices_dir"
            else
                echo "     ⚠ Appendices: no valid files in $appendices_dir"
            fi
        else
            echo "     ⚠ Appendices directory missing: $appendices_dir"
        fi
    done

    echo ""
    if [ "$all_good" = true ]; then
        echo "🎉 All dependencies are properly installed! You can build your book."
        echo "   Run: $0 --help for build options"
    else
        echo "❌ Some dependencies are missing. Please install them before building."
        echo "   See: $SCRIPT_DIR/BUILD-README.md for installation instructions"
        exit 1
    fi
}

# =============================================================================
# BUILD FUNCTIONS (one invocation per language × format)
# =============================================================================

build_pdf() {
    local lang="$1"
    local display
    local pandoc_lang
    local output_file
    display="$(get_lang_display "$lang")"
    pandoc_lang="$(get_pandoc_lang "$lang")"
    output_file="$OUTPUT_DIR/${OUTPUT_BASENAME}-${lang}.pdf"

    echo "📚 [$display] Building PDF: $output_file"

    # Ensure output directory exists
    mkdir -p "$OUTPUT_DIR"

    if ! collect_build_files "$lang" "false"; then
        echo "   ✗ No chapter files found for $display — skipping PDF"
        return 1
    fi

    echo "   Including files: ${#BUILD_FILES[@]} total (1 title + ${BUILD_FILES_CHAPTERS} chapters + ${BUILD_FILES_APPENDICES} appendices)"
    echo "   Running pandoc with Eisvogel template (lang=$pandoc_lang)..."

    pandoc "${BUILD_FILES[@]}" \
        --metadata-file="$METADATA_FILE" \
        --template eisvogel \
        --pdf-engine=xelatex \
        --from markdown+fenced_code_blocks \
        --toc \
        --toc-depth=3 \
        --number-sections \
        --metadata title="$BOOK_TITLE" \
        --metadata author="$BOOK_AUTHOR" \
        --metadata date="$BOOK_DATE" \
        --metadata lang="$pandoc_lang" \
        --variable titlepage=true \
        --variable toc-own-page=true \
        --variable graphics=true \
        --variable papersize=a4 \
        --variable documentclass=book \
        --variable book=true \
        --variable code-block-font-size=\\footnotesize \
        --variable float-placement-figure="H" \
        --variable figure-placement="H" \
        --syntax-highlighting=tango \
        --variable linestretch=1.15 \
        --variable geometry:"inner=2cm,outer=2cm,top=2.5cm,bottom=2.5cm" \
        -o "$output_file" 2>&1

    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        echo "   ✓ PDF successfully generated: $output_file"
        local file_size=$(du -h "$output_file" | cut -f1)
        echo "   ✓ PDF file size: $file_size"
    else
        echo "   ✗ PDF generation failed (exit code: $exit_code)"
        return $exit_code
    fi
}

build_epub() {
    local lang="$1"
    local display
    local pandoc_lang
    local output_file
    display="$(get_lang_display "$lang")"
    pandoc_lang="$(get_pandoc_lang "$lang")"
    output_file="$OUTPUT_DIR/${OUTPUT_BASENAME}-${lang}.epub"

    echo "📖 [$display] Building EPUB: $output_file"

    # Ensure output directory exists
    mkdir -p "$OUTPUT_DIR"

    if ! collect_build_files "$lang" "false"; then
        echo "   ✗ No chapter files found for $display — skipping EPUB"
        return 1
    fi

    echo "   Including files: ${#BUILD_FILES[@]} total (1 title + ${BUILD_FILES_CHAPTERS} chapters + ${BUILD_FILES_APPENDICES} appendices)"

    pandoc "${BUILD_FILES[@]}" \
        --metadata-file="$METADATA_FILE" \
        --metadata title="$BOOK_TITLE" \
        --metadata author="$BOOK_AUTHOR" \
        --metadata date="$BOOK_DATE" \
        --metadata lang="$pandoc_lang" \
        --toc \
        --toc-depth=2 \
        --number-sections \
        --syntax-highlighting=tango \
        --split-level=1 \
        --verbose \
        -o "$output_file" 2>&1

    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        echo "   ✓ EPUB successfully generated: $output_file"
        local file_size=$(du -h "$output_file" | cut -f1)
        echo "   ✓ EPUB file size: $file_size"
    else
        echo "   ✗ EPUB generation failed (exit code: $exit_code)"
        return $exit_code
    fi
}

build_html() {
    local lang="$1"
    local display
    local pandoc_lang
    local output_file
    display="$(get_lang_display "$lang")"
    pandoc_lang="$(get_pandoc_lang "$lang")"
    output_file="$OUTPUT_DIR/${OUTPUT_BASENAME}-${lang}.html"

    echo "🌐 [$display] Building HTML5: $output_file"

    # Ensure output directory exists
    mkdir -p "$OUTPUT_DIR"

    if ! collect_build_files "$lang" "false"; then
        echo "   ✗ No chapter files found for $display — skipping HTML"
        return 1
    fi

    # Debug: show what files will be processed
    echo "   Files to process:"
    echo "     Title:      $TITLE_FILE"
    echo "     Chapters:   ${BUILD_FILES_CHAPTERS} files"
    echo "     Appendices: ${BUILD_FILES_APPENDICES} files"
    echo "     Total:      ${#BUILD_FILES[@]} files"
    echo "   Using Pandoc's default CSS for better code highlighting"

    pandoc "${BUILD_FILES[@]}" \
        --metadata-file="$METADATA_FILE" \
        --to=html5 \
        --standalone \
        --embed-resources \
        --syntax-highlighting=tango \
        --toc \
        --toc-depth=2 \
        --number-sections \
        --metadata title="$BOOK_TITLE" \
        --metadata author="$BOOK_AUTHOR" \
        --metadata date="$BOOK_DATE" \
        --metadata lang="$pandoc_lang" \
        -o "$output_file" 2>&1

    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        echo "   ✓ HTML5 successfully generated: $output_file"
        local file_size=$(du -h "$output_file" | cut -f1)
        echo "   ✓ HTML5 file size: $file_size"
        echo "   ✓ HTML5 file has embedded resources and can be opened in any web browser"
    else
        echo "   ✗ HTML5 generation failed (exit code: $exit_code)"
        return $exit_code
    fi
}

show_build_summary() {
    echo ""
    echo "🎉 Build process completed!"
    echo ""
    echo "Generated files:"

    local lang
    local any_listed=false
    for lang in "${BUILD_LANGS[@]}"; do
        local display
        display="$(get_lang_display "$lang")"
        local header_printed=false
        local pdf_file="$OUTPUT_DIR/${OUTPUT_BASENAME}-${lang}.pdf"
        local epub_file="$OUTPUT_DIR/${OUTPUT_BASENAME}-${lang}.epub"
        local html_file="$OUTPUT_DIR/${OUTPUT_BASENAME}-${lang}.html"

        if [ "$BUILD_PDF" = true ] && [ -f "$pdf_file" ]; then
            if [ "$header_printed" = false ]; then echo "  [$display]"; header_printed=true; fi
            echo "    📚 PDF:  $pdf_file"
            any_listed=true
        fi
        if [ "$BUILD_EPUB" = true ] && [ -f "$epub_file" ]; then
            if [ "$header_printed" = false ]; then echo "  [$display]"; header_printed=true; fi
            echo "    📖 EPUB: $epub_file"
            any_listed=true
        fi
        if [ "$BUILD_HTML" = true ] && [ -f "$html_file" ]; then
            if [ "$header_printed" = false ]; then echo "  [$display]"; header_printed=true; fi
            echo "    🌐 HTML: $html_file"
            any_listed=true
        fi
    done
    if [ "$any_listed" = false ]; then
        echo "  (no output files were produced)"
    fi

    echo ""
    echo "File structure used:"
    for lang in "${BUILD_LANGS[@]}"; do
        local display
        local chapters_dir
        local appendices_dir
        display="$(get_lang_display "$lang")"
        chapters_dir="$(get_chapters_dir "$lang")"
        appendices_dir="$(get_appendices_dir "$lang")"

        local chapter_files=()
        local appendix_files=()
        while IFS= read -r f; do
            [ -n "$f" ] && chapter_files+=("$f")
        done < <(filter_markdown_files "$chapters_dir" "false")
        while IFS= read -r f; do
            [ -n "$f" ] && appendix_files+=("$f")
        done < <(filter_markdown_files "$appendices_dir" "false")

        local cc=${#chapter_files[@]}
        local ac=${#appendix_files[@]}

        echo "  [$display]"
        echo "    📁 Chapters:   $chapters_dir ($cc files, excluding short files)"
        if [ "$ac" -gt 0 ]; then
            echo "    📁 Appendices: $appendices_dir ($ac files, excluding short files)"
        fi
    done

    echo ""
    echo "Eisvogel template configuration:"
    echo "  🎨 Template: eisvogel.latex"
    echo "  🔧 PDF Engine: XeLaTeX"
    echo "  💻 Code highlighting: enabled with listings package"
    echo "  🔤 C language support: enabled"
    echo "  📖 Book format: enabled with title page"
}

# =============================================================================
# MAIN SCRIPT
# =============================================================================

# Initialize build flags
BUILD_PDF=false
BUILD_EPUB=false
BUILD_HTML=false
BUILD_LANGS=()
SHOW_HELP=false
TEST_DEPS=false

# Parse command-line arguments. Flag names and language codes are both
# case-insensitive; language codes additionally accept '-' or '_' as the
# separator (pt_BR, pt-BR, PT_br, ptbr all resolve to pt_BR).
while [[ $# -gt 0 ]]; do
    arg="$1"
    arg_lower="${arg,,}"

    case "$arg_lower" in
        --pdf)
            BUILD_PDF=true
            shift
            ;;
        --epub)
            BUILD_EPUB=true
            shift
            ;;
        --html)
            BUILD_HTML=true
            shift
            ;;
        --all)
            BUILD_PDF=true
            BUILD_EPUB=true
            BUILD_HTML=true
            shift
            ;;
        --lang=*)
            raw_value="${arg#*=}"
            canonical="$(normalize_lang "$raw_value")"
            if [ -z "$canonical" ]; then
                echo "❌ Unknown language: '$raw_value'"
                echo "   Accepted: en_US, pt_BR, es_ES, or all (case- and separator-insensitive)"
                exit 1
            fi
            if [ "$canonical" = "all" ]; then
                BUILD_LANGS=("${LANGUAGES_ALL[@]}")
            else
                add_lang "$canonical"
            fi
            shift
            ;;
        --lang)
            shift
            if [ $# -eq 0 ]; then
                echo "❌ --lang requires a value (en_US, pt_BR, es_ES, or all)"
                exit 1
            fi
            canonical="$(normalize_lang "$1")"
            if [ -z "$canonical" ]; then
                echo "❌ Unknown language: '$1'"
                echo "   Accepted: en_US, pt_BR, es_ES, or all (case- and separator-insensitive)"
                exit 1
            fi
            if [ "$canonical" = "all" ]; then
                BUILD_LANGS=("${LANGUAGES_ALL[@]}")
            else
                add_lang "$canonical"
            fi
            shift
            ;;
        --test)
            TEST_DEPS=true
            shift
            ;;
        -h|--help)
            SHOW_HELP=true
            shift
            ;;
        *)
            echo "❌ Unknown option: $arg"
            echo "   Run '$0 --help' for usage information"
            echo "   Note: flag names and language codes are case-insensitive"
            exit 1
            ;;
    esac
done

# Help and dependency-test requests short-circuit the build.
if [ "$SHOW_HELP" = true ]; then
    show_help
    exit 0
fi

if [ "$TEST_DEPS" = true ]; then
    test_dependencies
    exit 0
fi

# Apply defaults: no formats requested → build every format.
if [ "$BUILD_PDF" = false ] && [ "$BUILD_EPUB" = false ] && [ "$BUILD_HTML" = false ]; then
    BUILD_PDF=true
    BUILD_EPUB=true
    BUILD_HTML=true
fi

# Apply defaults: no languages requested → build every supported language.
if [ "${#BUILD_LANGS[@]}" -eq 0 ]; then
    BUILD_LANGS=("${LANGUAGES_ALL[@]}")
fi

# Show build configuration
echo "🚀 FreeBSD Device Drivers Book Build System"
echo "============================================="
echo "Build configuration:"
echo "  Formats:"
if [ "$BUILD_PDF"  = true ]; then echo "    📚 PDF:  Enabled"; fi
if [ "$BUILD_EPUB" = true ]; then echo "    📖 EPUB: Enabled"; fi
if [ "$BUILD_HTML" = true ]; then echo "    🌐 HTML: Enabled"; fi
echo "  Languages:"
for lang in "${BUILD_LANGS[@]}"; do
    echo "    🌍 $(get_lang_display "$lang")"
done
echo "  Output directory: $OUTPUT_DIR"
echo ""

# Build each requested language × format combination. Failures are recorded
# but do not stop the loop, so a broken language does not block the others.
build_errors=0

for lang in "${BUILD_LANGS[@]}"; do
    echo "────────────────────────────────────────────"
    echo "🌍 Language: $(get_lang_display "$lang")"
    echo "────────────────────────────────────────────"

    if [ "$BUILD_PDF" = true ]; then
        if build_pdf "$lang"; then
            echo ""
        else
            build_errors=$((build_errors + 1))
        fi
    fi

    if [ "$BUILD_EPUB" = true ]; then
        if build_epub "$lang"; then
            echo ""
        else
            build_errors=$((build_errors + 1))
        fi
    fi

    if [ "$BUILD_HTML" = true ]; then
        if build_html "$lang"; then
            echo ""
        else
            build_errors=$((build_errors + 1))
        fi
    fi
done

# Show build summary
show_build_summary

# Exit with error code if any builds failed
if [ $build_errors -gt 0 ]; then
    echo ""
    echo "❌ Some builds failed. Check the error messages above."
    exit 1
else
    echo ""
    echo "✅ All requested formats built successfully!"
    exit 0
fi
