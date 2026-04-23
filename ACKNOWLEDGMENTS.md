# Acknowledgments and Credits

*FreeBSD Device Drivers: From First Steps to Kernel Mastery* would not exist in its current form without a long list of free and open tools, communities, and projects. The author is deeply grateful to everyone who builds and maintains them. This page is a thank you note to the people and projects that make writing, producing, and distributing this book possible.

If anything on this list is missing or misattributed, please open an issue at https://github.com/ebrandi/FDD-book/issues and it will be corrected.

## Writing and Authoring

**Markdown.** The entire manuscript is written in plain Markdown files. Markdown is simple, portable, diff friendly, and ages well. Thank you to John Gruber for the original specification and to the broader community behind CommonMark and GitHub Flavored Markdown, which together form the de facto standard used throughout this project.

**Typora.** Most of the day to day writing and revision happens in Typora (https://typora.io/), a live preview Markdown editor that keeps the prose readable while still writing raw Markdown on disk. Its distraction free layout, table editor, and code block handling make long form technical writing noticeably more pleasant.

**Visual Studio Code.** Used occasionally for heavier file wrangling, bulk edits, and working with the companion example tree. Thank you to Microsoft and to the many extension authors in the VS Code ecosystem.

## Version Control and Collaboration

**Git.** Every chapter, appendix, example, and draft lives in a Git repository. Git makes it safe to experiment, to rewrite entire sections, and to recover from mistakes. Thank you to Linus Torvalds and the many maintainers who keep Git fast, reliable, and universally available.

**GitHub.** The project is hosted at https://github.com/ebrandi/FDD-book and uses GitHub for source hosting, issue tracking, pull requests, and discussions. GitHub is also the home of many of the upstream tools listed below, which makes it a practical single stop for collaboration.

## Book Production Toolchain

**Pandoc.** The central tool that turns the Markdown manuscript into PDF, EPUB, and HTML5. Pandoc (https://pandoc.org/) is the backbone of the build system. Thank you to John MacFarlane and every Pandoc contributor for producing such a capable, well documented, and reliable converter.

**Eisvogel template.** The polished PDF layout is produced using the Eisvogel Pandoc LaTeX template by Pascal Wagler (https://github.com/Wandmalfarbe/pandoc-latex-template). The title page styling, book structured chapters, code block formatting, and general look and feel all come from this template. The project is distributed under a permissive license and has been a pleasure to work with.

**TeX Live and XeLaTeX.** Behind Eisvogel sits a complete TeX Live distribution, with XeLaTeX as the engine used by the build. Thank you to the TeX Users Group, the TeX Live maintainers, and every package author whose work gets pulled in during a full build. Special thanks to Donald Knuth, Leslie Lamport, and the decades of contributors who built and refined TeX and LaTeX.

**LaTeX packages.** The build pulls in, among others, graphicx, fontspec, unicode-math, listings, and fvextra. Each of these is maintained by volunteer package authors, and the book benefits directly from their work.

**Pandoc default HTML template and CSS.** The HTML5 build relies on Pandoc's default standalone template and stylesheet, which produce a clean, readable single file output with embedded resources.

**Tango syntax highlighting style.** Code samples are highlighted using Pandoc's built in Tango style, which is tuned for C and shell code and remains legible in both PDF and HTML5 output.

## Fonts and Typography

**Liberation fonts.** Liberation Mono is used throughout the book for code blocks, inline code, and verbatim text. Thank you to Red Hat and the Liberation font authors for releasing these fonts under a free license.

**DejaVu fonts.** Used as a fallback for extended Unicode coverage, especially for symbols and diagrams.

**Latin Modern Math.** Provides mathematical glyphs for the PDF build.

**Microsoft core fonts.** Times New Roman and Arial are used for the main body text and headings in the PDF. Thank you to the authors of these fonts and to the packaging work that makes them available on Linux systems.

**Noto and Ubuntu font families.** Occasionally used for broader language and glyph coverage.

**Fontconfig.** Keeps the font stack on the build machine consistent and discoverable.

## Supporting Utilities

**Ghostscript.** Used indirectly by the PDF toolchain for various post processing steps.

**poppler utils.** Handy for inspecting generated PDFs during troubleshooting.

**ImageMagick.** Used for occasional image preparation and conversion tasks during manuscript production.

**cabextract.** Used when preparing the Microsoft core fonts on build machines that do not already have them installed.

**GNU Bash, coreutils, and standard Unix tools.** The build script is a Bash script, and it relies on the usual cast of small, sharp Unix utilities. Thank you to the GNU project, to the many POSIX tool maintainers, and to everyone who keeps this foundation healthy.

## Translation and Localization

**Ollama.** The translation of the manuscript into Brazilian Portuguese (pt_BR) is driven by Ollama (https://ollama.com/), which makes it straightforward to run large language models locally. Running the translation pipeline on the author's own machine keeps the source text under local control, makes repeated iterations inexpensive, and allows experiments with prompts and passage boundaries without depending on an external service. Thank you to the Ollama maintainers and contributors for packaging this capability in such an approachable form.

**Qwen language model.** The actual translation step is performed by the `qwen3.6:35b-a3b-bf16` model running under Ollama. Thank you to the Qwen team at Alibaba Cloud and to every researcher and engineer whose work made a model of this size and quality available for local use under a permissive license. The readability of the Brazilian Portuguese edition reflects directly on the capability of this model.

**Python.** The translation workflow itself is driven by Python scripts that split the manuscript into translatable units, call the model through Ollama, reassemble the translated fragments, and keep the translated chapters in step with the English originals. Thank you to the Python Software Foundation and to the wider Python community for maintaining a language that is reliable, readable, and well suited to this kind of practical automation.

## Platform and Environment

**FreeBSD.** The subject of the book and its deepest source of inspiration. Thank you to the FreeBSD Project, its core team, its committers, its documentation team, and the wider community of users and contributors. Every chapter is grounded in the real FreeBSD source tree, and every accurate claim in the book reflects the quality of that source.

**FreeBSD documentation.** The Handbook, the manual pages, the Architecture Handbook, and the many committer notes were consistently useful for verification, terminology, and context. Thank you to the FreeBSD Documentation Project and to every author who has contributed to them over the years.

**Ubuntu and Windows Subsystem for Linux.** The author's working environment is a Windows laptop running WSL with Ubuntu, which is where the build toolchain is installed and where the PDF, EPUB, and HTML5 outputs are generated. Thank you to Canonical for Ubuntu and to Microsoft for WSL, which together make this a practical setup for writing a book about an entirely different operating system.

## Communities and Readers

**The FreeBSD community.** Mailing lists, forums, IRC and Matrix channels, and the BSDCan, EuroBSDCon, and AsiaBSDCon communities have all contributed, directly or indirectly, to the understanding reflected in these pages.

**Reviewers, early readers, and contributors.** Everyone who filed an issue, suggested a correction, proposed an example, reviewed a chapter, or translated a section has shaped the final text. Their names, where known, appear in the README and in the project's contribution history.

**You, the reader.** The book is written for you. Thank you for picking it up and for giving this material the time it takes to read carefully. Every question, correction, and improvement suggestion helps the next reader and is genuinely appreciated.

## Licensing and Responsibility

The book content itself is distributed under the terms in the repository's `LICENSE` file. Each third party tool, template, font, and package listed above retains its own license and copyright. None of the above projects endorse this book, and any errors are the responsibility of the author.
