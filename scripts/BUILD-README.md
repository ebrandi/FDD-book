# FreeBSD Device Drivers Book - Build System

This document explains how to build the FreeBSD Device Drivers book using the updated build system with the Eisvogel pandoc LaTeX template.

> [!WARNING]
>
> ## Note on my build environment    
>
> I'm writing this book on a Windows machine using Typora (https://typora.io/), and I'm building the book's PDF and EPUB files on the same Windows laptop via WSL (Windows Subsystem for Linux), which is running Ubuntu. 
>
> The Markdown sources are converted to PDF and EPUB using standard open-source tools within that environment. I don’t foresee any problems with this setup; it’s simply the most convenient way for me to work at the moment. 
>
> My priority is finishing the content, so I’m using the resources that make life simpler; there’s no room for ideological battles.    
>
> If you encounter any platform-specific issues, please open an issue with details, and I’ll take a look. If this approach isn’t to your taste, I’m afraid I won’t be complicating my workflow to accommodate preferences; I’d rather spend that time on the book. 
>
> **Development Priority**:  My current focus is on completing the book content. Technical improvements to the build system and file format optimizations will be addressed after the book writing is finished to maintain focus and writing momentum.
>
> Thank you
>
> Edson

## Prerequisites

This section provides complete installation instructions for Ubuntu 24.04. All commands assume a fresh system with no pre-installed dependencies.

**Note**: Throughout this document, replace `~/your-project-directory` with the actual path to your project directory.

### System Requirements

- **Operating System**: Ubuntu 24.04 LTS (or later)
- **Architecture**: x86_64 (64-bit)
- **Disk Space**: At least 8GB free space (for full LaTeX installation)
- **Memory**: At least 4GB RAM recommended
- **Internet**: Stable internet connection for downloading packages

### Required Software

#### 1. **System Updates and Basic Tools**

First, ensure your system is up to date and install essential build tools:

```bash
# Update package list and upgrade existing packages
sudo apt update && sudo apt upgrade -y

# Install essential build tools and utilities
sudo apt install -y build-essential wget curl git unzip

# Install additional system utilities
sudo apt install -y software-properties-common apt-transport-https ca-certificates
```

#### 2. **Pandoc Installation** (version 3.0 or later)

```bash
# Download the latest pandoc version for Ubuntu
cd /tmp
wget https://github.com/jgm/pandoc/releases/latest/download/pandoc-3.1.11-1-amd64.deb

# Install the downloaded package
sudo dpkg -i pandoc-3.1.11-1-amd64.deb

# If you get dependency errors, fix them
sudo apt --fix-broken install -y

# Verify installation
pandoc --version

# Clean up
rm -f pandoc-3.1.11-1-amd64.deb
```

#### 3. **LaTeX Distribution Installation** (with XeLaTeX support)

**Full Installation (Recommended for production use - includes everything):**
```bash
# Install the complete TeX Live distribution
sudo apt install -y texlive-full

# This includes all packages, fonts, and engines you might need
# Note: This will download and install approximately 6-8GB of packages
```

**Minimal Installation (if you prefer smaller footprint):**
```bash
# Install essential LaTeX base packages
sudo apt install -y texlive-base texlive-binaries texlive-latex-base

# Install XeLaTeX engine (required for Eisvogel template)
sudo apt install -y texlive-xetex

# Install additional required LaTeX packages
sudo apt install -y texlive-latex-extra texlive-latex-recommended

# Install font packages (essential for proper typography)
sudo apt install -y texlive-fonts-recommended texlive-fonts-extra

# Install additional useful packages
sudo apt install -y texlive-science texlive-publishers

# Install packages for code highlighting and listings
sudo apt install -y texlive-listings texlive-minted

# Install packages for tables and graphics
sudo apt install -y texlive-booktabs texlive-array texlive-tools

# Install packages for mathematical typesetting
sudo apt install -y texlive-amsmath texlive-amssymb texlive-amsthm

# Install packages for bibliography
sudo apt install -y texlive-bibtex-extra texlive-biblatex
```

#### 4. **Font Installation** (Essential for Professional Typography)

