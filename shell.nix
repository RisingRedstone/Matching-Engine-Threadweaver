{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    # Compilers
    gcc13
    clang_18
    
    # Build Tools
    cmake
    gnumake
    
    # Documentation & Visualization
    doxygen
    graphviz
    
    # Utils
    gdb
    valgrind # Great for debugging memory in matching engines
  ];

  shellHook = ''
    # This changes the prompt to: [nix-shell:FolderName]$
    export PS1="\[\e[1;32m\][shell:ThreadWeaver]\$ \[\e[0m\]"

    echo "--- Matching Engine Dev Environment ---"
    echo "GCC:   $(g++ --version | head -n 1)"
    echo "Clang: $(clang++ --version | head -n 1)"
    echo "---------------------------------------"

    # Helpers to switch compilers on the fly
    alias use-gcc="export CC=gcc && export CXX=g++ && echo 'Switched to GCC'"
    alias use-clang="export CC=clang && export CXX=clang++ && echo 'Switched to Clang'"

    # Cleaning
    alias clean_build="rm -rf build/"
    alias clean_bin="rm -rf bin/"
    alias clean_docs="rm -rf docs/"
    alias clean_all="clean_build && clean_bin && clean_docs"

    # Configuration (Auto-cleaning build to prevent cache conflicts)
    alias config_debug="clean_build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug"
    alias config_release="clean_build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release"

    # Build helpers
    alias run_build="cmake --build build -j$(nproc)"
    alias run_test="ctest --test-dir build --output-on-failure"
    alias build_docs="mkdir -p docs && cmake --build build --target doc"

    # View Docs (Detects OS to use the right open command)
    if [[ "$OSTYPE" == "darwin"* ]]; then
      alias view_docs="open docs/html/index.html >/dev/null 2>&1 &"
    else
      alias view_docs="xdg-open docs/html/index.html >/dev/null 2>&1 &"
    fi
  '';
}
