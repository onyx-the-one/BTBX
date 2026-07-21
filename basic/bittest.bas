10 PRINT "BITWISE OPS DEMO"
20 PRINT "----------------"
30 A = 12
40 B = 10
50 PRINT "A = "; A; "  ("; BIN$(A); ")"
60 PRINT "B = "; B; "  ("; BIN$(B); ")"
70 PRINT
80 PRINT "A AND B = "; AND(A,B); "  ("; BIN$(AND(A,B)); ")"
90 PRINT "A OR  B = "; OR(A,B);  "  ("; BIN$(OR(A,B)); ")"
100 PRINT "A XOR B = "; XOR(A,B); "  ("; BIN$(XOR(A,B)); ")"
110 PRINT "NOT A   = "; NOT(A); "  ("; HEX$(NOT(A)); ")"
120 PRINT "A SHL 2 = "; SHL(A,2); "  ("; BIN$(SHL(A,2)); ")"
130 PRINT "A SHR 2 = "; SHR(A,2); "  ("; BIN$(SHR(A,2)); ")"
140 PRINT
150 PRINT "DONE."
