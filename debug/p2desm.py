#!/usr/bin/env python3

import re
import sys

def main():
    data = sys.argv[1]

    pattern = (
        r'x\s*=\s*([^,]+),\s*'
        r'y\s*=\s*([^,]+),\s*'
        r'z\s*=\s*([^,]+),\s*'
        r'w\s*=\s*([^}]+)'
    )

    matches = re.findall(pattern, data)

    for x, y, z, w in matches:
        print(f"{x}x + ({y})y + ({z})z = {w}")

if __name__ == "__main__":
    main()
