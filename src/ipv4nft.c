/*
 * ipv4nft.c - FakeHTTP: https://github.com/MikeWang000000/FakeHTTP
 *
 * Copyright (C) 2025  MikeWang000000
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE
#include "ipv4nft.h"

#include <inttypes.h>
#include <stdlib.h>

#include "globvar.h"
#include "logging.h"
#include "process.h"

static int nft4_iface_setup(void)
{
    char nftstr[120];
    size_t i, cnt;
    int res;
    char *nft_iface_cmd[] = {"nft", nftstr, NULL};

    if (g_ctx.alliface) {
        res = snprintf(nftstr, sizeof(nftstr),
                       "add rule ip fakehttp fh_prerouting jump fh_rules");
        if (res < 0 || (size_t) res >= sizeof(nftstr)) {
            E("ERROR: snprintf(): %s", "failure");
            return -1;
        }

        res = fh_execute_command(nft_iface_cmd, 0, NULL);
        if (res < 0) {
            E(T(fh_execute_command));
            return -1;
        }
        return 0;
    }

    cnt = sizeof(g_ctx.iface) / sizeof(*g_ctx.iface);

    for (i = 0; i < cnt && g_ctx.iface[i]; i++) {
        res = snprintf(
            nftstr, sizeof(nftstr),
            "add rule ip fakehttp fh_prerouting iifname \"%s\" jump fh_rules",
            g_ctx.iface[i]);
        if (res < 0 || (size_t) res >= sizeof(nftstr)) {
            E("ERROR: snprintf(): %s", "failure");
            return -1;
        }

        res = fh_execute_command(nft_iface_cmd, 0, NULL);
        if (res < 0) {
            E(T(fh_execute_command));
            return -1;
        }
    }
    return 0;
}


int fh_nft4_setup(void)
{
    size_t i, nft_opt_cmds_cnt;
    int res;
    char *nft_cmd[] = {"nft", "-f", "-", NULL};
    char nft_conf_buff[2048];
    char *nft_conf_fmt =
        "table ip fakehttp {\n"
        "    chain fh_prerouting {\n"
        "        type filter hook prerouting priority mangle - 5;\n"
        "        policy accept;\n"
        "    }\n"
        "\n"
        "    chain fh_rules {\n"

        /*
            exclude marked packets
        */
        "        meta mark and %" PRIu32 " == %" PRIu32
        " ct mark set ct mark and %" PRIu32 " xor %" PRIu32 ";\n"

        "        ct mark and %" PRIu32 " == %" PRIu32
        " meta mark set mark and %" PRIu32 " xor %" PRIu32 ";\n"

        "        meta mark and %" PRIu32 " == %" PRIu32 " return;\n"

        /*
            exclude local IPs
        */
        "        ip saddr 0.0.0.0/8      return;\n"
        "        ip saddr 10.0.0.0/8     return;\n"
        "        ip saddr 100.64.0.0/10  return;\n"
        "        ip saddr 127.0.0.0/8    return;\n"
        "        ip saddr 169.254.0.0/16 return;\n"
        "        ip saddr 172.16.0.0/12  return;\n"
        "        ip saddr 192.168.0.0/16 return;\n"
        "        ip saddr 224.0.0.0/3    return;\n"

        /*
            send to nfqueue
        */
        "        tcp flags & (fin | rst | ack) == ack queue num %" PRIu32
        " bypass;\n"

        "    }\n"
        "}\n";

    char *nft_opt_cmds[][32] = {
        /*
            exclude packets from connections with more than 32 packets
        */
        {"nft", "insert rule ip fakehttp fh_rules ct packets > 32 return",
         NULL},

        /*
            exclude big packets
        */
        {"nft", "insert rule ip fakehttp fh_rules meta length > 120 return",
         NULL}};

    nft_opt_cmds_cnt = sizeof(nft_opt_cmds) / sizeof(*nft_opt_cmds);

    res = snprintf(nft_conf_buff, sizeof(nft_conf_buff), nft_conf_fmt,
                   g_ctx.fwmask, g_ctx.fwmark, ~g_ctx.fwmask, g_ctx.fwmark,
                   g_ctx.fwmask, g_ctx.fwmark, ~g_ctx.fwmask, g_ctx.fwmark,
                   g_ctx.fwmask, g_ctx.fwmark, g_ctx.nfqnum);
    if (res < 0 || (size_t) res >= sizeof(nft_conf_buff)) {
        E("ERROR: snprintf(): %s", "failure");
        return -1;
    }

    fh_nft4_cleanup();

    res = fh_execute_command(nft_cmd, 0, nft_conf_buff);
    if (res < 0) {
        E(T(fh_execute_command));
        return -1;
    }

    for (i = 0; i < nft_opt_cmds_cnt; i++) {
        fh_execute_command(nft_opt_cmds[i], 1, NULL);
    }

    res = nft4_iface_setup();
    if (res < 0) {
        E(T(nft4_iface_setup));
        return -1;
    }

    return 0;
}


void fh_nft4_cleanup(void)
{
    char *nft_delete_cmd[] = {"nft", "delete table ip fakehttp", NULL};

    fh_execute_command(nft_delete_cmd, 1, NULL);
}
