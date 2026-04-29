infile = "c2rpqs-connected.txt"
outfile = "c2rpqs-connected-debug.txt"
error_queries = []  # Input error query IDs here
import os
for q in error_queries:
    os.system("head -" + str(q) + ' ' + infile + ' | tail -1 >> ' + outfile)