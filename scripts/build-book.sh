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
BOOK_DATE="DRAFT Version 1.0 - August, 30th 2025"

# File paths
TITLE_FILE="$SCRIPT_DIR/title.md"
METADATA_FILE="$SCRIPT_DIR/metadata.yaml"
OUTPUT_DIR="public/downloads"

# Template and engine
EISVOGEL_TEMPLATE="$HOME/.local/share/pandoc/templates/eisvogel.latex"
PDF_ENGINE="xelatex"

# =============================================================================
# FUNCTIONS
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

show_help() {
    cat << EOF
Usage: $0 [OPTIONS]

Build the FreeBSD Device Drivers book in various formats.

OPTIONS:
    --pdf           Build PDF version only
    --epub          Build EPUB version only  
    --html          Build HTML5 version only
    --all           Build all formats (PDF, EPUB, HTML5)
    --test          Test if all dependencies are properly installed
    -h, --help      Show this help message

NOTE: All options are case-insensitive (--PDF, --Pdf, --pdf all work the same)

EXAMPLES:
    $0 --pdf                    # Build PDF only
    $0 --epub --html           # Build EPUB and HTML5
    $0 --all                   # Build all formats
    $0 --test                  # Test dependencies
    $0 --PDF                   # Same as --pdf
    $0 --EPUB --HTML           # Same as --epub --html

OUTPUT DIRECTORY:
    Generated files will be placed in: $OUTPUT_DIR/
    
    - PDF:  $OUTPUT_DIR/freebsd-device-drivers.pdf
    - EPUB: $OUTPUT_DIR/freebsd-device-drivers.epub
    - HTML: $OUTPUT_DIR/freebsd-device-drivers.html

REQUIREMENTS:
    - Pandoc 3.0+
    - XeLaTeX (texlive-xetex)
    - Eisvogel template at: $EISVOGEL_TEMPLATE
    - Required fonts and LaTeX packages

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
    
    # Test content files
    echo ""
    echo "5. Testing content files..."
    local chapter_files_filtered=()
    local appendix_files_filtered=()
    
    # Read filtered files into arrays
    while IFS= read -r file; do
        if [ -n "$file" ]; then
            chapter_files_filtered+=("$file")
        fi
    done < <(filter_markdown_files "content/chapters")
    
    while IFS= read -r file; do
        if [ -n "$file" ]; then
            appendix_files_filtered+=("$file")
        fi
    done < <(filter_markdown_files "content/appendices")
    
    local chapter_count=${#chapter_files_filtered[@]}
    local appendix_count=${#appendix_files_filtered[@]}
    
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
    
    if [ "$chapter_count" -gt 0 ]; then
        echo "   ✓ Chapters found: $chapter_count files (excluding short files < 20 lines)"
    else
        echo "   ✗ No valid chapter files found in content/chapters/ (all files have < 20 lines)"
        all_good=false
    fi
    
    if [ "$appendix_count" -gt 0 ]; then
        echo "   ✓ Appendices found: $appendix_count files (excluding short files < 20 lines)"
    else
        echo "   ⚠ No valid appendix files found in content/appendices/ (all files have < 20 lines)"
    fi
    
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

build_pdf() {
    echo "📚 Building PDF: $OUTPUT_DIR/freebsd-device-drivers.pdf"
    
    # Ensure output directory exists
    mkdir -p "$OUTPUT_DIR"
    
    # Collect all markdown files (filter out files with less than 20 lines)
    local title_file="$TITLE_FILE"
    local chapter_files=()
    local appendix_files=()
    
    # Read filtered files into arrays (suppress warnings during build)
    while IFS= read -r file; do
        if [ -n "$file" ]; then
            chapter_files+=("$file")
        fi
    done < <(filter_markdown_files "content/chapters" "false")
    
    while IFS= read -r file; do
        if [ -n "$file" ]; then
            appendix_files+=("$file")
        fi
    done < <(filter_markdown_files "content/appendices" "false")
    
    # Combine all files
    local all_files=("$title_file" "${chapter_files[@]}" "${appendix_files[@]}")
    local total_files=${#all_files[@]}
    
    echo "   Including files: $total_files total (1 title + ${#chapter_files[@]} chapters + ${#appendix_files[@]} appendices)"
    
    # Build PDF with Eisvogel template
    echo "   Running pandoc with Eisvogel template..."
    
    pandoc "${all_files[@]}" \
        --metadata-file="$METADATA_FILE" \
        --template eisvogel \
        --pdf-engine=xelatex \
        --from markdown+fenced_code_blocks \
        --toc \
        --toc-depth=2 \
        --number-sections \
        --metadata title="$BOOK_TITLE" \
        --metadata author="$BOOK_AUTHOR" \
        --metadata date="$BOOK_DATE" \
        --variable titlepage=true \
        --variable toc-own-page=true \
        --variable graphics=true \
        --variable papersize=a4 \
        --variable documentclass=book \
        --variable book=true \
        --variable code-block-font-size=\\footnotesize \
        --variable float-placement-figure="H" \
        --variable figure-placement="H" \
        --highlight-style=tango \
        --variable linestretch=1.15 \
        --variable geometry:"inner=2cm,outer=2cm,top=2.5cm,bottom=2.5cm" \
        -o "$OUTPUT_DIR/freebsd-device-drivers.pdf" 2>&1
    
    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        echo "   ✓ PDF successfully generated: $OUTPUT_DIR/freebsd-device-drivers.pdf"
        local file_size=$(du -h "$OUTPUT_DIR/freebsd-device-drivers.pdf" | cut -f1)
        echo "   ✓ PDF file size: $file_size"
    else
        echo "   ✗ PDF generation failed (exit code: $exit_code)"
        return $exit_code
    fi
}

build_epub() {
    echo "📖 Building EPUB: $OUTPUT_DIR/freebsd-device-drivers.epub"
    
    # Ensure output directory exists
    mkdir -p "$OUTPUT_DIR"
    
    # Collect all markdown files (filter out files with less than 20 lines)
    local title_file="$TITLE_FILE"
    local chapter_files=()
    local appendix_files=()
    
    # Read filtered files into arrays (suppress warnings during build)
    while IFS= read -r file; do
        if [ -n "$file" ]; then
            chapter_files+=("$file")
        fi
    done < <(filter_markdown_files "content/chapters" "false")
    
    while IFS= read -r file; do
        if [ -n "$file" ]; then
            appendix_files+=("$file")
        fi
    done < <(filter_markdown_files "content/appendices" "false")
    
    # Combine all files
    local all_files=("$title_file" "${chapter_files[@]}" "${appendix_files[@]}")
    
    # Build EPUB
    pandoc "${all_files[@]}" \
        --metadata-file="$METADATA_FILE" \
        --metadata title="$BOOK_TITLE" \
        --metadata author="$BOOK_AUTHOR" \
        --metadata date="$BOOK_DATE" \
        --toc \
        --toc-depth=2 \
        --number-sections \
        --highlight-style=tango \
        --epub-chapter-level=1 \
        --verbose \
        -o "$OUTPUT_DIR/freebsd-device-drivers.epub" 2>&1
    
    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        echo "   ✓ EPUB successfully generated: $OUTPUT_DIR/freebsd-device-drivers.epub"
        local file_size=$(du -h "$OUTPUT_DIR/freebsd-device-drivers.epub" | cut -f1)
        echo "   ✓ EPUB file size: $file_size"
    else
        echo "   ✗ EPUB generation failed (exit code: $exit_code)"
        return $exit_code
    fi
}

build_html() {
    echo "🌐 Building HTML5: $OUTPUT_DIR/freebsd-device-drivers.html"
    
    # Ensure output directory exists
    mkdir -p "$OUTPUT_DIR"
    
    # Collect all markdown files (filter out files with less than 20 lines)
    local title_file="$TITLE_FILE"
    local chapter_files=()
    local appendix_files=()
    
    # Read filtered files into arrays (suppress warnings during build)
    while IFS= read -r file; do
        if [ -n "$file" ]; then
            chapter_files+=("$file")
        fi
    done < <(filter_markdown_files "content/chapters" "false")
    
    while IFS= read -r file; do
        if [ -n "$file" ]; then
            appendix_files+=("$file")
        fi
    done < <(filter_markdown_files "content/appendices" "false")
    
    # Combine all files
    local all_files=("$title_file" "${chapter_files[@]}" "${appendix_files[@]}")
    
    # Debug: Show what files will be processed
    echo "   Files to process:"
    echo "     Title: $title_file"
    echo "     Chapters: ${#chapter_files[@]} files"
    echo "     Appendices: ${#appendix_files[@]} files"
    echo "     Total: ${#all_files[@]} files"
    
    # Use Pandoc's default CSS for better code highlighting
    echo "   Using Pandoc's default CSS for better code highlighting"
    
    # Build HTML5
    pandoc "${all_files[@]}" \
        --metadata-file="$METADATA_FILE" \
        --to=html5 \
        --standalone \
        --embed-resources \
        --highlight-style=tango \
        --toc \
        --toc-depth=2 \
        --number-sections \
        --metadata title="$BOOK_TITLE" \
        --metadata author="$BOOK_AUTHOR" \
        --metadata date="$BOOK_DATE" \
        -o "$OUTPUT_DIR/freebsd-device-drivers.html" 2>&1
    
    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        echo "   ✓ HTML5 successfully generated: $OUTPUT_DIR/freebsd-device-drivers.html"
        local file_size=$(du -h "$OUTPUT_DIR/freebsd-device-drivers.html" | cut -f1)
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
    
    if [ "$BUILD_PDF" = true ] && [ -f "$OUTPUT_DIR/freebsd-device-drivers.pdf" ]; then
        echo "  📚 PDF:  $OUTPUT_DIR/freebsd-device-drivers.pdf"
    fi
    
    if [ "$BUILD_EPUB" = true ] && [ -f "$OUTPUT_DIR/freebsd-device-drivers.epub" ]; then
        echo "  📖 EPUB: $OUTPUT_DIR/freebsd-device-drivers.epub"
    fi
    
    if [ "$BUILD_HTML" = true ] && [ -f "$OUTPUT_DIR/freebsd-device-drivers.html" ]; then
        echo "  🌐 HTML: $OUTPUT_DIR/freebsd-device-drivers.html"
    fi
    
    echo ""
    echo "File structure used:"
    local chapter_files_filtered=()
    local appendix_files_filtered=()
    
    # Read filtered files into arrays (suppress warnings during summary)
    while IFS= read -r file; do
        if [ -n "$file" ]; then
            chapter_files_filtered+=("$file")
        fi
    done < <(filter_markdown_files "content/chapters" "false")
    
    while IFS= read -r file; do
        if [ -n "$file" ]; then
            appendix_files_filtered+=("$file")
        fi
    done < <(filter_markdown_files "content/appendices" "false")
    
    local chapter_count=${#chapter_files_filtered[@]}
    local appendix_count=${#appendix_files_filtered[@]}
    echo "  📁 Chapters: content/chapters/ ($chapter_count files, excluding short files)"
    if [ "$appendix_count" -gt 0 ]; then
        echo "  📁 Appendices: content/appendices/ ($appendix_count files, excluding short files)"
    fi
    
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
SHOW_HELP=false
TEST_DEPS=false

# Parse command line arguments (case-insensitive)
while [[ $# -gt 0 ]]; do
    # Convert argument to lowercase for case-insensitive comparison
    arg_lower="${1,,}"
    
    case $arg_lower in
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
        --test)
            TEST_DEPS=true
            shift
            ;;
        -h|--help)
            SHOW_HELP=true
            shift
            ;;
        *)
            echo "❌ Unknown option: $1"
            echo "   Run '$0 --help' for usage information"
            echo "   Note: Options are case-insensitive (e.g., --PDF, --Pdf, --pdf all work)"
            exit 1
            ;;
    esac
done

# Show help if requested or no options specified
if [ "$SHOW_HELP" = true ] || ([ "$BUILD_PDF" = false ] && [ "$BUILD_EPUB" = false ] && [ "$BUILD_HTML" = false ] && [ "$TEST_DEPS" = false ]); then
    show_help
    exit 0
fi

# Test dependencies if requested
if [ "$TEST_DEPS" = true ]; then
    test_dependencies
    exit 0
fi

# Show build configuration
echo "🚀 FreeBSD Device Drivers Book Build System"
echo "============================================="
echo "Build configuration:"
if [ "$BUILD_PDF" = true ]; then echo "  📚 PDF:  Enabled"; fi
if [ "$BUILD_EPUB" = true ]; then echo "  📖 EPUB: Enabled"; fi
if [ "$BUILD_HTML" = true ]; then echo "  🌐 HTML: Enabled"; fi
echo "Output directory: $OUTPUT_DIR"
echo ""

# Build requested formats
build_errors=0

if [ "$BUILD_PDF" = true ]; then
    if build_pdf; then
        echo ""
    else
        build_errors=$((build_errors + 1))
    fi
fi

if [ "$BUILD_EPUB" = true ]; then
    if build_epub; then
        echo ""
    else
        build_errors=$((build_errors + 1))
    fi
fi

if [ "$BUILD_HTML" = true ]; then
    if build_html; then
        echo ""
    else
        build_errors=$((build_errors + 1))
    fi
fi

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
