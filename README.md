# SLOW_Protocol
This work implements the SLOW protocol an ad hoc protocol in the transport layer for data flow control, more specifically the peripheral.

SLOW has some similarities to QUIC: when those responsible for implementing QUIC were planning it, they faced the challenge of getting major operating systems, such as Linux, Windows and Mac, to implement a new transport protocol at the kernel level, since without kernel-level support it would be impossible to use. Therefore, they decided to use UDP, since it is a lightweight protocol that was already implemented in almost all kernels, as the infrastructure for QUIC.
SLOW also uses UDP as the infrastructure for exchanging messages, and adds functionalities on top of it.
This work implements the peripheral in C++.
