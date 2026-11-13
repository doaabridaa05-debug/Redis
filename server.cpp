#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cassert>
#include <fcntl.h>
#include <vector>
#include <poll.h>
#include <errno.h>
#include <map>
#include <string>
#include <algorithm>
#include <time.h>
#include "hashtable.h"
#include "zset.h"
#include "heap.h"
#include "thread_pool.h"

// 1. La macro container_of
#define container_of(ptr, type, member) ({ \
    const typeof( ((type *)0)->member ) *__mptr = (ptr); \
    (type *)( (char *)__mptr - offsetof(type, member) ); \
})

// 2. Les types de données
enum {
    T_STR  = 1, // Chaîne de caractères standard
    T_ZSET = 2, // Sorted Set (Arbre AVL + Hashtable)
};

// Liste doublement chaînée pour les timers d'inactivité
struct DList {
    DList *prev = nullptr;
    DList *next = nullptr;
};

inline void dlist_init(DList *node) {
    node->prev = node->next = node;
}

inline bool dlist_empty(DList *node) {
    return node->next == node;
}

inline void dlist_detach(DList *node) {
    DList *prev = node->prev;
    DList *next = node->next;
    prev->next = next;
    next->prev = prev;
}

inline void dlist_insert_before(DList *target, DList *rookie) {
    DList *prev = target->prev;
    prev->next = rookie;
    rookie->prev = prev;
    rookie->next = target;
    target->prev = rookie;
}

// 3. Définition de la structure Entry
struct Entry {
    struct HNode node;  // Nœud de la table de hachage principale
    std::string key;    // Clé de l'entrée
    uint32_t type = 0;  // Type de la valeur (T_STR ou T_ZSET)
    size_t heap_idx = (size_t)-1; // Index pour le TTL Min-Heap

    // Valeur selon le type
    std::string str;
    ZSet zset;
};

// Stockage de la connexion
struct Conn {
    int fd = -1;
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;
    std::vector<uint8_t> incoming;  
    std::vector<uint8_t> outgoing;  
    
    // Champs pour l'idle timer
    uint64_t last_active_ms = 0;
    DList idle_node;
};

// Structure globale de la base de données
const uint64_t k_idle_timeout_ms = 5000; // 5 secondes d'inactivité

static struct {
    HMap db;
    std::vector<Conn *> fd2conn;
    DList idle_list; // Tête de liste sentinelle pour les connexions inactives
    ThreadPool thread_pool;
    std::vector<HeapItem> heap; // Min-heap pour les expirations TTL
} g_data;

// Fonction de hachage simple
static uint64_t str_hash(const uint8_t *data, size_t len) {
    uint32_t h = 0x811c9dc5;
    for (size_t i = 0; i < len; i++) {
        h = (h ^ data[i]) * 0x01000193;
    }
    return h;
}

// Comparaison d'égalité pour la table de hachage
static bool entry_eq(HNode *lhs, HNode *rhs) {
    struct Entry *le = container_of(lhs, struct Entry, node);
    struct Entry *re = container_of(rhs, struct Entry, node);
    return le->key == re->key;
}

// Déclaration anticipée de entry_set_ttl
static void entry_set_ttl(Entry *ent, int64_t ttl_ms);

// Fonction pour afficher une erreur et quitter
static void die(const char *msg) {
    perror(msg);
    exit(1);
}

const size_t k_max_msg = 4096;
const uint32_t k_max_args = 1024;