**System and Professional Fonts:**
```bash
# Install Liberation fonts (open source alternative to Arial, Times, Courier)
sudo apt install -y fonts-liberation fonts-liberation-sans fonts-liberation-serif fonts-liberation-mono

# Install DejaVu fonts (high-quality open source fonts)
sudo apt install -y fonts-dejavu fonts-dejavu-core fonts-dejavu-extra

# Install Ubuntu system fonts
sudo apt install -y fonts-ubuntu fonts-ubuntu-title fonts-ubuntu-ui

# Install additional professional fonts
sudo apt install -y fonts-freefont-ttf fonts-liberation2 fonts-noto
```

**Windows Fonts (Times New Roman, Arial, Courier):**
```bash
# Install Microsoft TrueType fonts including Times New Roman, Arial, and Courier
sudo apt install -y fonts-microsoft fonts-microsoft-core fonts-microsoft-web

# Alternative: Install ttf-mscorefonts-installer for additional Microsoft fonts
sudo apt install -y ttf-mscorefonts-installer

# If the above package is not available, manually install Microsoft fonts
# Download and install the Microsoft Core Fonts package
cd /tmp
wget https://downloads.sourceforge.net/corefonts/comic32.exe
wget https://downloads.sourceforge.net/corefonts/arial32.exe
wget https://downloads.sourceforge.net/corefonts/arialb32.exe
wget https://downloads.sourceforge.net/corefonts/comic32.exe
wget https://downloads.sourceforge.net/corefonts/courie32.exe
wget https://downloads.sourceforge.net/corefonts/georgi32.exe
wget https://downloads.sourceforge.net/corefonts/impact32.exe
wget https://downloads.sourceforge.net/corefonts/times32.exe
wget https://downloads.sourceforge.net/corefonts/trebuc32.exe
wget https://downloads.sourceforge.net/corefonts/verdan32.exe
wget https://downloads.sourceforge.net/corefonts/webdin32.exe

# Install cabextract to extract Windows font files
sudo apt install -y cabextract

# Extract the fonts
cabextract *.exe

# Create Microsoft fonts directory
sudo mkdir -p /usr/share/fonts/truetype/msttcorefonts

# Move fonts to the directory
sudo mv *.ttf /usr/share/fonts/truetype/msttcorefonts/

# Set proper permissions
sudo chmod 644 /usr/share/fonts/truetype/msttcorefonts/*.ttf

# Update font cache
sudo fc-cache -fv

# Clean up temporary files
rm -f *.exe *.ttf
```

**LaTeX-Specific Fonts:**
```bash
# Install Latin Modern fonts (LaTeX standard)
sudo apt install -y texlive-fonts-recommended

# Install additional LaTeX font packages
sudo apt install -y texlive-fonts-extra

# Install Computer Modern fonts (LaTeX default)
sudo apt install -y texlive-fonts-extra texlive-fonts-recommended
```

**CJK Fonts (if you need Asian language support):**
```bash
# Install Chinese, Japanese, Korean fonts
sudo apt install -y fonts-noto-cjk fonts-noto-cjk-extra

# Install additional CJK fonts
sudo apt install -y fonts-ipafont fonts-ipafont-gothic fonts-ipafont-mincho

# Install Korean fonts
sudo apt install -y fonts-nanum fonts-nanum-coding fonts-nanum-extra
```

#### 5. **Eisvogel Template Installation**

