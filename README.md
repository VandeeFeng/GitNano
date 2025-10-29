# GitNano

GitNano is a mini, experiment implementation of Git.

The goal of this project is: to serve as an educational tool for understanding Git's internals, and to offer a simple, local version control system that operates independently of Git.

## How It Works

GitNano differs from standard Git in the following key ways:

1.  **Isolated Workspace**:
    `gitnano` maintains an isolated workspace in the `~/GitNano/[project-name]/` directory. When you run `gitnano add` or `gitnano checkout`, files are automatically synchronized between your project directory and this isolated workspace. This design separates the version control metadata from your working directory.

2.  **Simplified Staging Area**:
    The staging area in `gitnano` (the `.gitnano/index` file) is a simple text file that records the SHA-1 hash and path of each file. 

## Basic Commands

- **Initialize a repository**
  ```bash
  gitnano init
  ```

- **Add a file**
  ```bash
  gitnano add <file>
  ```

- **Create a commit**
  ```bash
  gitnano commit "Your commit message"
  ```

- **View the log**
  ```bash
  gitnano log
  ```

- **Checkout a version**
  ```bash
  gitnano checkout <commit-sha>
  ```

- **Restore a file**
  ```bash
  gitnano checkout <commit-sha> <path/to/file>
  ```

## Installation

### Prerequisites

Before you begin, ensure you have the following installed:
- `gcc` and `make`
- `zlib` (for compression)
- `openssl` (for cryptographic functions)

### Build and Install

1.  **Compile the project:**
    ```bash
    make
    ```

2.  **Install the executable:**
    ```bash
    make install
    ```
    This will copy the `gitnano` binary to `~/.local/bin/`.

3.  **Update your PATH:**
    To run `gitnano` from any directory, make sure `~/.local/bin` is in your system's `PATH`. You can do this by adding the following line to your shell's configuration file (e.g., `~/.bashrc` or `~/.zshrc`):
    ```bash
    export PATH="$HOME/.local/bin:$PATH"
    ```
    Remember to reload your shell's configuration (`source ~/.bashrc`)

