#!/bin/python3

import sys
import time

def test_streams():
    for i in range(1, 11):
        # Print 40 of a character to STDOUT
        char = chr(64 + i)  # A, B, C...
        sys.stdout.write(f"LINE {i:03}: " + (char * 40) + "\n")
        sys.stdout.flush()
        
        # Interject an ALERT to STDERR every 2 lines
        if i % 2 == 0:
            sys.stderr.write(f"LINE {i:03}: !!! STDERR ALERT !!!\n")
            sys.stderr.flush()
            
        time.sleep(0.1)

if __name__ == "__main__":
    test_streams()