The Eisvogel template is a [pandoc LaTeX template](https://github.com/Wandmalfarbe/pandoc-latex-template) that provides professional PDF formatting.

**Step-by-Step Installation:**
```bash
# Create pandoc templates directory in your home folder
mkdir -p ~/.local/share/pandoc/templates

# Navigate to the templates directory
cd ~/.local/share/pandoc/templates

# Download the latest Eisvogel template (version 3.2.0)
wget https://github.com/Wandmalfarbe/pandoc-latex-template/releases/download/v3.2.0/Eisvogel-3.2.0.tar.gz

# Extract the downloaded archive
tar -xzf Eisvogel-3.2.0.tar.gz

# Copy the main template file to the templates directory
cp Eisvogel-3.2.0/eisvogel.latex ./

# Clean up temporary files
rm -rf Eisvogel-3.2.0 Eisvogel-3.2.0.tar.gz

# Verify the template is properly installed
ls -la eisvogel.latex

# Check template content (should show LaTeX code, not HTML)
head -5 eisvogel.latex
```

#### 6. **Additional Required Utilities**

```bash
# Install utilities for PDF processing and information
sudo apt install -y poppler-utils

# Install additional system utilities that might be needed
sudo apt install -y ghostscript

# Install utilities for image processing (if you have images in your documents)
sudo apt install -y imagemagick

# Install utilities for font management
sudo apt install -y fontconfig
```

### Optional Software

**Version Control and Development Tools:**
```bash
# Install Git for version control
sudo apt install -y git

# Install Python for additional pandoc filters (if needed)
sudo apt install -y python3 python3-pip

# Install additional development tools
sudo apt install -y make cmake
```

**Additional LaTeX Tools:**
```bash
# Install LaTeX build tools
sudo apt install -y latexmk

# Install additional LaTeX utilities
sudo apt install -y texlive-extra-utils
```

### Post-Installation Verification

After completing all installations, verify that all components are working correctly:

```bash
# Check pandoc installation and version
echo "=== Pandoc Check ==="
pandoc --version

# Check XeLaTeX installation and version
echo "=== XeLaTeX Check ==="
xelatex --version

# Check Eisvogel template installation
echo "=== Eisvogel Template Check ==="
ls -la ~/.local/share/pandoc/templates/eisvogel.latex

# Check if the template file contains LaTeX code (not HTML)
echo "=== Template Content Check ==="
head -3 ~/.local/share/pandoc/templates/eisvogel.latex

# Test basic LaTeX compilation
echo "=== LaTeX Compilation Test ==="
cat > test.tex << 'EOF'
\documentclass{article}
\begin{document}
Hello World! This is a test of LaTeX compilation.
\end{document}
EOF

# Compile the test document
xelatex test.tex

# Check if PDF was created
if [ -f test.pdf ]; then
    echo "✓ LaTeX compilation successful - test.pdf created"
    ls -la test.pdf
else
    echo "✗ LaTeX compilation failed"
fi

# Clean up test files
rm -f test.tex test.pdf test.log test.aux

# Update font cache
echo "=== Font Cache Update ==="
sudo fc-cache -fv

# Verify Windows fonts are available
echo "=== Windows Fonts Verification ==="
fc-list | grep -i "times new roman" | head -1
fc-list | grep -i "arial" | head -1
fc-list | grep -i "courier" | head -1

echo "=== Installation Verification Complete ==="
echo "If all checks passed, your system is ready to build the book!"
echo ""
echo "Note: If Windows fonts are not showing up, you may need to restart your system"
echo "or run 'sudo fc-cache -fv' again to ensure the font cache is updated."
```

## File Structure

```
~/FDD-book/
├── content/
│   ├── chapters/
│   │   ├── part1/
│   │   │   ├── chapter-01.md
│   │   │   ├── chapter-02.md
│   │   │   └── ...
│   │   ├── part2/
│   │   └── ...
│   └── appendices/
│       ├── appendix-a.md
│       ├── appendix-b.md
│       └── ...
├── scripts/
│   ├── build-book.sh        # Main build script (updated for Eisvogel)
│   ├── metadata.yaml        # Book metadata and Eisvogel configuration
│   └── title.md             # Book title and introduction
├── public/
    └── downloads/           # Output directory for generated files
```

## Building the Book

### Build the Full Book

To build the complete book, run the script from the root directory:

```bash
# From the root directory (~/FDD-book/)
./scripts/build-book.sh
```

**Note**: All command-line options are case-insensitive. You can use `--PDF`, `--Pdf`, `--pdf`, or any mixed case variation.

The script will:
1. Check all prerequisites
2. Discover all chapters and appendices automatically
3. Build the PDF using the Eisvogel template
4. Optionally build an EPUB version
5. Optionally build an HTML5 version
6. Place output files in the `public/downloads/` directory

### Build Options

The build script supports various command-line options:

```bash
# Build specific formats
./scripts/build-book.sh --pdf          # Build PDF only
./scripts/build-book.sh --epub         # Build EPUB only
./scripts/build-book.sh --html         # Build HTML5 only

# Build combinations
./scripts/build-book.sh --pdf --html   # Build PDF and HTML5
./scripts/build-book.sh --epub --html  # Build EPUB and HTML5

# Build all formats
./scripts/build-book.sh --all

# Test dependencies
./scripts/build-book.sh --test

# Show help
./scripts/build-book.sh --help
```

**Case-Insensitive Options**: All options work with any case combination (e.g., `--PDF`, `--Pdf`, `--pdf` all work the same).

### Content Filtering

The build system automatically filters out markdown files with less than 20 lines to exclude incomplete or placeholder content:
- **Minimum requirement**: 20 lines per file
- **Automatic detection**: Files are scanned and filtered during build
- **No manual intervention**: Filtering happens transparently
- **Build summary**: Shows actual number of files processed

### Output Files

- **PDF**: `public/downloads/freebsd-device-drivers.pdf`
- **EPUB**: `public/downloads/freebsd-device-drivers.epub` (if requested)
- **HTML5**: `public/downloads/freebsd-device-drivers.html` (if requested)

## Eisvogel Template Features

### Code Highlighting

The template automatically provides syntax highlighting for:
- **C/C++ code** (primary language for device drivers)
- **Shell scripts** (for build and installation commands)
- **Makefiles** (for kernel module compilation)
- **Configuration files** (for system setup)

### Book Formatting

- **Title page** with custom colors and styling
- **Table of contents** with proper page breaks
- **Chapter numbering** and section organization
- **Professional typography** optimized for technical content
- **Proper margins** and spacing for readability

### Image Handling

**Automatic Image Sizing**: All images are automatically resized to fit within page boundaries using the `header-includes` configuration in `scripts/metadata.yaml`:

```yaml
header-includes: |
  \usepackage{graphicx}
  \setkeys{Gin}{width=0.85\linewidth,keepaspectratio}
```

This ensures:
- **No image bleeding** out of page margins
- **Consistent sizing** across all chapters
- **Aspect ratio maintained** - no distortion
- **No manual sizing needed** in markdown files

### Customization

The template behavior can be customized through `scripts/metadata.yaml`:

```yaml
# Title page settings
titlepage: true
titlepage-color: "2C3E50"        # Blue theme
titlepage-text-color: "FFFFFF"    # White text
titlepage-rule-color: "E74C3C"    # Red accent

# Code block settings
listings-disable-line-numbers: false
listings-no-page-break: true
code-block-font-size: "\small"

# Book format
book: true
toc-own-page: true

# Image handling (automatic sizing)
header-includes: |
  \usepackage{graphicx}
  \setkeys{Gin}{width=0.85\linewidth,keepaspectratio}
```

## Troubleshooting

### Common Issues

1. **LaTeX Package Missing**
   ```bash
   sudo apt-get install texlive-full
   ```

2. **Eisvogel Template Not Found**
   - Verify the template is in `~/.local/share/pandoc/templates/`
   - Check file permissions: `ls -la ~/.local/share/pandoc/templates/eisvogel.latex`
   - Reinstall if needed using the installation instructions above

3. **XeLaTeX Not Available**
   ```bash
   sudo apt-get install texlive-xetex
   ```

4. **Memory Issues with Large Documents**
   - The script uses XeLaTeX which handles large documents better
   - Consider building in parts if issues persist
   - Increase system memory if possible

5. **Font Issues**
   ```bash
   # Update font cache
   sudo fc-cache -fv
   
   # Reinstall font packages
   sudo apt install --reinstall texlive-fonts-recommended texlive-fonts-extra
   ```

### Eisvogel Template Specific Issues

#### **Missing endcsname inserted or File not found errors**
These errors occur when using `titlepage-background`, `logo`, or `titlepage-logo` with filenames containing underscores.

**Solution:**
```bash
# Replace underscores with hyphens in image filenames
mv "background_image.pdf" "background-image.pdf"
mv "logo_image.pdf" "logo-image.pdf"
```

**Alternative: Use LaTeX escaping in YAML:**
```yaml
titlepage-background: "`background_image.pdf`{=latex}"
logo: "`logo_image.pdf`{=latex}"
```

#### **Missing \begin{document} error**
This indicates you're using the wrong template file. Ensure you have the correct Eisvogel template:
```bash
# Verify template location and content
ls -la ~/.local/share/pandoc/templates/eisvogel.latex
head -10 ~/.local/share/pandoc/templates/eisvogel.latex
```

#### **Auto expansion font errors (Windows/MiKTeX)**
```bash
# Navigate to MiKTeX bin directory and run updmap
cd "C:\Program Files\MiKTeX 2.9\miktex\bin\x64"
updmap.exe
```

#### **Cannot find image file errors**
- Check all image references and filenames for correctness
- Ensure images are in the correct relative paths
- Update to the latest Eisvogel version
- Update your LaTeX distribution

### Debug Mode

The build script provides verbose output and helpful error messages. If you encounter issues:

1. Check the error messages in the terminal
2. Verify all required files exist in the `scripts/` directory
3. Check LaTeX installation with `xelatex --version`
4. Test the Eisvogel template with a simple document:

```bash
# Create a test document
cat > test.md << 'EOF'
---
title: "Test Document"
author: "Test Author"
date: "2025-01-01"
---

# Test Chapter

This is a test.

```c
#include <stdio.h>
int main() { return 0; }
EOF
```
# Test with Eisvogel template
pandoc test.md -o test.pdf --template=eisvogel --pdf-engine=xelatex

# Clean up
rm test.md test.pdf
```

## Maintenance and Updates

### Updating the Eisvogel Template

To keep your Eisvogel template up to date:

```bash
# Check current version
cd ~/.local/share/pandoc/templates
ls -la eisvogel.latex

# Backup current template
cp eisvogel.latex eisvogel.latex.backup

# Download latest version
wget https://github.com/Wandmalfarbe/pandoc-latex-template/releases/latest/download/Eisvogel-3.2.0.tar.gz

# Extract and install
tar -xzf Eisvogel-3.2.0.tar.gz
cp Eisvogel-3.2.0/eisvogel.latex ./

# Clean up
rm -rf Eisvogel-3.2.0 Eisvogel-3.2.0.tar.gz

# Test the new template
cd ~/your-project-directory
./scripts/build-book.sh
```

### Updating LaTeX Packages

```bash
# Update TeX Live packages
sudo apt update
sudo apt upgrade texlive-*

# Update font packages
sudo apt upgrade fonts-* texlive-fonts-*
```

### System Maintenance

```bash
# Update font cache
sudo fc-cache -fv

# Clean LaTeX temporary files
find . -name "*.aux" -delete
find . -name "*.log" -delete
find . -name "*.out" -delete
find . -name "*.toc" -delete
find . -name "*.fdb_latexmk" -delete
find . -name "*.fls" -delete
find . -name "*.synctex.gz" -delete
```

## Code Block Formatting

### C Language Code

```c
#include <sys/param.h>
#include <sys/kernel.h>

static int
example_function(void)
{
    return 0;
}
```

### Shell Commands

```sh
# Build the kernel module
make
kldload ./example.ko
```

### Makefiles

```makefile
KMOD=   example
SRCS=   example.c

.include <bsd.kmod.mk>
```

## HTML5 Generation

While the Eisvogel template is designed specifically for LaTeX/PDF generation, Pandoc can also generate professional HTML5 versions of your book. This provides an alternative format that's easy to share, view in web browsers, and can be hosted online.

### HTML5 Features

- **Professional Styling**: Pandoc default CSS provides clean, professional appearance
- **Tango-style Code Highlighting**: Excellent syntax highlighting for C language code using the Tango theme
- **Code Block Styling**: Grey background boxes for better code readability
- **Embedded Resources**: Single HTML file with embedded CSS and JavaScript
- **Cross-platform**: Opens in any modern web browser
- **Searchable**: Full-text search within the document
- **Accessible**: Better support for screen readers and accessibility tools

### HTML5 Styling

The build system uses Pandoc's default CSS for optimal code highlighting:

- **Default Pandoc Styling**: Clean, professional appearance using Pandoc's built-in CSS
- **Code Highlighting**: Excellent C language syntax highlighting with grey boxes for code blocks
- **Typography**: Professional typography optimized for technical documentation
- **Code Blocks**: Grey background boxes for better code readability
- **Cross-browser**: Consistent appearance across all modern browsers
- **Optimized**: Specifically designed for technical documents with code

### HTML5 Generation Command

To generate HTML5 manually (without the build script):

```bash
# From the root directory
pandoc $(find content/ -name "*.md" | sort) \
    --metadata-file=scripts/metadata.yaml \
    --to=html5 \
    --standalone \
    --embed-resources \
    --highlight-style=tango \
    --toc \
    --toc-depth=2 \
    --number-sections \
    --metadata title="FreeBSD Device Drivers" \
    --metadata author="Edson Brandi" \
    --metadata date="DRAFT Version 1.0 - August, 30th 2025" \
    -o public/downloads/freebsd-device-drivers.html

**Note**: The `--highlight-style=tango` option provides excellent C language syntax highlighting. Other available styles include: `pygments`, `kate`, `espresso`, `zenburn`, `monochrome`, `breezedark`, and `haddock`. You can list all available styles with `pandoc --list-highlight-styles`.
```

### HTML5 vs PDF Comparison

| Feature | PDF (Eisvogel) | HTML5 |
|---------|----------------|-------|
| **Code Highlighting** | LaTeX listings package | Tango-style highlighting |
| **Typography** | Professional LaTeX fonts | Web-optimized fonts |
| **Page Layout** | Fixed page boundaries | Fluid, responsive layout |
| **Cross-platform** | Requires PDF reader | Works in any browser |
| **File Size** | Smaller, optimized | Larger due to embedded resources |
| **Print Quality** | Excellent | Good (print-optimized CSS) |
| **Sharing** | Download and open | Can be hosted online |
| **Search** | Basic text search | Full-text search with browser tools |

## Performance Tips

1. **First Build**: The first build may take longer as LaTeX caches are created
2. **Subsequent Builds**: Much faster due to caching
3. **Large Documents**: The Eisvogel template is optimized for large technical documents
4. **Memory**: XeLaTeX handles memory more efficiently than pdfLaTeX

## Support

If you encounter issues:

1. Check the troubleshooting section above
2. Verify all prerequisites are installed
3. Check the Eisvogel documentation in `Eisvogel-3.2.0/README.md`

The build system is designed to be robust and provide clear error messages to help resolve any issues quickly.

## Known Issues and Limitations

This section documents known problems and limitations that exist in the current version of the book content and build system. These issues are being tracked for future improvements.

### 1. **ASCII Diagram Rendering Issues**

**Problem**: ASCII diagrams in the markdown files are not being rendered correctly in the generated PDF. The issue appears to be related to the Eisvogel template's handling of ASCII art and monospace text formatting.

**Symptoms**:
- ASCII diagrams appear as plain text without proper spacing
- Box-drawing characters may not align correctly
- Diagrams may break across page boundaries inappropriately

### 2. **Code Block Page Overflow**

**Problem**: Code blocks with long content (exceeding one page) are not being rendered properly and their content bleeds across page boundaries, making the final PDF difficult to read.

**Symptoms**:
- Code blocks extend beyond page margins
- Long functions or code sections are split awkwardly across pages
- Content appears cut off or overlapping

### 3. **Image Scaling and Placement Limitations**

**Problem**: While the current image handling prevents images from bleeding out of pages, there are limitations in fine-tuning image placement and scaling.

**Current Limitations**:
- Images are automatically scaled to 85% of line width
- Limited control over image positioning within text flow
- No automatic page break optimization for image-heavy sections

### 4. **Template Compatibility Issues**

**Problem**: Some advanced LaTeX features and customizations may conflict with the Eisvogel template's internal structure.

**Known Conflicts**:
- Custom page header/footer modifications
- Complex table formatting
- Advanced mathematical notation
- Custom bibliography styles

### 5. **Performance and Build Time**

**Problem**: Large documents with many images and code blocks can have extended build times and memory usage.

**Current Limitations**:
- Build time increases significantly with document size
- Memory usage can be high for complex documents
- Some LaTeX warnings may appear during compilation

### 6. **Font and Typography Constraints**

**Problem**: While the system supports multiple fonts, there are limitations in font switching and advanced typography features.

**Current Constraints**:
- Limited font fallback options
- Some special characters may not render correctly
- Font scaling is limited to predefined options

## Future Improvements

**Planned Enhancements**:
- Investigate better ASCII diagram rendering solutions
- Implement automatic code block page break detection
- Add more flexible image placement options
- Improve template customization capabilities
- Optimize build performance for large documents

**Contributing**:
- Report new issues with detailed reproduction steps
- Test proposed solutions on different systems
- Contribute improvements to the build system
- Share workarounds and best practices

**Note**: These limitations are documented to set realistic expectations and help users work around known problems. The build system is functional for most use cases, but awareness of these limitations helps in planning and content preparation.