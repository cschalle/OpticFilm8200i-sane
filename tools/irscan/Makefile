# Uses the SANE headers from this tree and the system libsane.
CFLAGS ?= -O2 -Wall
irscan: irscan.c
	$(CC) $(CFLAGS) -I../../backends/include -o $@ $< -l:libsane.so.1
clean:
	rm -f irscan
