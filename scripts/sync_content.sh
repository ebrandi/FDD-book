#!/bin/bash

# FDD Book Content Sync Script
# Syncs content from the current project to the website directory

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../content" && pwd)"
TARGET_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../FDD-Book-website/content" && pwd)"

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

# Function to check if rsync is available
check_rsync() {
    if ! command -v rsync &> /dev/null; then
        print_error "rsync is not installed. Please install rsync to use this script."
        exit 1
    fi
}

# Function to check if source directory exists
check_source() {
    if [ ! -d "$SOURCE_DIR" ]; then
        print_error "Source directory does not exist: $SOURCE_DIR"
        exit 1
    fi
}

# Function to create target directory if it doesn't exist
ensure_target() {
    if [ ! -d "$TARGET_DIR" ]; then
        print_warning "Target directory does not exist. Creating: $TARGET_DIR"
        mkdir -p "$TARGET_DIR"
    fi
}

# Function to perform the sync
sync_content() {
    print_status "Starting content synchronization..."
    print_status "Source: $SOURCE_DIR"
    print_status "Target: $TARGET_DIR"
    
    # Use rsync with the following options:
    # -a: archive mode (preserves permissions, timestamps, etc.)
    # -v: verbose output
    # -h: human-readable sizes
    # --delete: remove files in target that don't exist in source
    # --exclude: exclude common files that shouldn't be synced
    rsync -avh --delete \
        --exclude='*.tmp' \
        --exclude='*.swp' \
        --exclude='*.bak' \
        --exclude='.DS_Store' \
        --exclude='Thumbs.db' \
        "$SOURCE_DIR/" "$TARGET_DIR/"
    
    if [ $? -eq 0 ]; then
        print_status "Content synchronization completed successfully!"
    else
        print_error "Content synchronization failed!"
        exit 1
    fi
}

# Function to show usage
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -h, --help     Show this help message"
    echo "  -d, --dry-run  Show what would be synced without actually doing it"
    echo ""
    echo "This script syncs content from the current project's content directory"
    echo "to the FDD-Book-website content directory."
}

# Function to perform dry run
dry_run() {
    print_status "Performing dry run (no files will be modified)..."
    print_status "Source: $SOURCE_DIR"
    print_status "Target: $TARGET_DIR"
    
    rsync -avh --delete --dry-run \
        --exclude='*.tmp' \
        --exclude='*.swp' \
        --exclude='*.bak' \
        --exclude='.DS_Store' \
        --exclude='Thumbs.db' \
        "$SOURCE_DIR/" "$TARGET_DIR/"
}

# Main script logic
main() {
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_usage
                exit 0
                ;;
            -d|--dry-run)
                DRY_RUN=true
                shift
                ;;
            *)
                print_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done
    
    # Check prerequisites
    check_rsync
    check_source
    ensure_target
    
    # Perform sync or dry run
    if [ "$DRY_RUN" = true ]; then
        dry_run
    else
        sync_content
    fi
}

# Run main function with all arguments
main "$@" 