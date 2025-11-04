# FreeBSD Device Drivers: From First Steps to Kernel Mastery

## 🌐 Live Site

**Production URL**: https://freebsd.edsonbrandi.com  
**Repository**: https://github.com/ebrandi/FDD-book

## 📖 About the Book

"FreeBSD Device Drivers: From First Steps to Kernel Mastery" - is a comprehensive, community-driven guide to writing device drivers for FreeBSD. The book covers everything from basic concepts to advanced topics like interrupt handling, DMA, and performance optimization.

### What You'll Learn

- **Foundation & Environment**: UNIX fundamentals, C programming, and development setup
- **Core Driver Development**: Building your first device driver and understanding kernel modules
- **Advanced System Programming**: Concurrency, synchronization, and memory management
- **Hardware Integration**: PCI drivers, interrupts, DMA, and power management
- **Debugging & Optimization**: FreeBSD-specific debugging tools and performance tuning
- **Professional Development**: Portable drivers, specialized hardware, and open-source contribution

### Project Structure

```
FDD-book/
├── content/              # Book content (Markdown files)
│   ├── appendices/       # Appendix markdown files
│   └── chapters/         # Chapter markdown files by part
├── examples/             # Book Code Examples
│   ├── appendices/       # Source Code from appendices
│   └── chapters/         # Source Code from appendices
├── translations/         # Book content translated
│   └── pt_BR/            # Content in pt_BR
│       ├── appendices/   # 
│       └── chapters/     # 
└── scripts/              # Utility scripts
```

## 📝 Content Management

### Adding New Chapters

1. **Create a new markdown file** in the appropriate part directory:
   ```bash
   content/chapters/part1/chapter-07.md
   ```

2. **Add YAML frontmatter**:
   ```yaml
   ---
   title: "Chapter Title"
   description: "Chapter description"
   chapter: 7
   part: 1
   status: "draft"  # planned, draft, revised, complete
   estimatedReadTime: 15
   lastUpdated: "2025-01-14"
   author: "Author Name"
   reviewer: "Reviewer Name"
   ---
   ```

3. **Write your content** in Markdown format

### Adding New Appendices

1. **Create a new markdown file** in the appendices directory:
   ```bash
   content/appendices/appendix-f.md
   ```

2. **Add YAML frontmatter**:
   ```yaml
   ---
   title: "Appendix Title"
   description: "Appendix description"
   status: "draft"
   estimatedReadTime: 10
   lastUpdated: "2025-01-14"
   author: "Author Name"
   reviewer: "Reviewer Name"
   ---
   ```

### Content Status Types

- **`planned`**: Content is planned but not yet started
- **`draft`**: Initial content written, needs review
- **`revised`**: Content reviewed and revised
- **`complete`**: Content finalized and ready for publication

## 🤝 Contributing

We welcome contributions! Please see our [Contributing Guide](https://freebsd.edsonbrandi.com/contribute) for detailed information.

### Ways to Contribute

- **Content**: Add new chapters, improve existing content
- **Review**: Technical review and proofreading
- **Translation**: Help translate the book to other languages
- **Code**: Improve the website and add features
- **Issues**: Report bugs and suggest improvements

### Development Workflow

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/your-feature`
3. Make your changes
4. Test your changes, use the scripts/build-book.sh to generate the PDF/EPUB
5. Commit your changes: `git commit -m "Add your feature"`
6. Push to your fork: `git push origin feature/your-feature`
7. Create a Pull Request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- The FreeBSD development community
- All contributors and reviewers
- The open-source community for tools and inspiration

## 📞 Support

- **Website**: https://freebsd.edsonbrandi.com
- **Repository**: https://github.com/ebrandi/FDD-book
- **Issues**: [GitHub Issues](https://github.com/ebrandi/FDD-book/issues)
- **Discussions**: [GitHub Discussions](https://github.com/ebrandi/FDD-book/discussions)

---

**Author**: Edson Brandi  (ebrandi@FreeBSD.org)
**Website**: https://freebsd.edsonbrandi.com  
**Repository**: https://github.com/ebrandi/FDD-book
