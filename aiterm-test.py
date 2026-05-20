#!/bin/python3

import sys
import time

def run_test(iterations=100, delay=0.01):
    print(f"--- Starting aiterm Tee Buffer Test ({iterations} lines) ---")
    
    for i in range(1, iterations + 1):
        # Test standard output
        print(f"LINE {i:04}: This is a standard STDOUT message.")
        
        # Test standard error (to see if tee captures both streams)
        if i % 10 == 0:
            print(f"LINE {i:04}: !!! STDERR ALERT !!!", file=sys.stderr)
            sys.stderr.flush()
 
        # Optional delay to simulate realistic app behavior
        if delay > 0:
            time.sleep(delay)

        # Flush stdout to ensure it hits the buffer immediately
        sys.stdout.flush()

    print("--- Buffer Test Complete ---")

if __name__ == "__main__":
    # You can pass the number of lines as an argument
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    run_test(count)

