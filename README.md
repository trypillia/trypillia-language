<div align="center">

<img src="public/logo-squircle.svg" alt="Trypillia Programming Language" width="160" height="160" />

# Trypillia Programming Language

*A modern, fast, and elegantly designed programming language.*

[![Build and Release](https://github.com/trypillia/trypillia-language/actions/workflows/build.yml/badge.svg)](https://github.com/trypillia/trypillia-language/actions/workflows/build.yml)
[![License: CSSM Unlimited License v2.0](https://img.shields.io/badge/License-CSSM%20Unlimited%20License%20v2.0-blue.svg?logo=opensourceinitiative)](LICENSE)

[Features](#features) · [Performance](#performance) · [Installation & Build](#installation--build) · [Quick Start](#quick-start) · [IDE Support](#ide-support) · [Contributing](#contributing)

---

</div>

**Trypillia** is a modern, fast, and elegantly designed programming language built from the ground up to empower developers. With a clean syntax, robust standard library, and built-in LSP support, Trypillia is ready for your next big project.

## Features

- **Clean Syntax**: Familiar C-style syntax with modern conveniences (similar to JS/TypeScript and Rust).
- **Rich Standard Library**: Built-in support for Math, File System, Networking (TCP, HTTP, WebSockets), Regex, and more.
- **First-class LSP**: Ships with its own Language Server for VS Code, providing semantic highlighting, autocompletion, signature help, and live error checking.
- **Performant VM**: Runs on a custom-built Virtual Machine optimized for speed and execution safety.
- **Multithreading**: Native support for background Workers.

## Performance

Trypillia features a highly optimized backend, including a custom sljit-based **Static Register Allocator** that completely eliminates memory accesses during loop execution. 

In pure mathematical operations, Trypillia's JIT executes exactly as fast as V8 (Node.js) and is **15x faster than Python**. 

Check out the detailed cross-language comparison in [BENCHMARKS.md](BENCHMARKS.md).

## Installation & Build

### Quick Install

You can install and compile Trypillia quickly using the following one-liners:

**Linux / macOS** (Bash):
```bash
curl -sSL https://tryp.pp.ua/install.sh | sh
```

**Windows** (PowerShell):
```powershell
irm https://tryp.pp.ua/install.ps1 | iex
```

### Building from Source

Trypillia is built using CMake and a modern C++ compiler.

```bash
# Clone the repository
git clone https://github.com/trypillia/trypillia-language.git
cd trypillia-language

# Build the compiler and LSP server
mkdir build && cd build
cmake ..
make -j4
```

## Quick Start

Create a file named `hello.try`:

```trypillia
// Declare variables
let message = "Привіт, Трипілля!";
print(message);

// Define functions
fn calculate(a, b) {
    return a + b;
}

print(calculate(10, 20));
```

Run your code:
```bash
./build/trypillia hello.try
```

## Error Handling

Trypillia features beautifully formatted runtime errors (panics) with full stack traces, making debugging intuitive and friendly:

```text
$ ./trypillia main.try

 ૮ ˶ᵔ ᵕ ᵔ˶ ა 
 / づ 📝 ♡ 

Panic: Expected 2 arguments but got 1.

Traceback (most recent call last):
  at calculate in main.try:8
  at <main> in main.try:12
```

## IDE Support

Trypillia comes with an official **VS Code extension**. To install and use it:
1. Open the `vscode-extension` folder in VS Code.
2. Run `npm install` to install dependencies.
3. The extension will automatically connect to the `trypillia-lsp` binary from your `build` directory.

## Package Manager (TrypPM)

Trypillia has a native package manager called **[TrypPM](https://github.com/trypillia/tryppm)**. It allows you to initialize projects, manage dependencies, and even upgrade the language compiler natively.

**Features of TrypPM:**
- **`tryppm init`**: Generate a standard `tryp.json` manifest file for your project.
- **`tryppm install`**: Installs dependencies directly from GitHub into a local `tryp_modules` directory.
- **`tryppm install github:User/Repo[@version]`**: Installs a specific package (supports SemVer like `@^1.0.0` or `@latest`).
- **`tryppm uninstall <pkg>`**: Remove a package from `tryp.json`, delete its files, and update the autoloader.
- **`tryppm upgrade`**: Seamlessly upgrade the Trypillia language compiler from source.

## Contributing

Contributions are welcome and appreciated! Here's how you can contribute:

1. Fork the project
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

Please make sure to update tests as appropriate and adhere to the existing coding style.

## License

This project is licensed under the CSSM Unlimited License v2.0 (CSSM-ULv2). See the [LICENSE](LICENSE) file for details.

## Acknowledgments

- The lightning-fast JIT compiler backend is powered by [sljit](https://zherczeg.github.io/sljit/), a platform-independent low-level JIT compiler
- JSON parsing and serialization across the standard library and language server is handled by [nlohmann/json](https://github.com/nlohmann/json) (json.hpp)
- Cryptographic and TLS/SSL capabilities are provided by [Mbed TLS](https://github.com/Mbed-TLS/mbedtls)
