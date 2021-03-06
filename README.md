# Reliable Data Transport

This project implements a reliable data transfer over an unreliable (simulated) link using the stop-and-wait protocol that works even when links have very small packet buffers. The ReliableSocket class (ReliableSocket.h and ReliableSocket.cpp) allows any application that’s built on top of it to achieve reliable communication, such that transferred files is shown to be byte-for-byte identical copies from sender to receiver. It is able to cleanly shut down connections on both ends, even if packets get lost.
