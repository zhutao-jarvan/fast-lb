#include "sockopt.h"
#include <stdio.h>
#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <unistd.h>
#include "common.h"

#define UNIX_DOMAIN "/var/run/fastlb_ctrl"

static inline int sockopt_msg_send(int clt_fd,
        const struct dpvs_sock_msg *hdr,
        const char *data, int data_len)
{
    int len, res;

    if (!hdr) {
        fprintf(stderr, "[%s] empty socket msg header\n", __func__);
        return -ESOCKOPT_INVAL;
    }

    len = sizeof(struct dpvs_sock_msg);
    res = sendn(clt_fd, hdr, len, MSG_NOSIGNAL);
    if (len != res) {
        fprintf(stderr, "[%s] socket msg header send error -- %d/%d sent\n",
                __func__, res, len);
        return -ESOCKOPT_IO;
    }

    if (data && data_len) {
        res = sendn(clt_fd, data, data_len, MSG_NOSIGNAL);
        if (data_len != res) {
            fprintf(stderr, "[%s] scoket msg body send error -- %d/%d sent\n",
                    __func__, res, data_len);
            return -ESOCKOPT_IO;
        }
    }

    return 0;
}

static inline int sockopt_msg_recv(int clt_fd, struct dpvs_sock_msg_reply *reply_hdr, 
        void **out, size_t *out_len)
{
    void *msg = NULL;
    int len, res;

    if (!reply_hdr) {
        fprintf(stderr, "[%s] empty reply msg pointer\n", __func__);
        return -ESOCKOPT_INVAL;
    }

    if (out)
        *out = NULL;
    if (out_len)
        *out_len = 0;

    len = sizeof(struct dpvs_sock_msg_reply);
    memset(reply_hdr, 0, len);
    res = readn(clt_fd, reply_hdr, len);
    if (len != res) {
        fprintf(stderr, "[%s] socket msg header recv error -- %d/%d recieved\n",
                __func__, res, len);
        return -ESOCKOPT_IO;
    }

    if (reply_hdr->errcode) {
        fprintf(stderr, "[%s] errcode set in socket msg#%d header: %s(%d)\n", __func__,
                reply_hdr->id, reply_hdr->errstr, reply_hdr->errcode);
        return reply_hdr->errcode;
    }

    if (reply_hdr->len > 0) {
        msg = malloc(reply_hdr->len);
        if (NULL == msg) {
            fprintf(stderr, "[%s] no memory\n", __func__);
            return -ESOCKOPT_NOMEM;
        }

        res = readn(clt_fd, msg, reply_hdr->len);
        if (res != reply_hdr->len) {
            fprintf(stderr, "[%s] socket msg body recv error -- %d/%d recieved\n",
                    __func__, res, (int)reply_hdr->len);
            free(msg);
            return -ESOCKOPT_IO;
        }
    }

    if (SOCKOPT_VERSION != reply_hdr->version) {
        fprintf(stderr, "[%s] socket msg version not match\n", __func__);
        if (reply_hdr->len > 0)
            free(msg);
        return -ESOCKOPT_VERSION;
    }

    if (out && out_len) {
        *out = msg;
        *out_len = reply_hdr->len;
    } else if (reply_hdr->len > 0) {
        free(msg);
        if (out)
            *out = NULL;
        if (out_len)
            *out_len = 0;
    }

    return ESOCKOPT_OK;
}

int dpvs_setsockopt(sockoptid_t cmd, const void *in, size_t in_len)
{
    struct dpvs_sock_msg *msg;
    struct dpvs_sock_msg_reply reply_hdr;
    struct sockaddr_un clt_addr;
    int clt_fd;
    int res;
    size_t msg_len;

    memset(&clt_addr, 0, sizeof(struct sockaddr_un));
    clt_addr.sun_family = AF_UNIX;
    strncpy(clt_addr.sun_path, UNIX_DOMAIN, sizeof(clt_addr.sun_path) - 1);

    msg_len = sizeof(struct dpvs_sock_msg);
    msg = malloc(msg_len);
    if (NULL == msg) {
        fprintf(stderr, "[%s] no memory\n", __func__);
        return -ESOCKOPT_INVAL;
    }

    clt_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    res = connect(clt_fd, (struct sockaddr *)&clt_addr, sizeof(clt_addr));
    if (-1 == res) {
        fprintf(stderr, "[%s] scoket msg connection error: %s\n",
                __func__, strerror(errno));
        free(msg);
        return -ESOCKOPT_IO;
    }

    memset(msg, 0, msg_len);
    msg->version = SOCKOPT_VERSION;
    msg->id = cmd;
    msg->type = SOCKOPT_SET;
    msg->len = in_len;
    res = sockopt_msg_send(clt_fd, msg, in, in_len);

    free(msg);
    msg = NULL;

    if (res) {
        close(clt_fd);
        return res;
    }

    res = sockopt_msg_recv(clt_fd, &reply_hdr, NULL, NULL);
    if (res) {
        close(clt_fd);
        return res;
    }

    if (reply_hdr.errcode) {
        fprintf(stderr, "[%s] Server error: %s\n", __func__, reply_hdr.errstr);
        close(clt_fd);
        return reply_hdr.errcode;
    }

    close(clt_fd);
    return ESOCKOPT_OK;
}

int dpvs_getsockopt(sockoptid_t cmd, const void *in, size_t in_len,
        void **out, size_t *out_len)
{
    struct dpvs_sock_msg *msg;
    struct dpvs_sock_msg_reply reply_hdr;
    struct sockaddr_un clt_addr;
    int clt_fd;
    int res;
    size_t msg_len;

    if (NULL == out || NULL == out_len) {
        fprintf(stderr, "[%s] no pointer for info return\n", __func__);
        return -1;
    }
    *out = NULL;
    *out_len = 0;

    memset(&clt_addr, 0, sizeof(struct sockaddr_un));
    clt_addr.sun_family = AF_UNIX;
    strncpy(clt_addr.sun_path, UNIX_DOMAIN, sizeof(clt_addr.sun_path) - 1);

    msg_len = sizeof(struct dpvs_sock_msg);
    msg = malloc(msg_len);
    if (NULL == msg) {
        fprintf(stderr, "[%s] no memory\n", __func__);
        return -ESOCKOPT_NOMEM;
    }

    clt_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    res = connect(clt_fd, (struct sockaddr*)&clt_addr, sizeof(clt_addr));
    if (-1 == res) {
        fprintf(stderr, "[%s] scoket msg connection error: %s\n",
                __func__, strerror(errno));
        free(msg);
        return -ESOCKOPT_IO;
    }

    memset(msg, 0, msg_len);
    msg->version = SOCKOPT_VERSION;
    msg->id = cmd;
    msg->type = SOCKOPT_GET;
    msg->len = in_len;
    res = sockopt_msg_send(clt_fd, msg, in, in_len);

    free(msg);
    msg = NULL;

    if (res) {
        close(clt_fd);
        return res;
    }

    res = sockopt_msg_recv(clt_fd, &reply_hdr, out, out_len);
    if (res) {
        close(clt_fd);
        return res;
    }

    if (reply_hdr.errcode) {
        fprintf(stderr, "[%s] Server error: %s\n", __func__, reply_hdr.errstr);
        close(clt_fd);
        return reply_hdr.errcode;
    }

    close(clt_fd);
    return ESOCKOPT_OK;
}
