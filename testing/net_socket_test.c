/* Opt-in guest integration tests: libc -> libos -> RPC -> netd -> shared memory. */
#include <ddk/service.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <inet/local.h>
#include <internal/_utils.h>
#include <io.h>
#include <limits.h>
#include <os/handle.h>
#include <os/services/net.h>
#include <string.h>
#include <threads.h>
#include <sys_socket_service_client.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        ERROR("NETTEST FAIL line %d: %s", __LINE__, #condition); \
        return -1; \
    } \
} while (0)

static void Pause(void)
{
    thrd_sleep(&(struct timespec){.tv_nsec = 10000000}, NULL);
}

static int Receive(int fd, const char* expected)
{
    char buffer[64] = {0};
    size_t length = strlen(expected) + 1;
    for (int i = 0; i < 500; i++) {
        intmax_t count = recv(fd, buffer, length, MSG_DONTWAIT);
        if (count > 0) {
            CHECK(count == length && memcmp(buffer, expected, length) == 0);
            return 0;
        }
        Pause();
    }
    CHECK(0);
}

static int TestErrors(void)
{
    const int invalidTypes[] = {-1, 0, SOCK_SEQPACKET + 1, INT_MAX};
    for (size_t i = 0; i < sizeof(invalidTypes) / sizeof(invalidTypes[0]); i++) {
        CHECK(socket(AF_LOCAL, invalidTypes[i], 0) == -1);
    }
    CHECK(socket(AF_INET, SOCK_STREAM, 0) == -1);
    CHECK(socket(AF_INET6, SOCK_STREAM, 0) == -1);

    OSHandle_t handle;
    CHECK(OSSocketOpen(AF_LOCAL, SOCK_STREAM, 0, &handle) == OS_EOK);
    for (int i = 0; i < 2; i++) {
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
        oserr_t status;
        uint8_t buffer[128];
        int length = -1;
        memset(buffer, 0xa5, sizeof(buffer));
        uuid_t id = i ? UUID_INVALID : handle.ID;
        CHECK(sys_socket_get_option(GetGrachtClient(), &msg.base, id, 0, SO_TYPE) == 0);
        CHECK(gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC) == 0);
        CHECK(sys_socket_get_option_result(GetGrachtClient(), &msg.base, &status,
                                          buffer, sizeof(buffer), &length) == 0);
        CHECK(status != OS_EOK && length == 0 && buffer[0] == 0xa5);

        msg = (struct vali_link_message)VALI_MSG_INIT_HANDLE(GetNetService());
        CHECK(sys_socket_get_address(GetGrachtClient(), &msg.base, id, SYS_ADDRESS_TYPE_PEER) == 0);
        CHECK(gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC) == 0);
        CHECK(sys_socket_get_address_result(GetGrachtClient(), &msg.base, &status,
                                           buffer, sizeof(buffer)) == 0);
        CHECK(status != OS_EOK && buffer[0] == 0xa5);
    }

    uint8_t address[128];
    for (int i = 0; i < 6; i++) {
        memset(address, 'x', sizeof(address));
        size_t length = sizeof(struct sockaddr_lc);
        address[0] = (uint8_t)length;
        address[1] = AF_LOCAL;
        switch (i) {
            case 0: length = 0; break;
            case 1: length = 1; break;
            case 2: break; /* unterminated */
            case 3: length = sizeof(address); address[0] = length; address[length - 1] = 0; break;
            case 4: address[1] = AF_INET; address[length - 1] = 0; break;
            case 5: address[0] = 3; address[length - 1] = 0; break;
        }
        CHECK(OSSocketBind(&handle, (struct sockaddr*)address, length) == OS_EINVALPARAMS);
        CHECK(OSSocketConnect(&handle, (struct sockaddr*)address, length) == OS_EINVALPARAMS);
    }
    OSHandleDestroy(&handle);
    NOTICE("NETTEST PASS errors and malformed addresses");
    return 0;
}

static int TestPair(void)
{
    int fds[2];
    CHECK(socketpair(AF_LOCAL, SOCK_STREAM, 0, fds) == 0);
    CHECK(send(fds[0], "forward", 8, 0) == 8);
    CHECK(Receive(fds[1], "forward") == 0);
    CHECK(send(fds[1], "reverse", 8, 0) == 8);
    CHECK(Receive(fds[0], "reverse") == 0);
    CHECK(close(fds[0]) == 0);
    CHECK(close(fds[1]) == 0);
    NOTICE("NETTEST PASS socketpair bidirectional transfer and close");
    return 0;
}

