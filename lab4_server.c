#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lab4.h"

typedef struct Node {
    LONG key;
    int red;
    struct Node *left, *right, *parent;
} Node;

static Node NIL_NODE = { 0, 0, &NIL_NODE, &NIL_NODE, &NIL_NODE };
#define NIL (&NIL_NODE)

static Node *root = NIL;
static LONG count = 0;

static void rotate_left(Node *x) {
    Node *y = x->right;
    x->right = y->left;
    if (y->left != NIL)
        y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == NIL)
        root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;
    y->left = x;
    x->parent = y;
}

static void rotate_right(Node *x) {
    Node *y = x->left;
    x->left = y->right;
    if (y->right != NIL)
        y->right->parent = x;
    y->parent = x->parent;
    if (x->parent == NIL)
        root = y;
    else if (x == x->parent->right)
        x->parent->right = y;
    else
        x->parent->left = y;
    y->right = x;
    x->parent = y;
}

static void insert_fixup(Node *z) {
    while (z->parent->red) {
        Node *gp = z->parent->parent;
        if (z->parent == gp->left) {
            Node *uncle = gp->right;
            if (uncle->red) {
                z->parent->red = 0;
                uncle->red = 0;
                gp->red = 1;
                z = gp;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    rotate_left(z);
                }
                z->parent->red = 0;
                z->parent->parent->red = 1;
                rotate_right(z->parent->parent);
            }
        } else {
            Node *uncle = gp->left;
            if (uncle->red) {
                z->parent->red = 0;
                uncle->red = 0;
                gp->red = 1;
                z = gp;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rotate_right(z);
                }
                z->parent->red = 0;
                z->parent->parent->red = 1;
                rotate_left(z->parent->parent);
            }
        }
    }
    root->red = 0;
}

// Возвращает 0, если такое число уже есть в дереве
static int tree_insert(LONG key) {
    Node *parent = NIL, *cur = root;
    while (cur != NIL) {
        parent = cur;
        if (key == cur->key)
            return 0;
        cur = key < cur->key ? cur->left : cur->right;
    }

    Node *z = (Node *)malloc(sizeof(Node));
    if (!z)
        return 0;
    z->key = key;
    z->red = 1;
    z->left = z->right = NIL;
    z->parent = parent;
    if (parent == NIL)
        root = z;
    else if (key < parent->key)
        parent->left = z;
    else
        parent->right = z;

    insert_fixup(z);
    count++;
    return 1;
}

static void tree_free(Node *n) {
    if (n == NIL)
        return;
    tree_free(n->left);
    tree_free(n->right);
    free(n);
}

static void print_node(Node *n, char *prefix, int is_last, const char *side, HANDLE out, WORD normal) {
    printf("%s%s%s ", prefix, is_last ? "└── " : "├── ", side);
    fflush(stdout);
    if (n->red)
        SetConsoleTextAttribute(out, FOREGROUND_RED | FOREGROUND_INTENSITY);
    printf("%ld [%s]\n", (long)n->key, n->red ? "красный" : "чёрный");
    fflush(stdout);
    SetConsoleTextAttribute(out, normal);

    size_t len = strlen(prefix);
    if (len + 8 >= 1024)
        return;
    strcat(prefix, is_last ? "    " : "│   ");

    // Правый потомок идёт первым, поэтому он "последний" только если левого нет
    if (n->right != NIL)
        print_node(n->right, prefix, n->left == NIL, "R:", out, normal);
    if (n->left != NIL)
        print_node(n->left, prefix, 1, "L:", out, normal);
    prefix[len] = '\0';
}

