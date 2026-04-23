#!/bin/bash
get_time_elapsed(){
    RESULT=$(sudo perf stat ./bin/release/buffer_general_test 2>&1 >/dev/null | awk '/seconds time/ {print $1}')
    echo "$RESULT"
}

run_build() {
    cmake --build build -j$(nproc)
}

APPROACH=(1 2)
NUMBER_OF_WRITERS=(8)
# BUFFER_SIZE=($((65536*8)) $((65536*64)) $((65536*256)))
# WRITE_NUMBERS=()
# for i in {2..11}; do
#     WRITE_NUMBERS+=($((65536*2**i)))
# done
BUFFER_SIZE=()
for i in {0..10}; do
    BUFFER_SIZE+=($((16384*4**i)))
done
WRITE_NUMBERS+=($((65536*2**10)))
NUM_OF_TESTS=4

# Main Loops - Iterating over VALUES directly is cleaner
echo "Approach, Number of Writers, Buffer Size, Numbers to write, Time elapsed"
for A in "${APPROACH[@]}"; do
    if [ "$A" -eq 1 ]; then
        DESC="Three-Pointer"
    else
        DESC="Cell-Lockable"
    fi
    echo "Doing the $DESC:" >&2
    for N_WRIT in "${NUMBER_OF_WRITERS[@]}"; do
        echo "\tNumber of writers: $N_WRIT:" >&2
        for B_SIZE in "${BUFFER_SIZE[@]}"; do
            echo "\t\tBuffer Size: $B_SIZE:" >&2
            for W_NUM in "${WRITE_NUMBERS[@]}"; do
                
                # Use -D flags directly in cmake
                cmake -B build -DAPPROACH="$A" -DNUM_OF_WRITERS="$N_WRIT" -DBUFFER_SIZE="$B_SIZE" -DWRITE_NUMBERS="$W_NUM" > /dev/null
                
                run_build > /dev/null 2>&1
                
                for ((i=0; i<=NUM_OF_TESTS; i++)); do
                    RESULT=$(get_time_elapsed)
                    # Formatting the output nicely
                    printf "%s,%-2s,%-10s,%-10s,%s\n" "$DESC" "$N_WRIT" "$B_SIZE" "$W_NUM" "$RESULT"
                done
            done
        done
    done
done