static void fd_set_nb(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

static void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

static void buf_consume(std::vector<uint8_t> &buf, size_t n) {
    buf.erase(buf.begin(), buf.begin() + n);
}

// Horloge monotone pour les timers
static uint64_t get_monotonic_msec() {
    struct timespec tv = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_nsec / 1000 / 1000;
}

// Fonctions utilitaires de lecture sécurisée
static bool read_u32(const uint8_t *&cur, const uint8_t *end, uint32_t &out) {
    if (cur + 4 > end) return false;
    memcpy(&out, cur, 4);
    cur += 4;
    return true;
}

static bool read_str(const uint8_t *&cur, const uint8_t *end, size_t n, std::string &out) {
    if (cur + n > end) return false;
    out.assign(cur, cur + n);
    cur += n;
    return true;
}

// Parseur de requêtes binaire
static int32_t parse_req(const uint8_t *data, size_t size, std::vector<std::string> &out) {
    const uint8_t *end = data + size;
    uint32_t nstr = 0;

    if (!read_u32(data, end, nstr)) return -1;
    if (nstr > k_max_args) return -1;

    while (out.size() < nstr) {
        uint32_t len = 0;
        if (!read_u32(data, end, len)) return -1;
        out.push_back(std::string());
        if (!read_str(data, end, len, out.back())) return -1;
    }

    if (data != end) return -1;
    return 0;
}

// Types de données du protocole sérialisé
enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

static void out_nil(std::vector<uint8_t> &out) {
    out.push_back(TAG_NIL);
}

static void out_str(std::vector<uint8_t> &out, const char *s, size_t size) {
    out.push_back(TAG_STR);
    uint32_t len = (uint32_t)size;
    out.insert(out.end(), (const uint8_t *)&len, (const uint8_t *)&len + 4);
    out.insert(out.end(), (const uint8_t *)s, (const uint8_t *)s + size);
}

static void out_int(std::vector<uint8_t> &out, int64_t val) {
    out.push_back(TAG_INT);
    out.insert(out.end(), (const uint8_t *)&val, (const uint8_t *)&val + 8);
}

static void out_dbl(std::vector<uint8_t> &out, double val) {
    out.push_back(TAG_DBL);
    out.insert(out.end(), (const uint8_t *)&val, (const uint8_t *)&val + 8);
}

static void out_err(std::vector<uint8_t> &out, const char *msg) {
    out.push_back(TAG_ERR);
    uint32_t len = (uint32_t)strlen(msg);
    out.insert(out.end(), (const uint8_t *)&len, (const uint8_t *)&len + 4);
    out.insert(out.end(), (const uint8_t *)msg, (const uint8_t *)msg + len);
}

static void response_begin(std::vector<uint8_t> &out, size_t *header_pos) {
    *header_pos = out.size();
    uint32_t zero = 0;
    out.insert(out.end(), (const uint8_t *)&zero, (const uint8_t *)&zero + 4);
}

static void response_end(std::vector<uint8_t> &out, size_t header_pos) {
    uint32_t body_len = (uint32_t)(out.size() - header_pos - 4);
    memcpy(&out[header_pos], &body_len, 4);
}

// Gestion du TTL
static void entry_set_ttl(Entry *ent, int64_t ttl_ms) {
    if (ttl_ms < 0 && ent->heap_idx != (size_t)-1) {
        heap_delete(g_data.heap, ent->heap_idx);
        ent->heap_idx = (size_t)-1;
    } else if (ttl_ms >= 0) {
        uint64_t expire_at = get_monotonic_msec() + (uint64_t)ttl_ms;
        HeapItem item = {expire_at, &ent->heap_idx};
        heap_upsert(g_data.heap, ent->heap_idx, item);
    }
}

// Destruction synchrone classique pour les petits objets
static void entry_del_sync(Entry *ent) {
    entry_set_ttl(ent, -1);
    if (ent->type == T_STR) {
        // Rien
    } else if (ent->type == T_ZSET) {
        zset_clear(&ent->zset);
    }
    delete ent;
}

static void entry_del_func(void *arg) {
    entry_del_sync((Entry *)arg);
}

// Fonction de suppression asynchrone/synchrone selon la taille
static void entry_del(Entry *ent) {
    size_t set_size = (ent->type == T_ZSET) ? hm_size(&ent->zset.hmap) : 0;
    const size_t k_large_container_size = 1000;
    
    if (set_size > k_large_container_size) {
        thread_pool_queue(&g_data.thread_pool, &entry_del_func, ent);
    } else {
        entry_del_sync(ent);
    }
}

// Logique métier
static void do_request(std::vector<std::string> &cmd, std::vector<uint8_t> &out) {
    if (cmd.size() == 2 && cmd[0] == "get") {
        Entry key;
        key.key = cmd[1];
        key.node.hcode = str_hash((const uint8_t *)key.key.data(), key.key.size());

        HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
        if (!node) {
            out_nil(out);
            return;
        }

        Entry *ent = container_of(node, Entry, node);
        if (ent->type != T_STR) {
            out_err(out, "ERR Type mismatch");
            return;
        }
        out_str(out, ent->str.data(), ent->str.size());
    } 
    else if (cmd.size() == 3 && cmd[0] == "set") {
        Entry key;
        key.key = cmd[1];
        key.node.hcode = str_hash((const uint8_t *)key.key.data(), key.key.size());

        HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
        if (node) {
            Entry *ent = container_of(node, Entry, node);
            if (ent->type != T_STR) {
                out_err(out, "ERR Type mismatch");
                return;
            }
            ent->str = cmd[2];
        } else {
            Entry *ent = new Entry();
            ent->key.swap(cmd[1]);
            ent->type = T_STR;
            ent->str.swap(cmd[2]);
            ent->node.hcode = key.node.hcode;
            hm_insert(&g_data.db, &ent->node);
        }
        out_int(out, 0);
    } 
    else if (cmd.size() == 2 && cmd[0] == "del") {
        Entry key;
        key.key = cmd[1];
        key.node.hcode = str_hash((const uint8_t *)key.key.data(), key.key.size());

        HNode *node = hm_pop(&g_data.db, &key.node, &entry_eq);
        if (node) {
            Entry *ent = container_of(node, Entry, node);
            entry_del(ent);
            out_int(out, 1);
        } else {
            out_int(out, 0);
        }
    }
    else if (cmd.size() == 4 && cmd[0] == "zadd") {
        double score = 0;
        try {
            score = std::stod(cmd[2]);
        } catch (...) {
            out_err(out, "ERR value is not a valid float");
            return;
        }

        Entry key;
        key.key = cmd[1];
        key.node.hcode = str_hash((const uint8_t *)key.key.data(), key.key.size());

        HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
        Entry *ent = nullptr;
        if (!node) {
            ent = new Entry();
            ent->key.swap(cmd[1]);
            ent->type = T_ZSET;
            ent->node.hcode = key.node.hcode;
            hm_insert(&g_data.db, &ent->node);
        } else {
            ent = container_of(node, Entry, node);
            if (ent->type != T_ZSET) {
                out_err(out, "ERR Type mismatch");
                return;
            }
        }

        bool added = zset_insert(&ent->zset, cmd[3].data(), cmd[3].size(), score);
        out_int(out, added ? 1 : 0);
    }
    else if (cmd.size() == 3 && cmd[0] == "zscore") {
        Entry key;
        key.key = cmd[1];
        key.node.hcode = str_hash((const uint8_t *)key.key.data(), key.key.size());

        HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
        if (!node) {
            out_nil(out);
            return;
        }
        Entry *ent = container_of(node, Entry, node);
        if (ent->type != T_ZSET) {
            out_err(out, "ERR Type mismatch");
            return;
        }

        ZNode *znode = zset_lookup(&ent->zset, cmd[2].data(), cmd[2].size());
        if (!znode) {
            out_nil(out);
        } else {
            out_dbl(out, znode->score);
        }
    }
    else if (cmd.size() == 3 && cmd[0] == "zrem") {
        Entry key;
        key.key = cmd[1];
        key.node.hcode = str_hash((const uint8_t *)key.key.data(), key.key.size());

        HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
        if (!node) {
            out_int(out, 0);
            return;
        }
        Entry *ent = container_of(node, Entry, node);
        if (ent->type != T_ZSET) {
            out_err(out, "ERR Type mismatch");
            return;
        }

        ZNode *znode = nullptr;
        bool deleted = zset_delete(&ent->zset, cmd[2].data(), cmd[2].size(), &znode);
        if (deleted) {
            free(znode);
            out_int(out, 1);
        } else {
            out_int(out, 0);
        }
    }
    else if (cmd.size() == 3 && cmd[0] == "expire") {
        int64_t ttl_ms = 0;
        try {
            ttl_ms = std::stoll(cmd[2]);
        } catch (...) {
            out_err(out, "ERR value is not an integer");
            return;
        }

        Entry key;
        key.key = cmd[1];
        key.node.hcode = str_hash((const uint8_t *)key.key.data(), key.key.size());

        HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
        if (!node) {
            out_int(out, 0);
            return;
        }

        Entry *ent = container_of(node, Entry, node);
        entry_set_ttl(ent, ttl_ms);
        out_int(out, 1);
    }
    else {
        out_nil(out);
    }
}

static bool try_one_request(Conn *conn) {
    if (conn->incoming.size() < 4) return false;
    uint32_t len = 0;
    memcpy(&len, conn->incoming.data(), 4);
    if (len > k_max_msg) {
        std::cerr << "Message trop long" << std::endl;
        conn->want_close = true;
        return false;
    }
    if (4 + len > conn->incoming.size()) return false;

    const uint8_t *request = &conn->incoming[4];
    std::vector<std::string> cmd;
    
    if (parse_req(request, len, cmd) < 0) {
        std::cerr << "Erreur de parsing de la requête" << std::endl;
        conn->want_close = true;
        return false;
    }

    size_t header_pos = 0;
    response_begin(conn->outgoing, &header_pos);
    do_request(cmd, conn->outgoing);
    response_end(conn->outgoing, header_pos);

    buf_consume(conn->incoming, 4 + len);
    return true;
}

static void handle_read(Conn *conn) {
    conn->last_active_ms = get_monotonic_msec();
    dlist_detach(&conn->idle_node);
    dlist_insert_before(&g_data.idle_list, &conn->idle_node);

    uint8_t buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv <= 0) {
        conn->want_close = true;
        return;
    }

    buf_append(conn->incoming, buf, (size_t)rv);
    while (try_one_request(conn)) {}

    if (conn->outgoing.size() > 0) {
        conn->want_read = false;
        conn->want_write = true;
    }
}