static void print_tree(Node *root_node, HANDLE out, WORD normal) {
    char prefix[1024] = "";
    printf("КОРЕНЬ ");
    fflush(stdout);
    if (root_node->red)
        SetConsoleTextAttribute(out, FOREGROUND_RED | FOREGROUND_INTENSITY);
    printf("%ld [%s]\n", (long)root_node->key, root_node->red ? "красный" : "чёрный");
    fflush(stdout);
    SetConsoleTextAttribute(out, normal);

    if (root_node->right != NIL)
        print_node(root_node->right, prefix, root_node->left == NIL, "R:", out, normal);
    if (root_node->left != NIL)
        print_node(root_node->left, prefix, 1, "L:", out, normal);
}

static int tree_height(Node *n) {
    if (n == NIL)
        return 0;
    int l = tree_height(n->left), r = tree_height(n->right);
    return 1 + (l > r ? l : r);
}

static void fill_sorted(Node *n, LONG *buf, LONG *pos) {
    if (n == NIL)
        return;
    fill_sorted(n->left, buf, pos);
    buf[(*pos)++] = n->key;
    fill_sorted(n->right, buf, pos);
}

static void show_tree(void) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    WORD normal = GetConsoleScreenBufferInfo(out, &info) ? info.wAttributes
                    : (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

    printf("========== Красно-чёрное дерево ==========\n");
    printf("Элементов: %ld, высота: %d\n", (long)count, tree_height(root));
    if (root == NIL)
        printf("(пусто)\n");
    else
        print_tree(root, out, normal);
    printf("==========================================\n");
    fflush(stdout);
}

static int send_sorted(HANDLE pipe) {
    DWORD written;
    LONG n = count;
    if (!WriteFile(pipe, &n, sizeof n, &written, NULL))
        return 0;
    if (n == 0)
        return 1;

    LONG *buf = (LONG *)malloc(sizeof(LONG) * n);
    if (!buf)
        return 0;
    LONG pos = 0;
    fill_sorted(root, buf, &pos);
    BOOL ok = WriteFile(pipe, buf, sizeof(LONG) * n, &written, NULL);
    free(buf);
    return ok;
}

int main(void) {
    SetConsoleOutputCP(CP_UTF8);

    HANDLE evCreate = CreateEventA(NULL, FALSE, FALSE, LAB4_EVENT_NAME); // автосброс
    if (!evCreate) {
        fprintf(stderr, "CreateEvent failed: %lu\n", GetLastError());
        return 1;
    }

    int quit = 0;
    while (!quit) {
        printf("Сервер ждёт сигнал на создание канала...\n");
        fflush(stdout);
        WaitForSingleObject(evCreate, INFINITE);

        HANDLE pipe = CreateNamedPipeA(LAB4_PIPE_NAME, PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                       1, 4096, 4096, 0, NULL);
        if (pipe == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "CreateNamedPipe failed: %lu\n", GetLastError());
            return 1;
        }
        printf("Сигнал получен, канал %s создан, ждём клиента\n", LAB4_PIPE_NAME);
        fflush(stdout);

        if (!ConnectNamedPipe(pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
            fprintf(stderr, "ConnectNamedPipe failed: %lu\n", GetLastError());
            CloseHandle(pipe);
            continue;
        }
        printf("Клиент подключён\n");
        fflush(stdout);

        Lab4Message msg;
        DWORD n;
        int session = 1;
        while (session && ReadFile(pipe, &msg, sizeof msg, &n, NULL) && n == sizeof msg) {
            switch (msg.cmd) {
            case CMD_INSERT:
                if (tree_insert(msg.value))
                    printf("Получено число %ld, добавлено в дерево\n", (long)msg.value);
                else
                    printf("Получено число %ld, уже есть в дереве\n", (long)msg.value);
                fflush(stdout);
                break;
            case CMD_PRINT:
                show_tree();
                break;
            case CMD_GET:
                if (!send_sorted(pipe))
                    session = 0;
                break;
            case CMD_EXIT:
                quit = 1;
                session = 0;
                break;
            case CMD_CLOSE:
                session = 0;
                break;
            }
        }

        printf("Сеанс завершён\n");
        fflush(stdout);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    show_tree();
    tree_free(root);
    CloseHandle(evCreate);
    return 0;
}
