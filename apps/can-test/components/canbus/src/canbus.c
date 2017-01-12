/*
 * Copyright 2016, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(D61_BSD)
 */
#define ZF_LOG_LEVEL ZF_LOG_WARN
#include <utils/zf_log.h>

/* standard */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <utils/ansi.h>

#include <camkes.h>

#include "can_inf.h"

void request_status() {
    ZF_LOGI("Request phase");

    struct can_frame tx;

    tx.ident.id = 0x200;
    tx.ident.exide = 0;
    tx.ident.rtr = 0;
    tx.dlc = 8;
    tx.data[0] = 0xBC;
    tx.data[1] = 0x22;
    tx.data[2] = 0x00;
    tx.data[3] = 0x9a;
    tx.data[4] = 0x00;
    tx.data[5] = 0x00;
    tx.data[6] = 0x00;
    tx.data[7] = 0x00;

    for (int i = 0; i < 10; i++) {
        int error = can_sendto(0, tx);
        txb0_ack_wait();

        ZF_LOGV("Sent: error(%d), id(%x), data(%x, %x, %x, %x, %x, %x, %x, %x)",
                error, tx.ident.id,
                tx.data[0], tx.data[1], tx.data[2], tx.data[3],
                tx.data[4], tx.data[5], tx.data[6], tx.data[7]);

        tx.ident.id++;
        tx.data[0] = 0x00;
        tx.data[1] = 0x00;
        tx.data[2] = 0x00;
        tx.data[3] = 0x00;
        tx.data[4] = 0x00;
        tx.data[5] = 0x00;
        tx.data[6] = 0x00;
        tx.data[7] = 0x00;
    }
}

void read_status() {
    ZF_LOGI("Response phase");

    for (int i = 0; i < 10; i++) {
        struct can_frame rx;
        can_recv(&rx);
        ZF_LOGV("Recv: id(%x), data(%x, %x, %x, %x, %x, %x, %x, %x)",
                rx.ident.id,
                rx.data[0], rx.data[1], rx.data[2], rx.data[3],
                rx.data[4], rx.data[5], rx.data[6], rx.data[7]);
    }
}

int run(void)
{
    printf("Start CAN Test\n");
    can_setup(125000);

    for (int i = 0; true; i++) {
        request_status();
        read_status();
        printf("Iterations: %d\n", i);
    }

    return 0;
}
