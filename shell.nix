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

    linuxPackages.perf
    gprof2dot
    hotspot # GUI for perf data
    flamegraph
    kdePackages.kcachegrind
    lttng-ust
    lttng-tools
    liburcu
    pkg-config
    babeltrace2
  ];

  shellHook = ''
    # This changes the prompt to: [nix-shell:FolderName]$
    export PS1="\[\e[1;32m\][shell:ThreadWeaver]\$ \[\e[0m\]"

    echo "--- Matching Engine Dev Environment ---"
    echo "GCC:   $(g++ --version | head -n 1)"
    echo "Clang: $(clang++ --version | head -n 1)"
    echo "LTTng-UST development environment loaded."
    # Optional: Ensure the lttng-sessiond can find the libraries
    export LD_LIBRARY_PATH="${pkgs.lttng-ust}/lib:$LD_LIBRARY_PATH"

    # --- THE FIX FOR CLANGD ---
    # This tells the compiler (and clangd) where to look for headers 
    # without needing to hardcode store paths.
    export CPATH="${pkgs.liburcu}/include:${pkgs.lttng-ust}/include:$CPATH"
    export CPLUS_INCLUDE_PATH="${pkgs.liburcu}/include:${pkgs.lttng-ust}/include:$CPLUS_INCLUDE_PATH"
    export LD_LIBRARY_PATH="${pkgs.liburcu}/lib:${pkgs.lttng-ust}/lib:$LD_LIBRARY_PATH"

    echo "--- Matching Engine Dev Environment ---"
    echo "LTTng-UST paths exported for LSP."

    # --- AUTOMATIC COMPILE COMMANDS ---
    # This automatically generates the 'map' clangd needs if you use CMake
    #if [ -f CMakeLists.txt ]; then
    #    echo "Generating compilation database for clangd..."
    #    cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null
    #    # Link it to the root so clangd finds it immediately
    #    ln -sf build/compile_commands.json .
    #fi
    # --------------------------

    echo "---------------------------------------"

    # Helpers to switch compilers on the fly
    alias use-gcc="export CC=gcc && export CXX=g++ && echo 'Switched to GCC'"
    alias use-clang="export CC=clang && export CXX=clang++ && echo 'Switched to Clang'"

    # Cleaning
    alias clean_compile_commands="rm compile_commands.json"
    alias clean_build="rm -rf build/"
    alias clean_bin="rm -rf bin/"
    alias clean_docs="rm -rf docs/"
    alias clean_all="clean_build && clean_bin && clean_docs && clean_compile_commands"

    # Configuration (Auto-cleaning build to prevent cache conflicts)
    alias config_debug="clean_build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && ln -sf build/compile_commands.json"
    alias config_release="clean_build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && ln -sf build/compile_commands.json"

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

    # Test this one
    alias valgrind_prof="mkdir -p .profile && valgrind --tool=callgrind --callgrind-out-file=.profile/callgrind.out.%p --separate-threads=yes --trace-children=yes --dump-instr=yes --collect-jumps=yes --simulate-cache=yes --simulate-hwpref=yes"
    alias perf_prof="mkdir -p .profile && sudo perf record -e cache-references,cache-misses,L1-dcache-load-misses -g -o ./.profile/perf.data"
  '';
}
