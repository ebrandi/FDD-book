#!/bin/bash

# FDD Book Translation Setup Script
# Creates a new language directory and populates it with content files for translation

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONTENT_DIR="$PROJECT_ROOT/content"
TRANSLATIONS_DIR="$PROJECT_ROOT/translations"

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_header() {
    echo -e "${BLUE}[SETUP]${NC} $1"
}

# Function to validate language code
validate_language_code() {
    local lang_code="$1"
    
    # Check if language code is provided
    if [ -z "$lang_code" ]; then
        print_error "Language code is required."
        return 1
    fi
    
    # Check if language code follows ISO 639-1 format (2 letters) or ISO 639-1 with region (e.g., pt_BR)
    if [[ ! "$lang_code" =~ ^[a-z]{2}(_[A-Z]{2})?$ ]]; then
        print_error "Invalid language code format. Use ISO 639-1 format (e.g., 'en', 'pt', 'es') or with region (e.g., 'pt_BR', 'en_US')."
        return 1
    fi
    
    return 0
}

# Function to check if content directory exists
check_content_directory() {
    if [ ! -d "$CONTENT_DIR" ]; then
        print_error "Content directory does not exist: $CONTENT_DIR"
        exit 1
    fi
}

# Function to create language directory
create_language_directory() {
    local lang_code="$1"
    local lang_dir="$TRANSLATIONS_DIR/$lang_code"
    
    if [ ! -d "$lang_dir" ]; then
        print_status "Creating language directory: $lang_dir"
        mkdir -p "$lang_dir"
    else
        print_warning "Language directory already exists: $lang_dir"
    fi
}

# Global variables for tracking copy results
COPY_COUNT=0
SKIP_COUNT=0

# Function to copy directory structure and files
copy_content_structure() {
    local source_dir="$1"
    local target_dir="$2"
    
    # Reset counters
    COPY_COUNT=0
    SKIP_COUNT=0
    
    # Create target directory if it doesn't exist
    mkdir -p "$target_dir"
    
    # Find all .md files in source directory and copy them
    while IFS= read -r -d '' file; do
        # Get relative path from source directory
        local relative_path="${file#$source_dir/}"
        local target_file="$target_dir/$relative_path"
        local target_file_dir="$(dirname "$target_file")"
        
        # Create target directory if it doesn't exist
        mkdir -p "$target_file_dir"
        
        # Check if target file already exists
        if [ -f "$target_file" ]; then
            print_warning "Skipping existing file: $relative_path"
            ((SKIP_COUNT++))
        else
            print_status "Copying: $relative_path"
            cp "$file" "$target_file"
            ((COPY_COUNT++))
        fi
    done < <(find "$source_dir" -name "*.md" -type f -print0)
}

# Function to show usage
show_usage() {
    echo "Usage: $0 <language_code> [OPTIONS]"
    echo ""
    echo "Arguments:"
    echo "  language_code    ISO 639-1 language code (e.g., 'en', 'pt', 'es')"
    echo "                   or with region (e.g., 'pt_BR', 'en_US')"
    echo ""
    echo "Options:"
    echo "  -h, --help       Show this help message"
    echo "  -f, --force      Force copy even if files already exist"
    echo "  -v, --verbose    Show detailed output"
    echo ""
    echo "Examples:"
    echo "  $0 fr            # Set up French translation"
    echo "  $0 de_DE         # Set up German (Germany) translation"
    echo "  $0 es_MX         # Set up Spanish (Mexico) translation"
    echo ""
    echo "This script creates a new language directory under translations/"
    echo "and populates it with copies of content files for translation work."
}

# Function to get language name from code
get_language_name() {
    local lang_code="$1"
    case "$lang_code" in
        "en") echo "English" ;;
        "pt") echo "Portuguese" ;;
        "pt_BR") echo "Portuguese (Brazil)" ;;
        "es") echo "Spanish" ;;
        "es_MX") echo "Spanish (Mexico)" ;;
        "es_ES") echo "Spanish (Spain)" ;;
        "fr") echo "French" ;;
        "fr_FR") echo "French (France)" ;;
        "de") echo "German" ;;
        "de_DE") echo "German (Germany)" ;;
        "it") echo "Italian" ;;
        "it_IT") echo "Italian (Italy)" ;;
        "ru") echo "Russian" ;;
        "ru_RU") echo "Russian (Russia)" ;;
        "zh") echo "Chinese" ;;
        "zh_CN") echo "Chinese (Simplified)" ;;
        "zh_TW") echo "Chinese (Traditional)" ;;
        "ja") echo "Japanese" ;;
        "ja_JP") echo "Japanese (Japan)" ;;
        "ko") echo "Korean" ;;
        "ko_KR") echo "Korean (South Korea)" ;;
        *) echo "$lang_code" ;;
    esac
}

# Main script logic
main() {
    local language_code=""
    local force_copy=false
    local verbose=false
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_usage
                exit 0
                ;;
            -f|--force)
                force_copy=true
                shift
                ;;
            -v|--verbose)
                verbose=true
                shift
                ;;
            -*)
                print_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
            *)
                if [ -z "$language_code" ]; then
                    language_code="$1"
                else
                    print_error "Multiple language codes provided. Only one is allowed."
                    exit 1
                fi
                shift
                ;;
        esac
    done
    
    # Validate language code
    if ! validate_language_code "$language_code"; then
        show_usage
        exit 1
    fi
    
    # Get language name
    local language_name=$(get_language_name "$language_code")
    
    print_header "Setting up translation for $language_name ($language_code)"
    
    # Check prerequisites
    check_content_directory
    
    # Create language directory
    create_language_directory "$language_code"
    local lang_dir="$TRANSLATIONS_DIR/$language_code"
    
    # Copy content structure
    print_status "Copying content files..."
    copy_content_structure "$CONTENT_DIR" "$lang_dir"
    
    print_status "Translation setup completed!"
    print_status "Language directory: $lang_dir"
    print_status "Files copied: $COPY_COUNT"
    print_status "Files skipped (already exist): $SKIP_COUNT"
    
    if [ "$SKIP_COUNT" -gt 0 ]; then
        print_warning "Some files already existed and were skipped."
        print_warning "Use --force option to overwrite existing files."
    fi
    
    print_status "Translators can now start working on files in: $lang_dir"
}

# Run main function with all arguments
main "$@" 