static int TestDatagram(void)
{
    int sender = socket(AF_LOCAL, SOCK_DGRAM, 0);
    int receiver = socket(AF_LOCAL, SOCK_DGRAM, 0);
    struct sockaddr_lc address = {
        .slc_len = sizeof(address), .slc_family = AF_LOCAL
    };
    CHECK(sender >= 0 && receiver >= 0);
    /* The longest supported address must round-trip with its terminator. */
    memset(address.slc_addr, 'z', sizeof(address.slc_addr) - 1);
    CHECK(bind(receiver, (struct sockaddr*)&address, sizeof(address)) == 0);
    struct sockaddr_lc actual = {0};
    socklen_t length = sizeof(actual);
    CHECK(getsockname(receiver, (struct sockaddr*)&actual, &length) == 0);
    CHECK(length == sizeof(actual) && memcmp(&actual, &address, sizeof(actual)) == 0);

    /* Malformed packet addresses must be dropped without breaking the monitor. */
    struct sockaddr_lc malformed = address;
    memset(malformed.slc_addr, 'x', sizeof(malformed.slc_addr));
    CHECK(sendto(sender, "bad", 4, 0, (struct sockaddr*)&malformed, sizeof(malformed)) == 4);
    CHECK(sendto(sender, "datagram", 9, 0, (struct sockaddr*)&address, sizeof(address)) == 9);
    CHECK(Receive(receiver, "datagram") == 0);

    /* A short, valid address expands to a full source-address slot on receive. */
    struct sockaddr_lc shortAddress = {
        .slc_len = offsetof(struct sockaddr_lc, slc_addr) + 3,
        .slc_family = AF_LOCAL, .slc_addr = "nt"
    };
    CHECK(bind(receiver, (struct sockaddr*)&shortAddress, shortAddress.slc_len) == 0);
    CHECK(sendto(sender, "short", 6, 0, (struct sockaddr*)&shortAddress, shortAddress.slc_len) == 6);
    CHECK(Receive(receiver, "short") == 0);
    CHECK(close(sender) == 0 && close(receiver) == 0);
    NOTICE("NETTEST PASS datagrams and address boundaries");
    return 0;
}

static struct sockaddr_lc g_listenAddress = {
    .slc_len = sizeof(struct sockaddr_lc), .slc_family = AF_LOCAL,
    .slc_addr = "/lc/nettest"
};

static int ConnectThread(void* context)
{
    int fd = *(int*)context;
    CHECK(connect(fd, (struct sockaddr*)&g_listenAddress, sizeof(g_listenAddress)) == 0);
    return 0;
}

static int TestAccept(void)
{
    int listener = socket(AF_LOCAL, SOCK_STREAM, 0);
    CHECK(listener >= 0);
    CHECK(bind(listener, (struct sockaddr*)&g_listenAddress, sizeof(g_listenAddress)) == 0);
    CHECK(listen(listener, 2) == 0);
    for (int i = 0; i < 2; i++) {
        int client = socket(AF_LOCAL, SOCK_STREAM, 0);
        thrd_t thread;
        CHECK(client >= 0);
        CHECK(thrd_create(&thread, ConnectThread, &client) == thrd_success);
        struct sockaddr_lc peer;
        socklen_t length = sizeof(peer);
        int accepted = accept(listener, i ? (struct sockaddr*)&peer : NULL, i ? &length : NULL);
        CHECK(accepted >= 0);
        int result;
        CHECK(thrd_join(thread, &result) == thrd_success && result == 0);
        if (i) {
            CHECK(length == sizeof(peer) && peer.slc_family == AF_LOCAL);
        }
        CHECK(send(client, "accepted", 9, 0) == 9);
        CHECK(Receive(accepted, "accepted") == 0);
        CHECK(send(accepted, "reply", 6, 0) == 6);
        CHECK(Receive(client, "reply") == 0);
        CHECK(close(accepted) == 0 && close(client) == 0);
    }
    CHECK(close(listener) == 0);
    NOTICE("NETTEST PASS bind listen connect accept");
    return 0;
}

static int RunTests(void* context)
{
    (void)context;
    NOTICE("NETTEST START");
    CHECK(WaitForNetService(10000) == OS_EOK);
    NOTICE("NETTEST service ready");
    CHECK(TestErrors() == 0);
    CHECK(TestPair() == 0);
    CHECK(TestDatagram() == 0);
    CHECK(TestAccept() == 0);
    NOTICE("NETTEST ALL PASS");
    return 0;
}

void ServiceInitialize(struct ServiceStartupOptions* options)
{
    thrd_t thread;
    (void)options;
    if (thrd_create(&thread, RunTests, NULL) != thrd_success) {
        ERROR("NETTEST FAIL could not start test thread");
    } else {
        thrd_detach(thread);
    }
}
