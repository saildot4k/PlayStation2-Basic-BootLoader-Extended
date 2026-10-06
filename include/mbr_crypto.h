#ifndef MBR_CRYPTO_H
#define MBR_CRYPTO_H

// Decrypts PSBBN MBRBOOT arguments.
// Adapted from OSDMenu mbr/include/crypto.h (AFL-3.0).
char **decryptMBRBOOTArgs(int *argc, char **argv);

#endif // MBR_CRYPTO_H
