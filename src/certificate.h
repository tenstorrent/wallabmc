/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 */

#ifndef __CERTIFICATE_H__
#define __CERTIFICATE_H__

enum tls_tag {
        /** Used for both the public and private server keys */
        HTTP_SERVER_CERTIFICATE_TAG,
};

static const unsigned char server_certificate[] = {
#include "server_cert.der.inc"
};

/* This is the private key in pkcs#8 format. */
static const unsigned char private_key[] = {
#include "server_privkey.der.inc"
};

#endif /* __CERTIFICATE_H__ */
