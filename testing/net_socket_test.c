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
#include <stdatomic.h>
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

/* A finite burst exceeds the 64 KiB receive pipe but fits in both pipes.
 * There must be no send after draining starts: that would hide a lost wakeup. */
static int TestReceiveCredit(int type)
{
    int fds[2];
    char block[1024];
    const size_t total = 96 * sizeof(block);
    size_t sent = 0;
    size_t received = 0;
    CHECK(socketpair(AF_LOCAL, type, 0, fds) == 0);
    struct sockaddr_lc destination;
    socklen_t addressLength = sizeof(destination);
    CHECK(getsockname(fds[1], (struct sockaddr*)&destination, &addressLength) == 0);
    for (int attempt = 0; sent < total && attempt < 1000; attempt++) {
        for (size_t j = 0; j < sizeof(block); j++) {
            block[j] = (char)((sent + j) % 251);
        }
        intmax_t count = type == SOCK_DGRAM
                ? sendto(fds[0], block, sizeof(block), MSG_DONTWAIT,
                         (struct sockaddr*)&destination, addressLength)
                : send(fds[0], block, sizeof(block), MSG_DONTWAIT);
        if (count > 0) {
            CHECK(count <= sizeof(block));
            CHECK(type == SOCK_STREAM || count == sizeof(block));
            sent += count;
        } else {
            Pause();
        }
    }
    CHECK(sent == total);
    // Give netd time to block on the full receiver, then check another socket
    // still makes progress. No more writes to the tested pair from here on.
    for (int i = 0; i < 10; i++) {
        Pause();
    }
    CHECK(TestPair() == 0);
    for (int attempt = 0; received < total && attempt < 2000; attempt++) {
        // Small stream reads exercise partial queued-buffer retries.
        intmax_t count = recv(fds[1], block,
                             type == SOCK_STREAM ? 257 : sizeof(block), MSG_DONTWAIT);
        if (count > 0) {
            CHECK(count <= sizeof(block) && received + count <= total);
            CHECK(type == SOCK_STREAM || count == sizeof(block));
            for (size_t j = 0; j < (size_t)count; j++) {
                CHECK(block[j] == (char)((received + j) % 251));
            }
            received += count;
        } else {
            Pause();
        }
    }
    CHECK(received == total);
    CHECK(close(fds[0]) == 0 && close(fds[1]) == 0);
    NOTICE("NETTEST PASS finite receive-credit burst type %d", type);
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

/* Use immutable server IDs so this tests netd lifetime rather than racing
 * access to a client-side descriptor or OSHandle being destroyed. */
struct LookupRace {
    uuid_t handle;
    atomic_int started;
};

static int LookupThread(void* context)
{
    struct LookupRace* race = context;
    for (int i = 0; i < 16; i++) {
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetNetService());
        struct sockaddr_lc address = {0};
        oserr_t status;
        CHECK(sys_socket_get_address(GetGrachtClient(), &msg.base, race->handle,
                                     i % 2 ? SYS_ADDRESS_TYPE_THIS : SYS_ADDRESS_TYPE_PEER) == 0);
        if (i == 0) {
            atomic_store(&race->started, 1);
        }
        CHECK(gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC) == 0);
        CHECK(sys_socket_get_address_result(GetGrachtClient(), &msg.base, &status,
                                           (uint8_t*)&address, sizeof(address)) == 0);
        CHECK(status == OS_EOK || status == OS_ENOENT);
    }
    return 0;
}

static int TestCloseLifetime(void)
{
    for (int i = 0; i < 32; i++) {
        OSHandle_t sockets[2];
        struct LookupRace races[2];
        thrd_t threads[2];
        for (int j = 0; j < 2; j++) {
            CHECK(OSSocketOpen(AF_LOCAL, SOCK_STREAM, 0, &sockets[j]) == OS_EOK);
            races[j].handle = sockets[j].ID;
            atomic_init(&races[j].started, 0);
        }
        CHECK(OSSocketPair(&sockets[0], &sockets[1]) == OS_EOK);
        for (int j = 0; j < 2; j++) {
            CHECK(thrd_create(&threads[j], LookupThread, &races[j]) == thrd_success);
        }
        while (!atomic_load(&races[0].started) || !atomic_load(&races[1].started)) {
            Pause();
        }
        OSHandleDestroy(&sockets[i % 2]);
        OSHandleDestroy(&sockets[1 - i % 2]);
        for (int j = 0; j < 2; j++) {
            int result;
            CHECK(thrd_join(threads[j], &result) == thrd_success && result == 0);
        }

        /* Close source and peer with monitor events still outstanding. */
        int fds[2];
        CHECK(socketpair(AF_LOCAL, SOCK_STREAM, 0, fds) == 0);
        CHECK(send(fds[0], "pending", 8, 0) == 8);
        CHECK(send(fds[1], "pending", 8, 0) == 8);
        CHECK(close(fds[i % 2]) == 0);
        CHECK(close(fds[1 - i % 2]) == 0);

        /* Exercise address-based peer lookup racing receiver destruction. */
        int sender = socket(AF_LOCAL, SOCK_DGRAM, 0);
        int receiver = socket(AF_LOCAL, SOCK_DGRAM, 0);
        struct sockaddr_lc address = {
            .slc_len = sizeof(address), .slc_family = AF_LOCAL,
            .slc_addr = "/lc/close-race"
        };
        CHECK(sender >= 0 && receiver >= 0);
        CHECK(bind(receiver, (struct sockaddr*)&address, sizeof(address)) == 0);
        CHECK(sendto(sender, "pending", 8, 0, (struct sockaddr*)&address, sizeof(address)) == 8);
        CHECK(close(receiver) == 0);
        CHECK(close(sender) == 0);
    }
    /* Prove that the monitor still delivers after processing stale events. */
    CHECK(TestPair() == 0);
    NOTICE("NETTEST PASS concurrent socket/peer lookup and close with pending events");
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
    CHECK(TestReceiveCredit(SOCK_STREAM) == 0);
    CHECK(TestReceiveCredit(SOCK_DGRAM) == 0);
    CHECK(TestReceiveCredit(SOCK_SEQPACKET) == 0);
    CHECK(TestAccept() == 0);
    CHECK(TestCloseLifetime() == 0);
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
