#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#define ARRAY_LEN(type, bytes) (((bytes) + sizeof(type) - 1) / sizeof(type))

/*
 * The stock libfileXio client exports generic 0x4c00-byte RPC and 0x3000-byte
 * interrupt buffers. Stage2 only uses open/read/lseek/close, mount/umount, and
 * devctl with no return payload, so keep enough room for its largest packet.
 */
#define STAGE2_FILEXIO_SBUFF_BYTES 0x1100
#define STAGE2_FILEXIO_INTR_BYTES sizeof(struct fxio_ctl_return_pkt)

typedef char filexio_sbuff_fits_devctl[
    (STAGE2_FILEXIO_SBUFF_BYTES >= sizeof(struct fxio_devctl_packet)) ? 1 : -1];
typedef char filexio_intr_fits_read[
    (STAGE2_FILEXIO_INTR_BYTES >= sizeof(rests_pkt)) ? 1 : -1];
typedef char filexio_intr_fits_ctl[
    (STAGE2_FILEXIO_INTR_BYTES >= sizeof(struct fxio_ctl_return_pkt)) ? 1 : -1];

unsigned int __sbuff[ARRAY_LEN(unsigned int, STAGE2_FILEXIO_SBUFF_BYTES)]
    __attribute__((aligned(64)));
int __intr_data[ARRAY_LEN(int, STAGE2_FILEXIO_INTR_BYTES)] __attribute__((aligned(64)));