static void handle_write(Conn *conn) {
    conn->last_active_ms = get_monotonic_msec();
    dlist_detach(&conn->idle_node);
    dlist_insert_before(&g_data.idle_list, &conn->idle_node);

    assert(conn->outgoing.size() > 0);
    ssize_t rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());
    if (rv < 0 && errno == EAGAIN) return;
    if (rv < 0) {
        conn->want_close = true;
        return;
    }

    buf_consume(conn->outgoing, (size_t)rv);
    if (conn->outgoing.size() == 0) {
        conn->want_read = true;
        conn->want_write = false;
    }
}

static void conn_destroy(Conn *conn) {
    (void)close(conn->fd);
    g_data.fd2conn[conn->fd] = NULL;
    dlist_detach(&conn->idle_node);
    delete conn;
}

static int32_t next_timer_ms() {
    uint64_t now_ms = get_monotonic_msec();
    uint64_t next_ms = (uint64_t)-1;

    if (!dlist_empty(&g_data.idle_list)) {
        Conn *conn = container_of(g_data.idle_list.next, Conn, idle_node);
        next_ms = conn->last_active_ms + k_idle_timeout_ms;
    }

    if (!g_data.heap.empty() && g_data.heap[0].val < next_ms) {
        next_ms = g_data.heap[0].val;
    }

    if (next_ms == (uint64_t)-1) {
        return -1;
    }
    if (next_ms <= now_ms) {
        return 0;
    }
    return (int32_t)(next_ms - now_ms);
}

static void process_timers() {
    uint64_t now_ms = get_monotonic_msec();
    
    while (!dlist_empty(&g_data.idle_list)) {
        Conn *conn = container_of(g_data.idle_list.next, Conn, idle_node);
        uint64_t next_ms = conn->last_active_ms + k_idle_timeout_ms;
        if (next_ms >= now_ms) {
            break;
        }
        std::cerr << "Fermeture de la connexion inactive fd=" << conn->fd << std::endl;
        conn_destroy(conn);
    }

    const size_t k_max_works = 2000;
    size_t nworks = 0;
    while (!g_data.heap.empty() && g_data.heap[0].val < now_ms && nworks++ < k_max_works) {
        Entry *ent = container_of(g_data.heap[0].ref, Entry, heap_idx);
        HNode *node = hm_pop(&g_data.db, &ent->node, &entry_eq);
        assert(node);
        entry_del(ent);
    }
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) die("socket()");

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(0);

    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr))) die("bind()");
    if (listen(fd, SOMAXCONN)) die("listen()");

    fd_set_nb(fd);

    dlist_init(&g_data.idle_list);
    std::vector<struct pollfd> poll_args;
    thread_pool_init(&g_data.thread_pool, 4);

    while (true) {
        poll_args.clear();

        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);

        for (Conn *conn : g_data.fd2conn) {
            if (!conn) continue;
            struct pollfd cpfd = {conn->fd, POLLERR, 0};
            if (conn->want_read) cpfd.events |= POLLIN;
            if (conn->want_write) cpfd.events |= POLLOUT;
            poll_args.push_back(cpfd);
        }

        int32_t timeout_ms = next_timer_ms();

        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), timeout_ms);
        if (rv < 0 && errno == EINTR) continue;
        if (rv < 0) die("poll()");

        process_timers();

        if (poll_args[0].revents & POLLIN) {
            struct sockaddr_in client_addr = {};
            socklen_t addrlen = sizeof(client_addr);
            int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
            if (connfd >= 0) {
                fd_set_nb(connfd);
                Conn *conn = new Conn();
                conn->fd = connfd;
                conn->want_read = true;
                conn->last_active_ms = get_monotonic_msec();
                dlist_insert_before(&g_data.idle_list, &conn->idle_node);

                if (g_data.fd2conn.size() <= (size_t)connfd) {
                    g_data.fd2conn.resize(connfd + 1);
                }
                g_data.fd2conn[connfd] = conn;
            }
        }

        for (size_t i = 1; i < poll_args.size(); ++i) {
            uint32_t ready = poll_args[i].revents;
            if (!ready) continue;

            Conn *conn = g_data.fd2conn[poll_args[i].fd];
            if (ready & POLLIN) handle_read(conn);
            if (ready & POLLOUT) handle_write(conn);

            if ((ready & POLLERR) || (conn && conn->want_close)) {
                conn_destroy(conn);
            }
        }
    }

    close(fd);
    return 0;
}